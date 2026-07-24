// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_input_connector_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/graph.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result AddInputConnectorCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  ConnectorId id;
  try {
    id = node->add_input(name_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const InputConnector* added = node->find_input(id);
  if (added == nullptr)
    return Result(ResultCode::kInternalError);

  std::optional<InputConnector> snapshot;
  try {
    snapshot = *added;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  created_ = std::move(snapshot);
  state_   = State::kDone;
  return Result();
}

Result AddInputConnectorCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_.has_value())
    return Result(ResultCode::kInternalError);

  Graph        graph(project);
  const Result result = graph.remove_input(node_id_, created_->id());
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result AddInputConnectorCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<InputConnector> prepared;
  try {
    prepared = *created_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  Result result;
  try {
    result = node->restore_input(std::move(*prepared));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
