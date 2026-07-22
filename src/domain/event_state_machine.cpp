// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/event_state_machine.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_occurrence.hpp>

namespace graphscore {

std::optional<ConnectorId> select_winner(
    const std::vector<ArbitrationCandidate>& candidates) {
  if (candidates.empty())
    return std::nullopt;

  const ArbitrationCandidate* best = &candidates.front();
  for (const ArbitrationCandidate& candidate : candidates) {
    if (candidate.priority != best->priority) {
      if (candidate.priority > best->priority)
        best = &candidate;
      continue;
    }
    if (candidate.arrival_ordinal != best->arrival_ordinal) {
      if (candidate.arrival_ordinal > best->arrival_ordinal)
        best = &candidate;
      continue;
    }
    if (candidate.connector_order < best->connector_order)
      best = &candidate;
  }
  return best->connector;
}

std::optional<ConnectorId> resolve_vertical_match(
    const std::vector<ArbitrationCandidate>& candidates) {
  return select_winner(candidates);
}

Result EventStateMachine::record_sequential_occurrence(
    const Node& node, EventId event, std::int64_t sample_offset) {
  const EventListener* listener = node.find_listener(event);
  if (listener == nullptr ||
      listener->bound_type() != ConnectorType::kSequential)
    return Result(ResultCode::kInvalidArgument);

  const std::optional<EventOccurrence> occurrence =
      EventOccurrence::create(event, next_arrival_ordinal_, sample_offset);
  if (!occurrence.has_value())
    return Result(ResultCode::kInvalidArgument);
  ++next_arrival_ordinal_;

  EventQueue& queue = queues_[Key{node.id(), event}];
  queue.record(listener->policy(), *occurrence, listener->capacity());
  return Result();
}

BoundaryResolution EventStateMachine::resolve_sequential_boundary(
    const Node& node) {
  const std::vector<OutputConnector>& outputs = node.outputs();

  bool has_sequential_output = false;
  for (const OutputConnector& output : outputs) {
    if (output.type() == ConnectorType::kSequential &&
        output.destination().has_value()) {
      has_sequential_output = true;
      break;
    }
  }
  if (!has_sequential_output)
    return BoundaryResolution{BoundaryOutcome::kStopPlayback, std::nullopt};

  std::vector<ArbitrationCandidate> candidates;
  std::vector<std::pair<ConnectorId, decltype(queues_)::iterator>>
      candidate_queues;
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    const OutputConnector& output = outputs[index];
    if (output.type() != ConnectorType::kSequential)
      continue;
    if (!output.destination().has_value())
      continue;
    if (!output.event_binding().has_value())
      continue;

    const auto it = queues_.find(Key{node.id(), *output.event_binding()});
    if (it == queues_.end())
      continue;

    const std::optional<EventOccurrence> occurrence = it->second.peek();
    if (!occurrence.has_value())
      continue;

    candidates.push_back(ArbitrationCandidate{
        output.id(), index, output.priority(), occurrence->arrival_ordinal()});
    candidate_queues.emplace_back(output.id(), it);
  }

  const std::optional<ConnectorId> winner = select_winner(candidates);
  if (!winner.has_value())
    return BoundaryResolution{BoundaryOutcome::kNoEventIntent, std::nullopt};

  for (auto& [connector, queue_it] : candidate_queues) {
    if (connector == *winner) {
      queue_it->second.consume();
      break;
    }
  }

  return BoundaryResolution{BoundaryOutcome::kEventWinner, winner};
}

const EventQueue* EventStateMachine::find_queue(NodeId  node,
                                                EventId event) const {
  const auto it = queues_.find(Key{node, event});
  return it == queues_.end() ? nullptr : &it->second;
}

void EventStateMachine::clear_node(NodeId node) noexcept {
  for (auto& [key, queue] : queues_) {
    if (key.node == node)
      queue.clear();
  }
}

void EventStateMachine::clear_all() noexcept {
  for (auto& entry : queues_) {
    entry.second.clear();
  }
}

void EventStateMachine::clear_event(NodeId node, EventId event) noexcept {
  const auto it = queues_.find(Key{node, event});
  if (it != queues_.end())
    it->second.clear();
}

}  // namespace graphscore
