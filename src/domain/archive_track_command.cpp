// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/archive_track_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result ArchiveTrackCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  const Result result = project.archive_track(track_id_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

Result ArchiveTrackCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  const Result result = project.restore_track(track_id_);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result ArchiveTrackCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  const Result result = project.archive_track(track_id_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
