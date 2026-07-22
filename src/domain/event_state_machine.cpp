// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/event_state_machine.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_occurrence.hpp>
#include <graphscore/domain/vertical_transition.hpp>
#include <graphscore/domain/weighted_selection.hpp>

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

std::vector<ArbitrationCandidate> assemble_vertical_candidates(
    const Project& project, const Node& active_node,
    TimelineRegion active_region, std::span<const EventId> fired_events) {
  std::vector<ArbitrationCandidate> candidates;
  if (active_region != TimelineRegion::kMain)
    return candidates;

  const NodeTimeline* source_timeline = active_node.timeline();
  if (source_timeline == nullptr)
    return candidates;

  const std::vector<OutputConnector>& outputs = active_node.outputs();
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    const OutputConnector& output = outputs[index];
    if (output.type() != ConnectorType::kVertical)
      continue;
    if (!output.destination().has_value())
      continue;
    if (!output.event_binding().has_value())
      continue;

    bool fired = false;
    for (const EventId& event : fired_events) {
      if (event == *output.event_binding()) {
        fired = true;
        break;
      }
    }
    if (!fired)
      continue;

    const Node* destination_node =
        project.find_node(output.destination()->node);
    if (destination_node == nullptr)
      continue;
    const NodeTimeline* destination_timeline = destination_node->timeline();
    if (destination_timeline == nullptr)
      continue;
    if (!vertical_regions_compatible(*source_timeline, *destination_timeline))
      continue;

    candidates.push_back(ArbitrationCandidate{
        output.id(), index, output.priority(), /*arrival_ordinal=*/0});
  }
  return candidates;
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

Result EventStateMachine::queue_manual_transition(NodeId      active_node,
                                                  const Node& node,
                                                  ConnectorId output_id) {
  if (node.id() != active_node)
    return Result(ResultCode::kInvalidArgument);

  const OutputConnector* output = node.find_output(output_id);
  if (output == nullptr || output->type() != ConnectorType::kSequential ||
      !output->destination().has_value())
    return Result(ResultCode::kInvalidArgument);

  manual_queue_[node.id()] = output_id;
  return Result();
}

std::optional<ConnectorId> EventStateMachine::find_manual_queue(
    NodeId node) const {
  const auto it = manual_queue_.find(node);
  return it == manual_queue_.end() ? std::nullopt
                                   : std::optional<ConnectorId>(it->second);
}

SequentialTransitionResult EventStateMachine::resolve_sequential_transition(
    const Node& node) {
  const BoundaryResolution tier1 = resolve_sequential_boundary(node);
  if (tier1.outcome != BoundaryOutcome::kNoEventIntent) {
    manual_queue_.erase(node.id());
    if (tier1.outcome == BoundaryOutcome::kEventWinner)
      return SequentialTransitionResult{tier1.winner,
                                        SequentialTransitionTier::kEventIntent};
    return SequentialTransitionResult{std::nullopt, std::nullopt};
  }

  const auto manual_it = manual_queue_.find(node.id());
  if (manual_it != manual_queue_.end()) {
    const ConnectorId queued = manual_it->second;
    manual_queue_.erase(manual_it);
    const OutputConnector* output = node.find_output(queued);
    if (output != nullptr && output->type() == ConnectorType::kSequential &&
        output->destination().has_value()) {
      return SequentialTransitionResult{queued,
                                        SequentialTransitionTier::kManualQueue};
    }
    // Stale (removed/retyped/disconnected since queued): fall through to
    // tier 3 as though nothing had ever been queued.
  }

  std::vector<WeightedCandidate> group;
  for (const OutputConnector& output : node.outputs()) {
    if (output.type() == ConnectorType::kSequential &&
        output.destination().has_value()) {
      group.push_back(WeightedCandidate{output.id(), output.weight()});
    }
  }

  const std::optional<std::uint64_t> roll_bound =
      weight_group_roll_bound(group);
  if (!roll_bound.has_value())
    return SequentialTransitionResult{std::nullopt, std::nullopt};

  const std::uint64_t              roll   = prng_.next_below(*roll_bound);
  const std::optional<ConnectorId> winner = select_weighted_random(group, roll);
  if (!winner.has_value())
    return SequentialTransitionResult{std::nullopt, std::nullopt};

  return SequentialTransitionResult{winner,
                                    SequentialTransitionTier::kWeightedRandom};
}

std::optional<ConnectorId> EventStateMachine::resolve_vertical_transition(
    TransportInstant                         transport_instant,
    const std::vector<ArbitrationCandidate>& candidates) {
  if (last_vertical_jump_instant_.has_value() &&
      transport_instant <= *last_vertical_jump_instant_)
    return std::nullopt;

  const std::optional<ConnectorId> winner = resolve_vertical_match(candidates);
  if (winner.has_value())
    last_vertical_jump_instant_ = transport_instant;
  return winner;
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
  manual_queue_.erase(node);
}

void EventStateMachine::clear_all() noexcept {
  for (auto& entry : queues_) {
    entry.second.clear();
  }
  manual_queue_.clear();
  last_vertical_jump_instant_.reset();
}

void EventStateMachine::clear_event(NodeId node, EventId event) noexcept {
  const auto it = queues_.find(Key{node, event});
  if (it != queues_.end())
    it->second.clear();
}

}  // namespace graphscore
