// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_output_priority_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetOutputPriorityCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_priority_ = output->priority();
  output->set_priority(new_priority_);
  state_ = State::kDone;
  return Result();
}

Result SetOutputPriorityCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  output->set_priority(old_priority_);
  state_ = State::kUndone;
  return Result();
}

Result SetOutputPriorityCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  output->set_priority(new_priority_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
