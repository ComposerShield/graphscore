// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include <graphscore/domain/connector.hpp>

namespace graphscore {

// How a node/event listener interprets its matching persisted occurrences
// when a sequential transition needs one (product decision,
// docs/plan/README.md). kLatestValidWins is the default.
//
// The runtime event state machine that interprets this policy -- bounded
// occurrence storage, arrival sequence, first/latest/FIFO arbitration,
// cross-event connector-priority arbitration, consumption/discard, pause
// behavior, and clearing on stop/reset/node-play -- lives in EventQueue
// and EventStateMachine (event_queue.hpp, event_state_machine.hpp). A full
// kFifo listener drops its oldest occurrence and increments a diagnostic
// counter there (EventQueue::dropped_count()).
enum class QueuePolicy : std::uint8_t {
  kFirstWins = 0,
  kLatestValidWins,
  kFifo,
};

// The configuration a node shares across every one of its own connectors
// bound to the same event (product decision: queue policy/capacity
// "belongs to the node/event listener shared by matching outputs", i.e.
// stored once per (node, event) pair, never per connector). A node cannot
// hold both a kSequential and a kVertical listener for the same event --
// see Node::bind_output_event (node.hpp), which enforces that rejection --
// so `bound_type` alone identifies which kind currently owns this event on
// the node.
class EventListener {
 public:
  explicit EventListener(ConnectorType bound_type) noexcept
      : bound_type_(bound_type) {}

  [[nodiscard]] ConnectorType bound_type() const noexcept {
    return bound_type_;
  }

  [[nodiscard]] QueuePolicy policy() const noexcept { return policy_; }

  void set_policy(QueuePolicy policy) noexcept { policy_ = policy; }

  // Bounded occurrence-storage capacity used by the kFifo interpretation
  // (Phase 5b); meaningful only when policy() == kFifo.
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  [[nodiscard]] bool operator==(const EventListener&) const = default;

 private:
  friend class Node;

  // Raw mutator: keeps bound_type() in sync with whichever type the
  // surviving output(s) bound to this event actually hold. Only
  // Node::set_output_type (node.hpp) calls this, immediately after it has
  // verified no opposite-typed output remains bound to the same event.
  void set_bound_type(ConnectorType type) noexcept { bound_type_ = type; }

  // Raw mutator: performs no kFifo/capacity-zero rejection. Prefer
  // Node::set_listener_policy (node.hpp), which enforces it.
  void set_capacity(std::size_t capacity) noexcept { capacity_ = capacity; }

  ConnectorType bound_type_;
  QueuePolicy   policy_   = QueuePolicy::kLatestValidWins;
  std::size_t   capacity_ = 1;
};

}  // namespace graphscore
