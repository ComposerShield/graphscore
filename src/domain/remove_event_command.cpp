// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/remove_event_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_registry.hpp>
#include <graphscore/domain/graph.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result RemoveEventCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  const EventDefinition* def = project.events().find_by_id(event_id_);
  if (def == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<EventDefinition> def_snapshot;
  std::vector<BoundOutput>       bound_snapshot;
  std::vector<ListenerSnapshot>  listener_snapshot;
  try {
    def_snapshot = *def;
    for (const Node& node : project.nodes()) {
      for (const OutputConnector& output : node.outputs()) {
        if (output.event_binding() == event_id_)
          bound_snapshot.push_back(BoundOutput{node.id(), output.id()});
      }

      const EventListener* listener = node.find_listener(event_id_);
      if (listener != nullptr) {
        listener_snapshot.push_back(ListenerSnapshot{
            node.id(), listener->policy(), listener->capacity()});
      }
    }
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  Graph        graph(project);
  const Result result = graph.remove_event(event_id_);
  if (!result.ok())
    return result;

  removed_def_   = std::move(def_snapshot);
  bound_outputs_ = std::move(bound_snapshot);
  listeners_     = std::move(listener_snapshot);
  state_         = State::kDone;
  return Result();
}

Result RemoveEventCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!removed_def_.has_value())
    return Result(ResultCode::kInternalError);

  Result register_result;
  try {
    register_result = project.events().add_event_with_id(removed_def_->id,
                                                         removed_def_->name);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!register_result.ok())
    return register_result;

  for (const BoundOutput& bound : bound_outputs_) {
    Graph        graph(project);
    const Result bind_result =
        graph.bind_output_event(bound.node_id, bound.output_id, event_id_);
    if (!bind_result.ok())
      return bind_result;
  }

  for (const ListenerSnapshot& snapshot : listeners_) {
    Node* node = project.find_node(snapshot.node_id);
    if (node == nullptr)
      return Result(ResultCode::kInvalidArgument);

    const Result policy_result = node->set_listener_policy(
        event_id_, snapshot.policy, snapshot.capacity);
    if (!policy_result.ok())
      return policy_result;
  }

  state_ = State::kUndone;
  return Result();
}

Result RemoveEventCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result = graph.remove_event(event_id_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
