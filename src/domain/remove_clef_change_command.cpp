// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/remove_clef_change_command.hpp>

#include <new>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include "timeline_command_helpers.hpp"

namespace graphscore {

Result RemoveClefChangeCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);
  NodeTimeline*   timeline = internal::resolve_node_timeline(project, node_id_);
  const ClefLane* lane =
      timeline == nullptr ? nullptr
                          : internal::resolve_clef_lane(*timeline, stave_id_);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    ClefLane     pre              = *lane;
    ClefLane     candidate        = pre;
    const Result candidate_result = candidate.remove_change(position_);
    if (!candidate_result.ok())
      return candidate_result;
    const Result result = timeline->remove_clef_change(stave_id_, position_);
    if (!result.ok())
      return result;
    pre_snapshot_  = std::move(pre);
    post_snapshot_ = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kDone;
  return Result();
}

Result RemoveClefChangeCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  const Result result = internal::restore_clef_lane_snapshot(
      project, node_id_, stave_id_, pre_snapshot_, post_snapshot_);
  if (!result.ok())
    return result;
  state_ = State::kUndone;
  return Result();
}

Result RemoveClefChangeCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  const Result result = internal::restore_clef_lane_snapshot(
      project, node_id_, stave_id_, post_snapshot_, pre_snapshot_);
  if (!result.ok())
    return result;
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
