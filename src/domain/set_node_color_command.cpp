// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_node_color_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetNodeColorCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_color_ = node->color();
  node->set_color(new_color_);
  state_ = State::kDone;
  return Result();
}

Result SetNodeColorCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  node->set_color(old_color_);
  state_ = State::kUndone;
  return Result();
}

Result SetNodeColorCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  node->set_color(new_color_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
