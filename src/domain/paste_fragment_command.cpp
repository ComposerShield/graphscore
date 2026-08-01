// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/paste_fragment_command.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_fragment.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/voice_content.hpp>
#include "clipboard_command_helpers.hpp"

namespace graphscore {

namespace {

using LaneSnapshot = std::vector<std::pair<TrackId, TrackLane>>;

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
  try {
    const std::optional<internal::PasteMapping> mapping =
        internal::resolve_paste_mapping(project, fragment_, anchor_);
    if (!mapping.has_value())
      return Result(ResultCode::kInvalidArgument);

    // Touched destination tracks: the union of tracks referenced by
    // fragment voice parts AND by stave-scoped pedal spans.  A pedal span
    // that names a (track_ordinal, stave_ordinal) no voice part references
    // must still map to a destination track/stave (Defect 1 fix).
    std::vector<TrackId> touched;
    for (const FragmentVoicePart& part : fragment_.parts()) {
      const TrackId dest_track = mapping->track_id(part.track_ordinal);
      if (std::find(touched.begin(), touched.end(), dest_track) ==
          touched.end())
        touched.push_back(dest_track);
    }
    for (const FragmentPedalSpan& span : fragment_.pedal_spans()) {
      const TrackId dest_track = mapping->track_id(span.track_ordinal);
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
      const TrackId dest_track = mapping->track_id(part.track_ordinal);
      const std::optional<StaveId> dest_stave =
          mapping->stave_id(part.track_ordinal, part.stave_ordinal);
      if (!dest_stave.has_value())
        return Result(ResultCode::kInvalidArgument);

      TrackLane* candidate = find_candidate(candidates, dest_track);
      if (candidate == nullptr)
        return Result(ResultCode::kInvalidArgument);

      candidate->ensure_stave(*dest_stave);
      StaveVoices* stave = candidate->stave(*dest_stave);
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
      const TrackId dest_track = mapping->track_id(track_ordinal);
      const std::optional<StaveId> dest_stave =
          mapping->stave_id(track_ordinal, stave_ordinal);
      if (!dest_stave.has_value())
        return Result(ResultCode::kInvalidArgument);

      TrackLane* candidate = find_candidate(candidates, dest_track);
      if (candidate == nullptr)
        return Result(ResultCode::kInvalidArgument);

      // ensure_stave required: add_pedal_span rejects staves not yet in
      // the lane's staves_ map (pedal-only stave fix).
      candidate->ensure_stave(*dest_stave);

      Result clip_result = internal::clip_pedal_spans_in_range(
          *candidate, *dest_stave, anchor_.position, range_end);
      if (!clip_result.ok())
        return clip_result;

      Result add_result = internal::add_offset_fragment_pedal_spans(
          *candidate, *dest_stave, anchor_.position, fragment_, track_ordinal,
          stave_ordinal);
      if (!add_result.ok())
        return add_result;
    }

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
  static_assert(std::is_nothrow_move_assignable_v<decltype(pre_snapshot_)>);
  static_assert(std::is_nothrow_move_assignable_v<decltype(post_snapshot_)>);
  static_assert(std::is_nothrow_move_assignable_v<TrackLane>);
  static_assert(std::is_nothrow_assignable_v<decltype(state_)&, State>);

  pre_snapshot_  = std::move(commit->pre_snapshot);
  post_snapshot_ = std::move(commit->post_snapshot);

  for (std::size_t i = 0; i < commit->live_lanes.size(); ++i)
    *commit->live_lanes[i] = std::move(commit->candidates[i].second);

  state_ = State::kDone;
  return Result();
}

Result PasteFragmentCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value() || !post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  const Result result = internal::multi_lane_restore_snapshot(
      *pre_snapshot_, *post_snapshot_, anchor_.node, project);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result PasteFragmentCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value() || !post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  const Result result = internal::multi_lane_restore_snapshot(
      *post_snapshot_, *pre_snapshot_, anchor_.node, project);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
