// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_tempo_point_command.hpp>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>
#include "tempo_command_helpers.hpp"

namespace graphscore {

Result AddTempoPointCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  NodeTimeline* timeline = internal::resolve_timeline(project, node_id_);
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    internal::TempoSnapshot pre = internal::read_tempo_points(*timeline);

    std::vector<TempoPoint> candidate = pre.value_or(std::vector<TempoPoint>{});

    const bool duplicate = std::any_of(
        candidate.begin(), candidate.end(), [this](const TempoPoint& existing) {
          return existing.position == point_.position;
        });
    if (duplicate)
      return Result(ResultCode::kInvalidArgument);

    const auto insert_at = std::lower_bound(
        candidate.begin(), candidate.end(), point_.position,
        [](const TempoPoint& existing, const Rational position) {
          return existing.position < position;
        });
    candidate.insert(insert_at, point_);

    const Result result = timeline->set_tempo(candidate);
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

Result AddTempoPointCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  const Result result = internal::tempo_restore_snapshot(
      pre_snapshot_, post_snapshot_, node_id_, project);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result AddTempoPointCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  const Result result = internal::tempo_restore_snapshot(
      post_snapshot_, pre_snapshot_, node_id_, project);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
