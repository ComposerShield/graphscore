// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_project_dynamic_command.hpp>

#include <graphscore/core/dynamic.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetProjectDynamicCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  old_dynamic_ = project.default_dynamic();
  project.set_default_dynamic(new_dynamic_);
  state_ = State::kDone;
  return Result();
}

Result SetProjectDynamicCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  project.set_default_dynamic(old_dynamic_);
  state_ = State::kUndone;
  return Result();
}

Result SetProjectDynamicCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  project.set_default_dynamic(new_dynamic_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
