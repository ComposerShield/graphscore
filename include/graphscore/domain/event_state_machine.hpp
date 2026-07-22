// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/event_queue.hpp>
#include <graphscore/domain/node.hpp>

namespace graphscore {

// This header's file-level overview (docs/plan/README.md's locked
// decisions; docs/plan/02-domain-model.md's "Graph model" section) -- the
// specification of record for how sequential and vertical event
// occurrences are stored, interpreted, and arbitrated, spanning every
// declaration below (BoundaryOutcome through EventStateMachine), not only
// the one immediately following it. A toolkit-independent domain model,
// not a runtime/audio implementation.
//
// -- Sequential occurrences persist --
// A sequential occurrence (an OutputConnector of ConnectorType::kSequential
// whose bound event fires) is recorded into a bounded, per-(node, event)
// EventQueue (event_queue.hpp) via record_sequential_occurrence() and
// persists there, honoring the node's EventListener policy/capacity for
// that event, until a node boundary consumes it (resolve_sequential_
// boundary()) or a transport action clears it (clear_node()/clear_all()).
//
// -- Vertical occurrences never persist --
// A vertical occurrence (ConnectorType::kVertical) is sample-local: it
// exists only at the instant its bound event fires and must never enter
// persistent storage (product decision). record_sequential_occurrence()
// enforces this by rejecting any event whose listener is bound
// kVertical -- resolve_vertical_match() below takes the caller's already-
// assembled set of simultaneously-matched vertical connectors directly and
// never writes anything to this class's storage. Node::bind_output_event
// (node.hpp) separately rejects a node ever holding both a kVertical and a
// kSequential listener for the same event, so the two paths can never
// collide over one EventListener.
//
// -- Cross-event arbitration and stable tie-breaking --
// At a node boundary, more than one bound event may have an eligible
// pending occurrence at once. select_winner() (used by both
// resolve_sequential_boundary() and resolve_vertical_match()) arbitrates
// among the competing connectors with a strict, three-tier total order:
//   1. Highest OutputConnector::priority() wins outright.
//   2. On an equal priority, the candidate with the larger arrival_ordinal
//      wins -- "newest candidate occurrence wins equal priority". kVertical
//      candidates are all simultaneous by construction and so pass
//      arrival_ordinal == 0 uniformly, making this tier a no-op for them
//      ("Multiple simultaneous vertical matches use explicit priority and
//      then stable connector order" -- no recency tier applies).
//   3. On both equal, "stable connector order" decides, defined precisely
//      as: the relative order in which the node's still-existing output
//      connectors were originally added (Node::add_output always appends;
//      Node::remove_output erases in place without reordering the
//      survivors), i.e. each candidate's index into Node::outputs() at the
//      moment of arbitration. Two connectors that both still exist always
//      compare the same way under this order regardless of what else was
//      added or removed in between, so it is a genuine, reproducible total
//      order, not merely "whatever the container happens to iterate".
// This total order has no remaining ties: two distinct connectors can
// never share both an equal priority, an equal arrival_ordinal (each
// EventOccurrence gets a unique one), and an equal outputs() index, so
// select_winner() is fully deterministic for any input.
//
// -- Consumption and discard --
// See EventQueue's class comment for the exact per-policy record/consume
// behavior. resolve_sequential_boundary() both selects and immediately
// consumes the winning connector's queue in the same call -- it represents
// the transport actually crossing the boundary, not a dry-run query, so
// there is deliberately no separate "peek the winner without consuming"
// entry point here.
//
// -- A destination is required for eligibility --
// An OutputConnector may hold zero destinations while editing (product
// decision, connector.hpp's export_enabled() docs). A ConnectorType::
// kSequential output with no destination() can never be followed, so it
// is never eligible for anything this class does: it does not count
// toward "the node holds a sequential output" below, and it is never
// added to resolve_sequential_boundary()'s candidate set even when it is
// event-bound with a matching pending occurrence -- an unfollowable
// winner must never consume an occurrence. Only a destination-having
// kSequential output is ever a candidate, event-bound or not.
//
// -- "A node with no eligible sequential output stops playback" --
// resolve_sequential_boundary() reports BoundaryOutcome::kStopPlayback
// whenever the node holds no ConnectorType::kSequential output that also
// has a destination, independent of any event/queue state -- an unbound
// (but destination-having) sequential output is always itself a
// candidate (Phase 5a) for the later manual-queue/weighted-random tiers
// this class does not implement (see below), so this is the only way a
// sequential boundary can have literally nothing to select among.
//
// -- Sequential precedence, tier 1 only --
// docs/plan/README.md's sequential precedence is "matching persistent
// event intent wins, then a writer-only manually queued connector, then
// weighted random selection". This class resolves only the first tier:
// BoundaryOutcome::kEventWinner names the connector tier 1 selects;
// BoundaryOutcome::kNoEventIntent means no event-bound output had an
// eligible occurrence this boundary, and the caller (a later phase) must
// apply the remaining two tiers over Node::outputs() itself. TODO(Phase
// 6): implement those two tiers (the writer-only manual queue and
// weighted-random selection among zero-weight-filtered eligible sequential
// outputs) on top of this result; also owns vertical compatibility/mapping
// rules that decide which connectors are "simultaneously matched" before
// they ever reach resolve_vertical_match(), and the one-vertical-jump-
// per-sample-offset enforcement across nodes.
//
// -- Transport clearing (stop/reset/node-play clear; pause does not) --
// Activating a node's play button "reconciles current notes, clears
// tails/queues, and starts that node from its beginning" (product
// decision); stop and transport reset are both, in effect, "start over
// from a defined point" actions with the identical requirement. All three
// transport actions therefore map onto the same primitive here --
// clear_all(), which discards every pending occurrence project-wide (not
// only the target node's) since each of them represents playback
// restarting from a single defined point, not a partial, per-node
// clearing that would leave other nodes' stale queued state to be
// reconsidered later. clear_node() is exposed as the underlying per-node
// primitive in case a future phase needs a narrower clear. Pause has no
// corresponding call: it is implemented as the absence of one -- pausing
// transport must never call clear_node()/clear_all(), so every pending
// occurrence, across every node, remains exactly as queued when playback
// resumes. Neither clearing operation touches any EventQueue's
// dropped_count(); that diagnostic is reset only by its own explicit
// EventQueue::reset_dropped_count() call.
//
// -- Invariant: an EventListener removal must clear its queue --
// queues_ (below) is keyed by (NodeId, EventId) independently of Node's
// own EventListener lifetime. Node::bind_output_event(out, nullopt)
// (node.hpp) on the last output referencing an event erases that node's
// EventListener entirely; a later rebind to the same event creates a
// fresh listener with default policy/capacity, with no memory of the one
// that existed before. If this class's queue for that (node, event) pair
// is left untouched across such an unbind, a stale, pre-unbind occurrence
// can still be sitting in it, ready to resurrect and win arbitration
// after the rebind as if it had just been recorded. This class does not
// enforce the invariant itself -- it is not wired into Node/Graph
// mutation paths (a later phase's concern) -- so it is a documented
// obligation on every caller instead: any edit that removes a node's
// EventListener for an event must call clear_event() (below) for that
// exact (node, event) pair before the event can ever be rebound.
// clear_node()/clear_all() are not substitutes for this -- they exist for
// transport actions, not for per-listener bookkeeping, and neither is
// guaranteed to run between an unbind and a rebind.
enum class BoundaryOutcome : std::uint8_t {
  kEventWinner = 0,
  kNoEventIntent,
  kStopPlayback,
};

struct BoundaryResolution {
  BoundaryOutcome            outcome = BoundaryOutcome::kNoEventIntent;
  std::optional<ConnectorId> winner;

  [[nodiscard]] bool operator==(const BoundaryResolution&) const = default;
};

// One connector competing for a boundary or a simultaneous vertical match;
// see select_winner()'s tie-break order above for how these fields are
// compared.
struct ArbitrationCandidate {
  ConnectorId   connector;
  std::size_t   connector_order;
  int           priority;
  std::uint64_t arrival_ordinal = 0;

  [[nodiscard]] bool operator==(const ArbitrationCandidate&) const = default;
};

// Deterministic arbitration primitive shared by resolve_sequential_
// boundary() and resolve_vertical_match(); see this header's overview above
// for the exact three-tier order. Returns nullopt only if `candidates` is
// empty.
[[nodiscard]] std::optional<ConnectorId> select_winner(
    const std::vector<ArbitrationCandidate>& candidates);

// Vertical simultaneous-match arbitration: identical to select_winner(),
// restated under its own name because kVertical candidates never carry a
// meaningful arrival_ordinal (see this header's overview above), so this
// reads as "vertical" at the call site rather than reusing the generic
// name. Assembling `candidates` (deciding which vertical connectors
// actually matched at this sample offset) is Phase 6's compatibility/
// mapping responsibility, not this function's.
[[nodiscard]] std::optional<ConnectorId> resolve_vertical_match(
    const std::vector<ArbitrationCandidate>& candidates);

// The normative persistent event state machine itself; see this header's
// overview above for the full specification this class implements.
class EventStateMachine {
 public:
  EventStateMachine() = default;

  // Records a fresh sequential occurrence of `event`, having just fired at
  // `sample_offset` on `node`, into that (node, event) pair's EventQueue.
  // Fails, recording nothing, if `node` has no EventListener bound to
  // `event` or if that listener is bound ConnectorType::kVertical --
  // vertical events are sample-local and must never enter persistent
  // storage; resolve_vertical_match() is the only path for them. Also
  // fails if `sample_offset` is negative (EventOccurrence::create()'s own
  // invariant).
  [[nodiscard]] Result record_sequential_occurrence(const Node&  node,
                                                    EventId      event,
                                                    std::int64_t sample_offset);

  // Resolves `node`'s sequential boundary: tier 1 of sequential precedence
  // only (see this header's overview above). On BoundaryOutcome::
  // kEventWinner, also consumes the winning connector's event queue as a
  // side effect of crossing the boundary.
  [[nodiscard]] BoundaryResolution resolve_sequential_boundary(
      const Node& node);

  // Read-only inspection of the persistent queue for (node, event), or
  // nullptr if nothing has ever been recorded for that pair. Intended for
  // tests and diagnostics.
  [[nodiscard]] const EventQueue* find_queue(NodeId node, EventId event) const;

  // Discards every pending occurrence recorded against `node`, across
  // every event, without touching any other node's queues. Each affected
  // (node, event) pair's EventQueue is emptied in place (EventQueue::
  // clear()), not forgotten -- find_queue() still finds it afterward, and
  // its dropped_count() diagnostic is left untouched.
  void clear_node(NodeId node) noexcept;

  // Discards every pending occurrence project-wide, the same way
  // clear_node() does for a single node. stop, transport reset, and
  // activating a node's play button (node-play) all call this (see this
  // header's overview above); pause never calls it or clear_node().
  void clear_all() noexcept;

  // Discards the pending occurrences for exactly (node, event), the same
  // way clear_node() does for every event on a node, without touching any
  // other (node, event) pair. This is the primitive the "EventListener
  // removal must clear its queue" invariant above requires every caller to
  // call after any edit that erases a node's EventListener for `event`
  // (Node::bind_output_event unbinding the last output referencing it),
  // before that event is ever rebound -- EventStateMachine cannot enforce
  // this itself, since it is not wired into Node/Graph mutation paths.
  void clear_event(NodeId node, EventId event) noexcept;

 private:
  struct Key {
    NodeId  node;
    EventId event;

    [[nodiscard]] bool operator==(const Key&) const = default;
  };

  struct KeyHash {
    [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
      const std::size_t node_hash  = std::hash<NodeId>{}(key.node);
      const std::size_t event_hash = std::hash<EventId>{}(key.event);
      return node_hash ^
             (event_hash + 0x9e3779b9U + (node_hash << 6) + (node_hash >> 2));
    }
  };

  std::unordered_map<Key, EventQueue, KeyHash> queues_;
  std::uint64_t                                next_arrival_ordinal_ = 0;
};

}  // namespace graphscore
