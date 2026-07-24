// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/restore_track_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result RestoreTrackCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  const Result result = project.restore_track(track_id_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

Result RestoreTrackCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  const Result result = project.archive_track(track_id_);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result RestoreTrackCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  const Result result = project.restore_track(track_id_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
