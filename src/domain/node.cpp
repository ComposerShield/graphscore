// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/node.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace graphscore {

Node::Node(NodeId id, std::string name) : id_(id), name_(std::move(name)) {}

bool Node::has_lane(TrackId track_id) const {
  return lanes_.contains(track_id);
}

TrackLane* Node::lane(TrackId track_id) {
  const auto it = lanes_.find(track_id);
  return it == lanes_.end() ? nullptr : &it->second;
}

const TrackLane* Node::lane(TrackId track_id) const {
  const auto it = lanes_.find(track_id);
  return it == lanes_.end() ? nullptr : &it->second;
}

void Node::ensure_lane(TrackId track_id) {
  lanes_.try_emplace(track_id);
}

ConnectorId Node::add_input(std::string name) {
  inputs_.emplace_back(std::move(name));
  return inputs_.back().id();
}

ConnectorId Node::add_output(std::string name, ConnectorType type) {
  outputs_.emplace_back(std::move(name), type);
  return outputs_.back().id();
}

Result Node::remove_input(ConnectorId id) {
  const auto it = std::find_if(
      inputs_.begin(), inputs_.end(),
      [id](const InputConnector& input) { return input.id() == id; });
  if (it == inputs_.end())
    return Result(ResultCode::kInvalidArgument);

  inputs_.erase(it);
  return Result();
}

Result Node::remove_output(ConnectorId id) {
  const auto it = std::find_if(
      outputs_.begin(), outputs_.end(),
      [id](const OutputConnector& output) { return output.id() == id; });
  if (it == outputs_.end())
    return Result(ResultCode::kInvalidArgument);

  const std::optional<EventId> event = it->event_binding();
  outputs_.erase(it);
  if (event.has_value())
    cleanup_listener_if_unused(*event);
  return Result();
}

InputConnector* Node::find_input(ConnectorId id) {
  for (InputConnector& input : inputs_) {
    if (input.id() == id)
      return &input;
  }
  return nullptr;
}

const InputConnector* Node::find_input(ConnectorId id) const {
  for (const InputConnector& input : inputs_) {
    if (input.id() == id)
      return &input;
  }
  return nullptr;
}

OutputConnector* Node::find_output(ConnectorId id) {
  for (OutputConnector& output : outputs_) {
    if (output.id() == id)
      return &output;
  }
  return nullptr;
}

const OutputConnector* Node::find_output(ConnectorId id) const {
  for (const OutputConnector& output : outputs_) {
    if (output.id() == id)
      return &output;
  }
  return nullptr;
}

void Node::clear_destinations_to(NodeId node, ConnectorId connector) noexcept {
  for (OutputConnector& output : outputs_) {
    const auto& destination = output.destination();
    if (destination.has_value() && destination->node == node &&
        destination->connector == connector) {
      output.set_destination(std::nullopt);
    }
  }
}

Result Node::bind_output_event(ConnectorId            output_id,
                               std::optional<EventId> event) {
  OutputConnector* output = find_output(output_id);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const std::optional<EventId> previous = output->event_binding();
  if (previous == event)
    return Result();

  if (event.has_value()) {
    for (const OutputConnector& other : outputs_) {
      if (other.id() == output_id)
        continue;
      if (other.event_binding() == event && other.type() != output->type())
        return Result(ResultCode::kInvalidArgument);
    }
  }

  output->set_event_binding(event);

  if (previous.has_value())
    cleanup_listener_if_unused(*previous);

  if (event.has_value())
    listeners_.try_emplace(*event, output->type());

  return Result();
}

Result Node::set_output_type(ConnectorId output_id, ConnectorType type) {
  OutputConnector* output = find_output(output_id);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (output->type() == type)
    return Result();

  const std::optional<EventId> event = output->event_binding();
  if (event.has_value()) {
    for (const OutputConnector& other : outputs_) {
      if (other.id() == output_id)
        continue;
      if (other.event_binding() == event && other.type() != type)
        return Result(ResultCode::kInvalidArgument);
    }
  }

  output->set_type(type);

  if (event.has_value()) {
    const auto it = listeners_.find(*event);
    if (it != listeners_.end())
      it->second.set_bound_type(type);
  }

  return Result();
}

const EventListener* Node::find_listener(EventId event) const {
  const auto it = listeners_.find(event);
  return it == listeners_.end() ? nullptr : &it->second;
}

Result Node::set_listener_policy(EventId event, QueuePolicy policy,
                                 std::size_t capacity) {
  if (policy == QueuePolicy::kFifo && capacity == 0)
    return Result(ResultCode::kInvalidArgument);

  const auto it = listeners_.find(event);
  if (it == listeners_.end())
    return Result(ResultCode::kInvalidArgument);

  it->second.set_policy(policy);
  it->second.set_capacity(capacity);
  return Result();
}

void Node::cleanup_listener_if_unused(EventId event) {
  for (const OutputConnector& output : outputs_) {
    if (output.event_binding() == event)
      return;
  }
  listeners_.erase(event);
}

}  // namespace graphscore
