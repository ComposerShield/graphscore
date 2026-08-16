// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_pedal_span_command.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/project.hpp>
#include "marking_command_helpers.hpp"

namespace graphscore {

Result AddPedalSpanCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  TrackLane* lane = node->lane(track_id_);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (!lane->has_stave(stave_id_))
    return Result(ResultCode::kInvalidArgument);

  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end = timeline->node_end();

  try {
    TrackLane pre_snapshot = *lane;
    TrackLane candidate    = pre_snapshot;

    Result result = candidate.add_pedal_span(stave_id_, span_);
    if (!result.ok())
      return result;

    Result vr = internal::validate_lane_candidate(candidate, node_end);
    if (!vr.ok())
      return vr;

    pre_snapshot_  = std::move(pre_snapshot);
    post_snapshot_ = candidate;
    *lane          = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kDone;
  return Result();
}

Result AddPedalSpanCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Result r = internal::lane_restore_snapshot(pre_snapshot_, post_snapshot_,
                                             node_id_, track_id_, project,
                                             &compensation_snapshot_);
  if (!r.ok())
    return r;

  state_ = State::kUndone;
  return Result();
}

Result AddPedalSpanCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Result r = internal::lane_restore_snapshot(post_snapshot_, pre_snapshot_,
                                             node_id_, track_id_, project,
                                             &compensation_snapshot_);
  if (!r.ok())
    return r;

  state_ = State::kDone;
  return Result();
}

Result AddPedalSpanCommand::compensate_undo(Project& project) noexcept {
  if (state_ != State::kUndone || !compensation_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);

  Node*      node = project.find_node(node_id_);
  TrackLane* lane = node == nullptr ? nullptr : node->lane(track_id_);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);

  *lane = std::move(*compensation_snapshot_);
  compensation_snapshot_.reset();
  state_ = State::kDone;
  return Result();
}

Result AddPedalSpanCommand::compensate_redo(Project& project) noexcept {
  if (state_ != State::kDone || !compensation_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);

  Node*      node = project.find_node(node_id_);
  TrackLane* lane = node == nullptr ? nullptr : node->lane(track_id_);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);

  *lane = std::move(*compensation_snapshot_);
  compensation_snapshot_.reset();
  state_ = State::kUndone;
  return Result();
}

}  // namespace graphscore
