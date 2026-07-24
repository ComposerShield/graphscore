// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_output_connector_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result AddOutputConnectorCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  ConnectorId id;
  try {
    id = node->add_output(name_, type_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const OutputConnector* added = node->find_output(id);
  if (added == nullptr)
    return Result(ResultCode::kInternalError);

  std::optional<OutputConnector> snapshot;
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

Result AddOutputConnectorCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result result = node->remove_output(created_->id());
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result AddOutputConnectorCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<OutputConnector> prepared;
  try {
    prepared = *created_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  Result result;
  try {
    result = node->restore_output(std::move(*prepared), std::nullopt);
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
