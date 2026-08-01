// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/cut_fragment_command.hpp>

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
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/voice_content.hpp>
#include "clipboard_command_helpers.hpp"

namespace graphscore {

namespace {

using LaneSnapshot = std::vector<std::pair<TrackId, TrackLane>>;

struct CutCommitBundle {
  std::optional<NotationFragment> fragment;
  std::optional<NodeId>           node_id;
  LaneSnapshot                    pre_snapshot;
  LaneSnapshot                    post_snapshot;
  LaneSnapshot                    candidates;
  std::vector<TrackLane*>         live_lanes;
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

Result CutFragmentCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  std::optional<CutCommitBundle> commit;
  try {
    // ---- Phase 1: extract fragment + resolve target (may allocate) ----
    // These happen first; on failure the model is unchanged.

    FragmentExtraction extraction = extract_fragment(project, selection_);
    if (!extraction.status.ok())
      return extraction.status;

    const std::optional<internal::CutTarget> target =
        internal::resolve_cut_target(project, selection_);
    if (!target.has_value())
      return Result(ResultCode::kInvalidArgument);

    Node* node = project.find_node(target->node);
    if (node == nullptr)
      return Result(ResultCode::kInvalidArgument);
    const NodeTimeline* timeline = node->timeline();
    if (timeline == nullptr)
      return Result(ResultCode::kInvalidArgument);
    const Rational node_end = timeline->node_end();

    std::vector<TrackId> touched;
    for (const internal::AffectedVoice& voice : target->voices) {
      if (std::find(touched.begin(), touched.end(), voice.track) ==
          touched.end())
        touched.push_back(voice.track);
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

    // ---- Phase 2: build all modified candidates (may allocate) ----

    for (const internal::AffectedVoice& voice : target->voices) {
      TrackLane* candidate = find_candidate(candidates, voice.track);
      if (candidate == nullptr)
        return Result(ResultCode::kInvalidArgument);
      StaveVoices* stave = candidate->stave(voice.stave);
      if (stave == nullptr)
        return Result(ResultCode::kInvalidArgument);

      VoiceContent&                dest_voice = stave->voice(voice.voice);
      internal::VoiceRebuildResult rebuild    = internal::rebuild_voice_range(
          dest_voice, target->start, target->end, node_end, nullptr);
      if (!rebuild.status.ok())
        return rebuild.status;
      dest_voice = std::move(*rebuild.content);
    }

    // Apply pedal span changes.
    std::vector<std::pair<TrackId, StaveId>> distinct_staves;
    for (const internal::AffectedVoice& voice : target->voices) {
      const auto key = std::make_pair(voice.track, voice.stave);
      if (std::find(distinct_staves.begin(), distinct_staves.end(), key) ==
          distinct_staves.end())
        distinct_staves.push_back(key);
    }

    for (const auto& [track_id, stave_id] : distinct_staves) {
      TrackLane* candidate = find_candidate(candidates, track_id);
      if (candidate == nullptr)
        return Result(ResultCode::kInvalidArgument);
      const Result clip_result = internal::clip_pedal_spans_in_range(
          *candidate, stave_id, target->start, target->end);
      if (!clip_result.ok())
        return clip_result;
    }

    // ---- Phase 3: validate candidates (may allocate) ----

    for (auto& entry : candidates) {
      const Result validate_result =
          internal::validate_clipboard_lane_candidate(entry.second, node_end);
      if (!validate_result.ok())
        return validate_result;
    }

    // ---- Phase 4: prepare commit bundle (all allocations before publish)
    //
    // Every potentially-throwing allocation must complete before any member
    // variable is committed, so a late allocation failure leaves the model
    // unchanged and the command in kFresh with fragment() disengaged.
    //
    LaneSnapshot post_snapshot = candidates;

    // ---- Phase 5: pre-resolve live pointers (may allocate) ----
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

    commit.emplace(CutCommitBundle{
        std::move(extraction.fragment), std::optional<NodeId>(target->node),
        std::move(pre_snapshot), std::move(post_snapshot),
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
  static_assert(std::is_nothrow_move_assignable_v<decltype(fragment_)>);
  static_assert(std::is_nothrow_copy_assignable_v<decltype(node_id_)>);
  static_assert(std::is_nothrow_move_assignable_v<TrackLane>);
  static_assert(std::is_nothrow_assignable_v<decltype(state_)&, State>);

  pre_snapshot_  = std::move(commit->pre_snapshot);
  post_snapshot_ = std::move(commit->post_snapshot);
  fragment_      = std::move(commit->fragment);
  node_id_       = commit->node_id;

  for (std::size_t i = 0; i < commit->live_lanes.size(); ++i)
    *commit->live_lanes[i] = std::move(commit->candidates[i].second);

  state_ = State::kDone;
  return Result();
}

Result CutFragmentCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value() || !post_snapshot_.has_value() ||
      !node_id_.has_value())
    return Result(ResultCode::kInternalError);

  const Result result = internal::multi_lane_restore_snapshot(
      *pre_snapshot_, *post_snapshot_, *node_id_, project);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result CutFragmentCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value() || !post_snapshot_.has_value() ||
      !node_id_.has_value())
    return Result(ResultCode::kInternalError);

  const Result result = internal::multi_lane_restore_snapshot(
      *post_snapshot_, *pre_snapshot_, *node_id_, project);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
