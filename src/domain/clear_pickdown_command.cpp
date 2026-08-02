// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/clear_pickdown_command.hpp>

#include <new>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include "timeline_command_helpers.hpp"

namespace graphscore {

Result ClearPickdownCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);
  NodeTimeline* timeline = internal::resolve_node_timeline(project, node_id_);
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    std::optional<internal::PickdownTempoSnapshots> tempo =
        internal::prepare_pickdown_tempo_snapshots(
            *timeline, timeline->boundary_position());
    if (!tempo.has_value())
      return Result(ResultCode::kInvalidArgument);
    const std::optional<Rational> pre    = timeline->pickdown_duration();
    const Result                  result = timeline->clear_pickdown();
    if (!result.ok())
      return result;
    pre_snapshot_        = pre;
    post_snapshot_       = std::nullopt;
    pre_tempo_snapshot_  = std::move(tempo->pre);
    post_tempo_snapshot_ = std::move(tempo->post);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kDone;
  return Result();
}

Result ClearPickdownCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  const Result result = internal::restore_pickdown_snapshot(
      project, node_id_, pre_snapshot_, post_snapshot_, post_tempo_snapshot_);
  if (!result.ok())
    return result;
  state_ = State::kUndone;
  return Result();
}

Result ClearPickdownCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  const Result result = internal::restore_pickdown_snapshot(
      project, node_id_, post_snapshot_, pre_snapshot_, pre_tempo_snapshot_);
  if (!result.ok())
    return result;
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
