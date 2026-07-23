// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_project_tempo_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/core/tempo.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetProjectTempoCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  old_tempo_ = project.default_tempo();
  project.set_default_tempo(new_tempo_);
  state_ = State::kDone;
  return Result();
}

Result SetProjectTempoCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  project.set_default_tempo(old_tempo_);
  state_ = State::kUndone;
  return Result();
}

Result SetProjectTempoCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  project.set_default_tempo(new_tempo_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
