// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_clef_change_command.hpp>

#include <new>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include "timeline_command_helpers.hpp"

namespace graphscore {

Result AddClefChangeCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);
  NodeTimeline* timeline = internal::resolve_node_timeline(project, node_id_);
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const ClefLane* lane = internal::resolve_clef_lane(*timeline, stave_id_);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    ClefLane     pre              = *lane;
    ClefLane     candidate        = pre;
    const Result candidate_result = candidate.add_change(position_, clef_);
    if (!candidate_result.ok())
      return candidate_result;
    const Result result =
        timeline->add_clef_change(stave_id_, position_, clef_);
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

Result AddClefChangeCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  const Result result = internal::restore_clef_lane_snapshot(
      project, node_id_, stave_id_, pre_snapshot_, post_snapshot_);
  if (!result.ok())
    return result;
  state_ = State::kUndone;
  return Result();
}

Result AddClefChangeCommand::redo(Project& project) noexcept {
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
