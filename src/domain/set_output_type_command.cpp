// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_output_type_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetOutputTypeCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const ConnectorType old_type = output->type();
  const Result        result   = node->set_output_type(output_id_, new_type_);
  if (!result.ok())
    return result;

  old_type_ = old_type;
  state_    = State::kDone;
  return Result();
}

Result SetOutputTypeCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result result = node->set_output_type(output_id_, old_type_);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result SetOutputTypeCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result result = node->set_output_type(output_id_, new_type_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
