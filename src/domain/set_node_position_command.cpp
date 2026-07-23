// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_node_position_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetNodePositionCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_position_ = node->position();
  node->set_position(new_position_);
  state_ = State::kDone;
  return Result();
}

Result SetNodePositionCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  node->set_position(old_position_);
  state_ = State::kUndone;
  return Result();
}

Result SetNodePositionCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  node->set_position(new_position_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
