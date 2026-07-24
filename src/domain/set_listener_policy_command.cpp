// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_listener_policy_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetListenerPolicyCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const EventListener* listener = node->find_listener(event_);
  if (listener == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const QueuePolicy old_policy   = listener->policy();
  const std::size_t old_capacity = listener->capacity();
  const Result      result =
      node->set_listener_policy(event_, new_policy_, new_capacity_);
  if (!result.ok())
    return result;

  old_policy_   = old_policy;
  old_capacity_ = old_capacity;
  state_        = State::kDone;
  return Result();
}

Result SetListenerPolicyCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result result =
      node->set_listener_policy(event_, old_policy_, old_capacity_);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result SetListenerPolicyCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result result =
      node->set_listener_policy(event_, new_policy_, new_capacity_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
