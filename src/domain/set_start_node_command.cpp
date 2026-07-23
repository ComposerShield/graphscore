// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_start_node_command.hpp>

#include <optional>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetStartNodeCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  // Snapshot the prior value into a local before attempting the mutation,
  // so that a failed set_start_node does not leave a stale snapshot in
  // old_start_node_ and does not change the command's logical-fresh state.
  const std::optional<NodeId> prior = project.start_node();

  if (new_start_node_.has_value()) {
    Result result = project.set_start_node(*new_start_node_);
    if (!result.ok())
      return result;
  } else {
    project.clear_start_node();
  }

  // Only now store the snapshot and transition to kDone.
  old_start_node_ = prior;
  state_          = State::kDone;
  return Result();
}

Result SetStartNodeCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  if (old_start_node_.has_value()) {
    Result result = project.set_start_node(*old_start_node_);
    if (!result.ok())
      return result;
  } else {
    project.clear_start_node();
  }

  state_ = State::kUndone;
  return Result();
}

Result SetStartNodeCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  if (new_start_node_.has_value()) {
    Result result = project.set_start_node(*new_start_node_);
    if (!result.ok())
      return result;
  } else {
    project.clear_start_node();
  }

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
