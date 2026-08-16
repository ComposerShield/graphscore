// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/paste_fragment_command.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/clef_lane.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_fragment.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/staff_layout.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/voice_content.hpp>
#include "clipboard_command_helpers.hpp"

namespace graphscore {

namespace {

using LaneSnapshot = std::vector<std::pair<TrackId, TrackLane>>;
using ClefPreSnapshot =
    std::vector<std::pair<StaveId, std::optional<ClefLane>>>;
using ClefPostSnapshot = std::vector<std::pair<StaveId, ClefLane>>;

struct PasteCommitBundle {
  LaneSnapshot            pre_snapshot;
  LaneSnapshot            post_snapshot;
  LaneSnapshot            candidates;
  std::vector<TrackLane*> live_lanes;
};

TrackLane* find_candidate(
    std::vector<std::pair<TrackId, TrackLane>>& candidates, TrackId track_id) {
  for (auto& entry : candidates) {
    if (entry.first == track_id)
      return &entry.second;
  }
  return nullptr;
}

// ---- meter compatibility gate ----

void push_distinct_meter(std::vector<TimeSignature>& sigs, TimeSignature sig) {
  if (std::find(sigs.begin(), sigs.end(), sig) == sigs.end())
    sigs.push_back(sig);
}

std::vector<TimeSignature> fragment_meter_set(
    const NotationFragment& fragment) {
  std::vector<TimeSignature> sigs;
  for (const FragmentMeasureContext& context : fragment.measure_contexts())
    push_distinct_meter(sigs, context.time_signature);
  return sigs;
}

// The set of distinct TimeSignature values governing the destination
// measures overlapping [range_start, range_end). A portion of the range at
// or past the main-region/pickdown boundary is governed by the last
// main-region measure's signature (NodeTimeline has no measures of its own
// in the pickdown), matching NotationFragment's own pickdown-origin rule.
std::vector<TimeSignature> destination_meter_set(const NodeTimeline& timeline,
                                                 Rational range_start,
                                                 Rational range_end) {
  std::vector<TimeSignature> sigs;
  const MeasureMap&          measures = timeline.measures();
  const Rational             boundary = timeline.boundary_position();

  if (range_start < boundary) {
    std::optional<std::size_t> index = measures.measure_index_at(range_start);
    while (index.has_value() && *index < measures.measure_count() &&
           measures.measure_start(*index) < range_end) {
      push_distinct_meter(sigs, measures.measure(*index).time_signature);
      ++(*index);
    }
  }
  if (range_end > boundary) {
    const std::size_t last_index = measures.measure_count() - 1;
    push_distinct_meter(sigs, measures.measure(last_index).time_signature);
  }
  return sigs;
}

bool meter_compatible(const NotationFragment& fragment,
                      const NodeTimeline& timeline, Rational range_start,
                      Rational range_end) {
  const std::vector<TimeSignature> fragment_sigs = fragment_meter_set(fragment);
  const std::vector<TimeSignature> dest_sigs =
      destination_meter_set(timeline, range_start, range_end);
  if (fragment_sigs.size() == 1 && dest_sigs.size() == 1)
    return fragment_sigs.front() == dest_sigs.front();
  if (fragment_sigs.empty() || dest_sigs.empty())
    return false;

  const MeasureMap&                measures = timeline.measures();
  const std::optional<std::size_t> first =
      measures.measure_index_at(range_start);
  if (!first.has_value())
    return false;
  std::vector<FragmentMeasureContext> destination_contexts;
  destination_contexts.push_back(FragmentMeasureContext{
      Rational(0), measures.measure(*first).time_signature,
      measures.measure(*first).key_signature});
  for (std::size_t index = *first + 1; index < measures.measure_count();
       ++index) {
    const Rational position = measures.measure_start(index);
    if (!(position < range_end))
      break;
    destination_contexts.push_back(FragmentMeasureContext{
        position - range_start, measures.measure(index).time_signature,
        measures.measure(index).key_signature});
  }
  if (destination_contexts.size() != fragment.measure_contexts().size())
    return false;
  for (std::size_t i = 0; i < destination_contexts.size(); ++i) {
    if (destination_contexts[i].position !=
            fragment.measure_contexts()[i].position ||
        destination_contexts[i].time_signature !=
            fragment.measure_contexts()[i].time_signature) {
      return false;
    }
  }
  return true;
}

// ---- clef application (interior changes only, contained) ----

std::optional<Clef> stave_default_clef(const Project& project, TrackId track_id,
                                       StaveId stave_id) {
  const Track* track = project.find_active_track(track_id);
  if (track == nullptr)
    return std::nullopt;
  for (const StaveDefinition& definition : track->layout().staves()) {
    if (definition.id == stave_id)
      return definition.default_clef;
  }
  return std::nullopt;
}

struct ClefBuildResult {
  Result           status;
  ClefPreSnapshot  pre;
  ClefPostSnapshot post;
};

// Builds the per-stave clef-lane edits this paste implies. The stave set
// iterated is `clef_staves` -- the union of every stave the paste's own
// music replaces (the same distinct (track_ordinal, stave_ordinal) set
// voice-part/pedal-span reconnection touches) and every stave
// fragment_.clef_changes() names, so a stave whose notes are wholly
// replaced but which the fragment names no clef change for still has its
// in-range destination clef changes removed ("replace, not interleave" is
// unqualified -- Finding 1), while a clef change naming an otherwise
// untouched stave is never dropped (Finding 7). Each stave's destination is
// resolved through `mapping` (the same mapping voice/pedal reconnection
// uses, never a second path); existing destination changes inside
// [anchor_position, range_end) are removed, that stave's fragment changes
// (if any) are added at anchor_position-relative offsets, and the pre-edit
// prevailing clef at range_end is re-asserted there if the edit would
// otherwise change it (skipped at node_end(), or if a destination change
// already sits exactly at range_end). A stave whose resulting lane is
// unchanged from its pre-edit state (no removal, no addition, no
// reassertion) contributes no pre/post entry at all -- an untouched stave
// gains neither a snapshot nor a newly created lane. Read-only against
// `timeline`/`project`; never mutates live state.
ClefBuildResult build_clef_edits(
    const Project& project, const NodeTimeline& timeline,
    const internal::PasteMapping& mapping, const NotationFragment& fragment,
    const std::vector<std::pair<std::size_t, std::size_t>>& clef_staves,
    Rational anchor_position, Rational range_end, Rational node_end) {
  ClefBuildResult                        result;
  const std::vector<FragmentClefChange>& changes = fragment.clef_changes();

  for (const auto& [track_ordinal, stave_ordinal] : clef_staves) {
    const std::optional<PasteScope> destination =
        mapping.scope(track_ordinal, stave_ordinal);
    if (!destination.has_value()) {
      result.status = Result(ResultCode::kInvalidArgument);
      return result;
    }
    const StaveId dest_stave = destination->stave;

    const ClefLane*         existing = timeline.clef_lane(dest_stave);
    std::optional<ClefLane> pre =
        existing != nullptr ? std::optional<ClefLane>(*existing) : std::nullopt;

    Clef default_clef = Clef::kTreble;
    if (existing != nullptr) {
      default_clef = existing->default_clef();
    } else {
      const std::optional<Clef> looked_up =
          stave_default_clef(project, destination->track, dest_stave);
      if (!looked_up.has_value()) {
        result.status = Result(ResultCode::kInvalidArgument);
        return result;
      }
      default_clef = *looked_up;
    }

    // Captured now, while `existing` is still known non-null or null --
    // `timeline` is never mutated by this function, so this pointer stays
    // valid and this value stays correct for the rest of the loop body.
    const Clef pre_edit_clef =
        existing != nullptr ? existing->clef_at(range_end) : default_clef;

    ClefLane candidate = pre.value_or(ClefLane(default_clef));

    std::vector<Rational> to_remove;
    for (const ClefChange& change : candidate.changes()) {
      if (!(change.position < anchor_position) && change.position < range_end)
        to_remove.push_back(change.position);
    }
    for (const Rational position : to_remove) {
      const Result remove_result = candidate.remove_change(position);
      if (!remove_result.ok()) {
        result.status = remove_result;
        return result;
      }
    }

    for (const FragmentClefChange& change : changes) {
      if (change.track_ordinal != track_ordinal ||
          change.stave_ordinal != stave_ordinal)
        continue;
      const Result add_result =
          candidate.add_change(anchor_position + change.position, change.clef);
      if (!add_result.ok()) {
        result.status = add_result;
        return result;
      }
    }

    if (range_end != node_end) {
      const bool has_change_at_range_end =
          std::any_of(candidate.changes().begin(), candidate.changes().end(),
                      [range_end](const ClefChange& change) {
                        return change.position == range_end;
                      });
      if (!has_change_at_range_end) {
        const Clef post_edit_clef = candidate.clef_at(range_end);
        if (post_edit_clef != pre_edit_clef) {
          const Result reassert_result =
              candidate.add_change(range_end, pre_edit_clef);
          if (!reassert_result.ok()) {
            result.status = reassert_result;
            return result;
          }
        }
      }
    }

    const bool differs = existing != nullptr ? (candidate != *existing)
                                             : !candidate.changes().empty();
    if (differs) {
      result.pre.emplace_back(dest_stave, std::move(pre));
      result.post.emplace_back(dest_stave, std::move(candidate));
    }
  }

  result.status = Result();
  return result;
}

// Publishes `post` onto `timeline`'s live clef lanes: creates a lane for
// every stave `pre` records as previously absent (NodeTimeline::
// create_clef_lane, which may allocate), then restores every other stave's
// lane in place (NodeTimeline::restore_clef_lane, which cannot fail once
// the lane exists with the stave's invariant default clef -- guaranteed
// here since `pre`/`post` were built from that same live lane). If any
// creation fails, every creation already performed by this call is rolled
// back (NodeTimeline::remove_clef_lane, which cannot fail). If a restore
// fails -- unreachable given the precondition above, but handled for real
// rather than merely documented as impossible -- every restore this call
// already performed is itself rolled back to its captured pre-edit lane
// (via restore_clef_lane, using the still-available `pre` copy) before any
// newly created lane is removed. Either way, a failure here leaves every
// clef lane -- indeed the whole timeline -- exactly as found.
[[nodiscard]] Result apply_clef_snapshot(NodeTimeline&          timeline,
                                         const ClefPreSnapshot& pre,
                                         ClefPostSnapshot       post) noexcept {
  if (pre.size() != post.size())
    return Result(ResultCode::kInternalError);

  try {
    std::vector<StaveId> created;
    created.reserve(post.size());

    for (std::size_t i = 0; i < post.size(); ++i) {
      if (pre[i].second.has_value())
        continue;
      Result create_result;
      try {
        create_result =
            timeline.create_clef_lane(post[i].first, std::move(post[i].second));
      } catch (const std::bad_alloc&) {
        create_result = Result(ResultCode::kOutOfMemory);
      } catch (const std::length_error&) {
        create_result = Result(ResultCode::kOutOfMemory);
      }
      if (!create_result.ok()) {
        for (const StaveId stave : created)
          timeline.remove_clef_lane(stave);
        return create_result;
      }
      created.push_back(post[i].first);
    }

    std::vector<std::size_t> restored;
    restored.reserve(post.size());
    for (std::size_t i = 0; i < post.size(); ++i) {
      if (!pre[i].second.has_value())
        continue;
      const Result restore_result =
          timeline.restore_clef_lane(post[i].first, std::move(post[i].second));
      if (!restore_result.ok()) {
        for (const std::size_t idx : restored) {
          const std::optional<ClefLane>& stave_pre = pre[idx].second;
          if (stave_pre.has_value()) {
            const Result revert =
                timeline.restore_clef_lane(post[idx].first, *stave_pre);
            static_cast<void>(revert);
          }
        }
        for (const StaveId stave : created)
          timeline.remove_clef_lane(stave);
        return restore_result;
      }
      restored.push_back(i);
    }
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

// The inverse of apply_clef_snapshot: restores `pre` (a stale-checked
// caller's responsibility) onto `timeline`'s live clef lanes -- removing a
// stave's lane if `pre` records it as previously absent, otherwise
// restoring it in place. Both operations are noexcept and cannot fail
// given the caller's stale-context precondition, so this never allocates
// and never fails in practice. Unlike apply_clef_snapshot, this performs
// no rollback on an early return: undoing a partial revert would need the
// post-edit lanes, which live in the caller's clef_post_snapshot_. If this
// precondition is ever weakened, this function must gain the same
// rollback apply_clef_snapshot has -- a partial revert would also poison
// the undo() stale check on retry, leaving the command permanently
// unretryable.
[[nodiscard]] Result revert_clef_snapshot(NodeTimeline&   timeline,
                                          ClefPreSnapshot pre) noexcept {
  for (auto& [stave, lane] : pre) {
    if (lane.has_value()) {
      const Result result = timeline.restore_clef_lane(stave, std::move(*lane));
      if (!result.ok())
        return result;
    } else {
      timeline.remove_clef_lane(stave);
    }
  }
  return Result();
}

bool clef_lane_equals(const NodeTimeline& timeline, StaveId stave,
                      const ClefLane& expected) {
  const ClefLane* lane = timeline.clef_lane(stave);
  return lane != nullptr && *lane == expected;
}

bool clef_lane_absent_or_equals(const NodeTimeline& timeline, StaveId stave,
                                const std::optional<ClefLane>& expected) {
  const ClefLane* lane = timeline.clef_lane(stave);
  if (!expected.has_value())
    return lane == nullptr;
  return lane != nullptr && *lane == *expected;
}

}  // namespace

Result PasteFragmentCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  if (anchor_.position < Rational(0))
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(anchor_.node);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end  = timeline->node_end();
  const Rational range_end = anchor_.position + fragment_.span_length();
  if (range_end > node_end)
    return Result(ResultCode::kInvalidArgument);

  std::optional<PasteCommitBundle> commit;
  ClefPreSnapshot                  clef_pre;
  ClefPostSnapshot                 clef_post_stored;
  try {
    const std::optional<internal::PasteMapping> mapping =
        internal::resolve_paste_mapping(project, fragment_, anchor_);
    if (!mapping.has_value())
      return Result(ResultCode::kInvalidArgument);

    if (!meter_compatible(fragment_, *timeline, anchor_.position, range_end))
      return Result(ResultCode::kInvalidArgument);

    // Touched destination tracks: the union of tracks referenced by
    // fragment voice parts AND by stave-scoped pedal spans.  A pedal span
    // that names a (track_ordinal, stave_ordinal) no voice part references
    // must still map to a destination track/stave (Defect 1 fix).
    std::vector<TrackId> touched;
    for (const FragmentVoicePart& part : fragment_.parts()) {
      const std::optional<PasteScope> destination =
          mapping->scope(part.track_ordinal, part.stave_ordinal);
      if (!destination.has_value())
        return Result(ResultCode::kInvalidArgument);
      const TrackId dest_track = destination->track;
      if (std::find(touched.begin(), touched.end(), dest_track) ==
          touched.end())
        touched.push_back(dest_track);
    }
    for (const FragmentPedalSpan& span : fragment_.pedal_spans()) {
      const std::optional<PasteScope> destination =
          mapping->scope(span.track_ordinal, span.stave_ordinal);
      if (!destination.has_value())
        return Result(ResultCode::kInvalidArgument);
      const TrackId dest_track = destination->track;
      if (std::find(touched.begin(), touched.end(), dest_track) ==
          touched.end())
        touched.push_back(dest_track);
    }

    // Capture pre-snapshot exactly — raw lane state, no normalization.
    LaneSnapshot pre_snapshot;
    LaneSnapshot candidates;
    pre_snapshot.reserve(touched.size());
    candidates.reserve(touched.size());
    for (const TrackId track_id : touched) {
      TrackLane* lane = node->lane(track_id);
      if (lane == nullptr)
        return Result(ResultCode::kInvalidArgument);
      pre_snapshot.emplace_back(track_id, *lane);
      candidates.emplace_back(track_id, *lane);
    }

    // ---- Phase 1: build all modified candidates (may allocate) ----

    for (const FragmentVoicePart& part : fragment_.parts()) {
      const std::optional<PasteScope> destination =
          mapping->scope(part.track_ordinal, part.stave_ordinal);
      if (!destination.has_value())
        return Result(ResultCode::kInvalidArgument);
      const TrackId dest_track = destination->track;
      const StaveId dest_stave = destination->stave;

      TrackLane* candidate = find_candidate(candidates, dest_track);
      if (candidate == nullptr)
        return Result(ResultCode::kInvalidArgument);

      candidate->ensure_stave(dest_stave);
      StaveVoices* stave = candidate->stave(dest_stave);
      if (stave == nullptr)
        return Result(ResultCode::kInvalidArgument);

      VoiceContent&                dest_voice = stave->voice(part.voice);
      internal::VoiceRebuildResult rebuild    = internal::rebuild_voice_range(
          dest_voice, anchor_.position, range_end, node_end, &part.content);
      if (!rebuild.status.ok())
        return rebuild.status;
      dest_voice = std::move(*rebuild.content);
    }

    // Apply pedal span changes: iterate every distinct (track_ordinal,
    // stave_ordinal) referenced by either voice parts or pedal spans.
    // A pedal span on a stave with no voice part gets clip+insert
    // treatment but no voice rebuild (there is no voice part to copy).
    std::vector<std::pair<std::size_t, std::size_t>> distinct_staves;
    for (const FragmentVoicePart& part : fragment_.parts()) {
      const auto key = std::make_pair(part.track_ordinal, part.stave_ordinal);
      if (std::find(distinct_staves.begin(), distinct_staves.end(), key) ==
          distinct_staves.end())
        distinct_staves.push_back(key);
    }
    for (const FragmentPedalSpan& span : fragment_.pedal_spans()) {
      const auto key = std::make_pair(span.track_ordinal, span.stave_ordinal);
      if (std::find(distinct_staves.begin(), distinct_staves.end(), key) ==
          distinct_staves.end())
        distinct_staves.push_back(key);
    }

    for (const auto& [track_ordinal, stave_ordinal] : distinct_staves) {
      const std::optional<PasteScope> destination =
          mapping->scope(track_ordinal, stave_ordinal);
      if (!destination.has_value())
        return Result(ResultCode::kInvalidArgument);
      const TrackId dest_track = destination->track;
      const StaveId dest_stave = destination->stave;

      TrackLane* candidate = find_candidate(candidates, dest_track);
      if (candidate == nullptr)
        return Result(ResultCode::kInvalidArgument);

      // ensure_stave required: add_pedal_span rejects staves not yet in
      // the lane's staves_ map (pedal-only stave fix).
      candidate->ensure_stave(dest_stave);

      Result clip_result = internal::clip_pedal_spans_in_range(
          *candidate, dest_stave, anchor_.position, range_end);
      if (!clip_result.ok())
        return clip_result;

      Result add_result = internal::add_offset_fragment_pedal_spans(
          *candidate, dest_stave, anchor_.position, fragment_, track_ordinal,
          stave_ordinal);
      if (!add_result.ok())
        return add_result;
    }

    // Clef-edit stave set (Finding 1): the union of `distinct_staves` above
    // (every stave the paste's own music replaces) and every stave
    // fragment_.clef_changes() names -- so a stave the paste overwrites but
    // the fragment names no clef change for still has its in-range
    // destination clef changes replaced, and a clef change naming an
    // otherwise-unreferenced stave is never silently dropped (Finding 7,
    // resolved together with resolve_paste_mapping's `used` set above).
    std::vector<std::pair<std::size_t, std::size_t>> clef_staves =
        distinct_staves;
    for (const FragmentClefChange& change : fragment_.clef_changes()) {
      const auto key =
          std::make_pair(change.track_ordinal, change.stave_ordinal);
      if (std::find(clef_staves.begin(), clef_staves.end(), key) ==
          clef_staves.end())
        clef_staves.push_back(key);
    }

    // Build the clef-lane edits this stave set implies, offline (read-only
    // against the current, unmutated timeline).
    ClefBuildResult clef_build =
        build_clef_edits(project, *timeline, *mapping, fragment_, clef_staves,
                         anchor_.position, range_end, node_end);
    if (!clef_build.status.ok())
      return clef_build.status;

    // ---- Phase 2: validate candidates (may allocate) ----
    // Only touched staves are present; untouched staves/voices may be
    // raw-empty — validate_clipboard_lane_candidate accepts that.

    for (auto& entry : candidates) {
      const Result validate_result =
          internal::validate_clipboard_lane_candidate(entry.second, node_end);
      if (!validate_result.ok())
        return validate_result;
    }

    // ---- Phase 3: prepare commit bundle (all allocations before publish)
    //
    // pre_snapshot_ holds the raw pre-state so undo restores exactly.
    // post_snapshot_ captures the fully built post-state for stale-context
    // checks on undo.  fragment_ is set by constructor; node context is
    // inferred from anchor_.  All member state is committed before any
    // live lane is touched, so a late allocation failure leaves the model
    // unchanged and the command in kFresh.
    //
    LaneSnapshot post_snapshot = candidates;
    clef_pre                   = clef_build.pre;
    clef_post_stored           = clef_build.post;

    // ---- Phase 4: pre-resolve live pointers (may allocate) ----
    //
    // All live TrackLane* pointers are resolved before the first mutation
    // so that between the first and last lane assignment only
    // dereference + noexcept move assignment remain.  TrackLane noexcept
    // is enforced at compile time by the static_assert in
    // clipboard_command_helpers.hpp.

    std::vector<TrackLane*> live_lanes;
    live_lanes.reserve(candidates.size());
    for (auto& entry : candidates) {
      TrackLane* live_lane = node->lane(entry.first);
      if (live_lane == nullptr)
        return Result(ResultCode::kInvalidArgument);
      live_lanes.push_back(live_lane);
    }

    NodeTimeline* mutable_timeline = node->timeline();
    if (mutable_timeline == nullptr)
      return Result(ResultCode::kInvalidArgument);

    // Publish every clef-lane edit now: this is the last step that can
    // fail (a new lane's map insertion may allocate). If it fails, no
    // TrackLane has been touched yet, and apply_clef_snapshot has already
    // rolled back any lane it created — the project is unmutated.
    const Result clef_result = apply_clef_snapshot(
        *mutable_timeline, clef_build.pre, std::move(clef_build.post));
    if (!clef_result.ok())
      return clef_result;

    commit.emplace(
        PasteCommitBundle{std::move(pre_snapshot), std::move(post_snapshot),
                          std::move(candidates), std::move(live_lanes)});
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }

  // Nothing below this boundary can fail. In particular, no catch handler is
  // reachable after the first command-member commit or live-lane mutation.
  // Clef lanes have already been published above (apply_clef_snapshot),
  // before this boundary, precisely because that step alone can still
  // fail; only trivial noexcept moves remain here.
  static_assert(std::is_nothrow_move_assignable_v<decltype(pre_snapshot_)>);
  static_assert(std::is_nothrow_move_assignable_v<decltype(post_snapshot_)>);
  static_assert(
      std::is_nothrow_move_assignable_v<decltype(clef_pre_snapshot_)>);
  static_assert(
      std::is_nothrow_move_assignable_v<decltype(clef_post_snapshot_)>);
  static_assert(std::is_nothrow_move_assignable_v<TrackLane>);
  static_assert(std::is_nothrow_assignable_v<decltype(state_)&, State>);

  pre_snapshot_       = std::move(commit->pre_snapshot);
  post_snapshot_      = std::move(commit->post_snapshot);
  clef_pre_snapshot_  = std::move(clef_pre);
  clef_post_snapshot_ = std::move(clef_post_stored);

  for (std::size_t i = 0; i < commit->live_lanes.size(); ++i)
    *commit->live_lanes[i] = std::move(commit->candidates[i].second);

  state_ = State::kDone;
  return Result();
}

Result PasteFragmentCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value() || !post_snapshot_.has_value() ||
      !clef_pre_snapshot_.has_value() || !clef_post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(anchor_.node);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  for (const auto& [stave, lane] : *clef_post_snapshot_) {
    if (!clef_lane_equals(*timeline, stave, lane))
      return Result(ResultCode::kInvalidArgument);
  }

  std::optional<internal::LaneRestoreBundle> bundle;
  ClefPreSnapshot                            clef_revert_copy;
  try {
    const Result prepare_result = internal::prepare_lane_restore(
        *pre_snapshot_, *post_snapshot_, anchor_.node, project, bundle);
    if (!bundle.has_value())
      return prepare_result;
    clef_revert_copy = *clef_pre_snapshot_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }

  // revert_clef_snapshot only removes/restores clef lanes proven present
  // by the stale-context check above, so it cannot fail in practice; it is
  // still sequenced before the TrackLane commit below so that even an
  // unreachable failure here leaves every TrackLane untouched.
  NodeTimeline* mutable_timeline = node->timeline();
  if (mutable_timeline == nullptr)
    return Result(ResultCode::kInternalError);
  const Result clef_result =
      revert_clef_snapshot(*mutable_timeline, std::move(clef_revert_copy));
  if (!clef_result.ok())
    return clef_result;

  // Nothing below this boundary can fail.
  internal::commit_lane_restore(*bundle);

  state_ = State::kUndone;
  return Result();
}

Result PasteFragmentCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value() || !post_snapshot_.has_value() ||
      !clef_pre_snapshot_.has_value() || !clef_post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(anchor_.node);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  for (const auto& [stave, lane] : *clef_pre_snapshot_) {
    if (!clef_lane_absent_or_equals(*timeline, stave, lane))
      return Result(ResultCode::kInvalidArgument);
  }

  std::optional<internal::LaneRestoreBundle> bundle;
  ClefPostSnapshot                           clef_apply_copy;
  try {
    const Result prepare_result = internal::prepare_lane_restore(
        *post_snapshot_, *pre_snapshot_, anchor_.node, project, bundle);
    if (!bundle.has_value())
      return prepare_result;
    clef_apply_copy = *clef_post_snapshot_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }

  NodeTimeline* mutable_timeline = node->timeline();
  if (mutable_timeline == nullptr)
    return Result(ResultCode::kInternalError);

  // The one step of redo() that can still fail: creating a clef lane on a
  // stave that had none before execute(). prepare_lane_restore has already
  // validated and built every TrackLane candidate offline, so if this
  // fails, nothing has been mutated yet; if it succeeds, everything below
  // is guaranteed to complete.
  const Result clef_result = apply_clef_snapshot(
      *mutable_timeline, *clef_pre_snapshot_, std::move(clef_apply_copy));
  if (!clef_result.ok())
    return clef_result;

  internal::commit_lane_restore(*bundle);

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
