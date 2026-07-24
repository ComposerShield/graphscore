// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/remove_output_connector_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result RemoveOutputConnectorCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<OutputConnector> snapshot;
  std::optional<EventListener>   listener_snapshot;
  try {
    snapshot                           = *output;
    const std::optional<EventId> event = output->event_binding();
    if (event.has_value()) {
      const EventListener* listener = node->find_listener(*event);
      if (listener != nullptr)
        listener_snapshot = *listener;
    }
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const Result result = node->remove_output(output_id_);
  if (!result.ok())
    return result;

  removed_        = std::move(snapshot);
  saved_listener_ = listener_snapshot;
  state_          = State::kDone;
  return Result();
}

Result RemoveOutputConnectorCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!removed_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<OutputConnector> prepared;
  try {
    prepared = *removed_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  Result result;
  try {
    result = node->restore_output(std::move(*prepared), saved_listener_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result RemoveOutputConnectorCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!removed_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result result = node->remove_output(removed_->id());
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
