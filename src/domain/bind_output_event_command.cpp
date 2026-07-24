// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/bind_output_event_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/graph.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result BindOutputEventCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const std::optional<EventId> old_event        = output->event_binding();
  bool                         listener_existed = false;
  QueuePolicy                  policy           = QueuePolicy::kLatestValidWins;
  std::size_t                  capacity         = 0;
  if (old_event.has_value()) {
    const EventListener* listener = node->find_listener(*old_event);
    listener_existed              = listener != nullptr;
    if (listener != nullptr) {
      policy   = listener->policy();
      capacity = listener->capacity();
    }
  }

  Graph        graph(project);
  const Result result =
      graph.bind_output_event(node_id_, output_id_, new_event_);
  if (!result.ok())
    return result;

  old_event_            = old_event;
  old_listener_existed_ = listener_existed;
  old_policy_           = policy;
  old_capacity_         = capacity;
  state_                = State::kDone;
  return Result();
}

Result BindOutputEventCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result =
      graph.bind_output_event(node_id_, output_id_, old_event_);
  if (!result.ok())
    return result;

  if (old_event_.has_value() && old_listener_existed_) {
    Node* node = project.find_node(node_id_);
    if (node == nullptr)
      return Result(ResultCode::kInvalidArgument);

    const Result restore_result =
        node->set_listener_policy(*old_event_, old_policy_, old_capacity_);
    if (!restore_result.ok())
      return restore_result;
  }

  state_ = State::kUndone;
  return Result();
}

Result BindOutputEventCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result =
      graph.bind_output_event(node_id_, output_id_, new_event_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
