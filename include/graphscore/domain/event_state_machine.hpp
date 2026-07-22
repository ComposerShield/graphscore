// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/event_queue.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/project.hpp>

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
// candidate (Phase 5a) for the manual-queue/weighted-random tiers below,
// so this is the only way a sequential boundary can have literally
// nothing to select among.
//
// -- Sequential precedence, all three tiers --
// docs/plan/README.md's sequential precedence is "matching persistent
// event intent wins, then a writer-only manually queued connector, then
// weighted random selection". resolve_sequential_boundary() resolves only
// the first tier in isolation (BoundaryOutcome::kEventWinner names the
// connector it selects; BoundaryOutcome::kNoEventIntent means no
// event-bound output had an eligible occurrence this boundary).
// resolve_sequential_transition() composes all three tiers into the one
// entry point a caller actually crossing a boundary should use:
//   1. resolve_sequential_boundary() (unchanged, above). A kEventWinner or
//      kStopPlayback result here ends the resolution outright -- tiers 2
//      and 3 are never consulted once tier 1 has either selected a winner
//      or already determined the node has nothing eligible at all
//      (destination-less/absent kSequential outputs -- see "A destination
//      is required for eligibility" above, which applies identically to
//      tiers 2 and 3).
//   2. On kNoEventIntent, the writer-only manual queue (queue_manual_
//      transition(), find_manual_queue()): a single pending connector per
//      node, settable only while that node is the active node ("queues a
//      sequential connector ... only when its source is the active
//      node"), consumed -- whether it wins or turns out stale -- the next
//      time this tier is reached for that node. A stale queued connector
//      (removed, retyped away from kSequential, or disconnected since it
//      was queued) is silently discarded and resolution falls through to
//      tier 3, rather than stopping playback -- only tier 1's "no eligible
//      sequential output at all" check does that.
//   3. Otherwise, weighted-random selection (weighted_selection.hpp) over
//      the random group assembled from every eligible kSequential output
//      on the node, using this instance's own DeterministicPrng
//      (core/deterministic_prng.hpp). An invalid or entirely zero-weight
//      group (weighted_selection.hpp's WeightGroupValidity) yields no
//      selection, which resolve_sequential_transition() reports as
//      playback stopping -- the same outcome as tier 1's "no eligible
//      sequential output", since nothing was, in the end, actually
//      selectable.
// SequentialTransitionResult reports which tier (if any) produced the
// winner, distinctly from BoundaryOutcome's tier-1-only vocabulary --
// "kEventWinner" would be a misnomer for a manual-queue or weighted-random
// winner, so this composed entry point uses its own result type instead of
// overloading BoundaryResolution.
//
// -- Vertical candidate assembly and the one-jump-per-transport-instant rule --
// assemble_vertical_candidates() is the compatibility/mapping-aware
// candidate assembly resolve_vertical_match()'s own doc comment defers to:
// it decides which of `active_node`'s kVertical outputs are "simultaneously
// matched" at this instant before they ever reach resolve_vertical_match(),
// applying every eligibility rule at once -- destination required (the
// same rule tier 1 sequential resolution applies, carried over here so a
// destination-less vertical connector can never win an unfollowable jump),
// the bound event must actually be among `fired_events` this instant,
// `active_region` must be TimelineRegion::kMain ("pickdown tails never
// route" -- a node whose active position is currently in its own pickdown
// region yields no candidates at all, independent of any connector state),
// and the destination node (resolved via `project`) must have a timeline
// that is vertical_regions_compatible() (vertical_transition.hpp) with
// `active_node`'s. "Only the active main node can route vertically" holds
// by construction: this function only ever inspects `active_node`'s own
// outputs, never any other node's, so there is no path by which a
// non-active node's connectors could ever become candidates.
//
// EventStateMachine::resolve_vertical_transition() then enforces "at most
// one vertical transition may be selected from the node active at a given
// sample offset" (docs/plan/README.md) on top of resolve_vertical_match().
// The parameter that identifies "a given sample offset" is TransportInstant
// (below), not EventOccurrence::sample_offset() -- deliberately a distinct
// type, since the two are different coordinate systems. sample_offset() is
// scoped to whichever single node's boundary or jump produced it
// (event_occurrence.hpp); TransportInstant is project-wide and must
// identify the same real moment regardless of which node's outputs are
// being evaluated at it. This distinction is exactly what makes the guard
// correct across a jump: node A jumping vertically to node B lands B at
// its own, generally *different*, locally mapped sample position
// (map_vertical_position(), vertical_transition.hpp -- README: "the
// destination begins using its own tempo curve at the mapped position"), so
// a guard keyed on any single node's local sample coordinate could compare
// A's and B's positions as different when they are, in fact, the same real
// instant. The caller (ultimately the transport driving playback, wired up
// in a later phase) is responsible for supplying the *same* TransportInstant
// to both the pre-jump and any immediately-following post-jump evaluation
// within one real instant, and a TransportInstant that is monotonically
// non-decreasing across successive calls to one EventStateMachine instance
// otherwise (see TransportInstant's own docs for the exact precondition).
// resolve_vertical_transition() remembers the TransportInstant of the most
// recent vertical jump it actually produced, and refuses to produce another
// at an instant that is not strictly greater than it -- even from a
// different node's candidates -- even if the candidates passed would
// otherwise have a winning one. This is what makes "same-sample events
// cannot chain through a vertical cycle" hold: node A jumping vertically to
// node B, whose own vertical connector would otherwise also fire at that
// identical instant, can never immediately jump again out of B within the
// same instant. clear_all() resets this guard (see below), since it is
// project-wide, transport-level state, not per-node state clear_node()
// would own.
//
// -- Transport clearing (stop/reset/node-play clear; pause does not) --
// Activating a node's play button "reconciles current notes, clears
// tails/queues, and starts that node from its beginning" (product
// decision); stop and transport reset are both, in effect, "start over
// from a defined point" actions with the identical requirement. All three
// transport actions therefore map onto the same primitive here --
// clear_all(), which discards every pending occurrence project-wide (not
// only the target node's), every node's pending manual queue entry, and
// the vertical one-jump-per-transport-instant guard, since each of these
// transport actions represents playback restarting from a single defined
// point, not a partial, per-node clearing that would leave other nodes'
// stale queued state (or a stale guard TransportInstant that could
// spuriously collide with a freshly restarted transport clock) to be
// reconsidered later. clear_node() is exposed as the underlying per-node
// primitive in case a future phase needs a narrower clear; it clears that
// node's event queues and its manual queue entry (if any), but never the
// project-wide vertical-jump guard. Pause has no corresponding call: it is
// implemented as the absence of one -- pausing transport must never call
// clear_node()/clear_all(), so every pending occurrence, every manual
// queue entry, and the vertical-jump guard all remain exactly as they were
// when playback resumes. Neither clearing operation touches any
// EventQueue's dropped_count(); that diagnostic is reset only by its own
// explicit EventQueue::reset_dropped_count() call.
//
// -- PRNG reset is a distinct capability from transport clearing --
// reset_random() (deterministic PRNG reseeding for tier 3) is deliberately
// never called implicitly by clear_node()/clear_all(): a transport
// stop/reset/node-play does not, by itself, imply the host wants a fresh
// random stream, and reproducing a specific playback recording (the
// "shared golden vectors" acceptance bar) requires being able to clear
// queues without disturbing where the PRNG stream currently is. Call
// reset_random() explicitly whenever a fresh, reproducible stream is
// actually wanted.
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
//
// A project-wide instant, supplied by the caller driving playback, that
// identifies "a given sample offset" for resolve_vertical_transition()'s
// one-jump guard (see this header's overview above). Deliberately not
// std::int64_t and not EventOccurrence::sample_offset() -- construction
// only through create() below, rather than an implicit conversion from any
// integer, is what makes it impossible to pass a node-local sample_offset()
// here by accident: the two are different coordinate systems, and this
// class exists so a caller cannot conflate them without an explicit,
// visible create() call.
//
// EventStateMachine does not construct, derive, or convert this value
// itself -- it is not wired to any transport/scheduler (a later phase's
// concern) -- so it treats every TransportInstant it receives as already
// correct. Precondition on the caller: across successive calls to
// resolve_vertical_transition() on one EventStateMachine instance (outside
// of clear_all() resetting the guard), TransportInstant values must be
// monotonically non-decreasing, and two calls share the identical
// TransportInstant if and only if they refer to the same real project-wide
// instant -- regardless of which node's outputs produced the candidates
// passed alongside it, and regardless of what any node's own locally mapped
// sample position reads at that instant. resolve_vertical_transition()'s
// guard compares with <=, not ==, precisely so a caller that violates
// monotonicity (an instant that unexpectedly failed to advance, or moved
// backward, relative to the last recorded jump) fails closed -- blocking a
// jump it cannot safely have confidence is a genuinely new instant -- rather
// than failing open.
class TransportInstant {
 public:
  // Returns std::nullopt for a negative `value` -- sample positions are
  // never negative (EventOccurrence::create() rejects a negative
  // sample_offset() for the same reason), so neither should the instant
  // that identifies one project-wide.
  [[nodiscard]] static std::optional<TransportInstant> create(
      std::int64_t value) {
    if (value < 0)
      return std::nullopt;
    return TransportInstant(value);
  }

  [[nodiscard]] std::int64_t value() const noexcept { return value_; }

  [[nodiscard]] bool operator==(const TransportInstant&) const = default;

  [[nodiscard]] bool operator<=(const TransportInstant& other) const noexcept {
    return value_ <= other.value_;
  }

 private:
  explicit TransportInstant(std::int64_t value) noexcept : value_(value) {}

  std::int64_t value_;
};

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
// actually matched at this instant, applying compatibility/mapping rules)
// is assemble_vertical_candidates()'s responsibility, not this function's.
[[nodiscard]] std::optional<ConnectorId> resolve_vertical_match(
    const std::vector<ArbitrationCandidate>& candidates);

// Which tier of sequential precedence produced resolve_sequential_
// transition()'s winner (see this header's overview above). Deliberately
// not BoundaryOutcome: "kEventWinner" would misname a manual-queue or
// weighted-random winner.
enum class SequentialTransitionTier : std::uint8_t {
  kEventIntent = 0,
  kManualQueue,
  kWeightedRandom,
};

struct SequentialTransitionResult {
  // nullopt iff playback stops at this boundary (no tier produced a
  // selectable winner).
  std::optional<ConnectorId> winner;
  // nullopt iff winner is nullopt.
  std::optional<SequentialTransitionTier> tier;

  [[nodiscard]] bool operator==(const SequentialTransitionResult&) const =
      default;
};

// Assembles the vertical arbitration candidate set for `active_node`'s
// eligible kVertical outputs at this instant; see this header's overview
// above for the exact eligibility rules applied (destination required,
// event actually fired, active region must be main, destination timeline
// vertical-compatible). Every included candidate's arrival_ordinal is 0
// uniformly (see resolve_vertical_match()'s doc comment for why). Returns
// an empty vector, never an error, when nothing is eligible -- an empty
// result is exactly what resolve_vertical_match() already treats as "no
// winner".
[[nodiscard]] std::vector<ArbitrationCandidate> assemble_vertical_candidates(
    const Project& project, const Node& active_node,
    TimelineRegion active_region, std::span<const EventId> fired_events);

// The normative persistent event state machine itself; see this header's
// overview above for the full specification this class implements.
class EventStateMachine {
 public:
  // Seed used by the default constructor when the caller does not supply
  // one explicitly. Not otherwise significant -- any fixed constant would
  // do; call reset_random() for host-specified seeding.
  static constexpr std::uint64_t kDefaultRandomSeed = 0;

  explicit EventStateMachine(std::uint64_t random_seed = kDefaultRandomSeed)
      : prng_(random_seed) {}

  // Deterministically reseeds tier-3 weighted-random selection in place:
  // from this call onward, the sequence of rolls resolve_sequential_
  // transition() draws is bit-for-bit identical to a freshly constructed
  // EventStateMachine(seed)'s sequence, regardless of how many rolls this
  // instance already drew before the call ("deterministic PRNG reset").
  // Affects only tier-3 randomness; every other piece of state (queues,
  // manual queue entries, the vertical-jump guard) is untouched.
  void reset_random(std::uint64_t seed) noexcept {
    prng_ = DeterministicPrng(seed);
  }

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

  // Tier 2: sets `node`'s pending manually queued connector to `output_id`,
  // replacing any connector already queued for it. Fails, changing nothing,
  // if `node.id() != active_node` ("only when its source is the active
  // node"), if `output_id` does not reference an output on `node`, if that
  // output is not ConnectorType::kSequential, or if it has no
  // destination() -- the same destination-required-for-eligibility rule
  // applied everywhere else in this header. This class never validates that
  // destination().node actually still references an existing node: this
  // method, resolve_sequential_boundary(), and resolve_sequential_transition()
  // all take only `node`, never a Project, so they have no way to check --
  // exactly the same limitation assemble_vertical_candidates() would have if
  // it were not separately handed `project`. Confirming a destination node
  // still exists is the caller's (ultimately Phase 9's ValidationService's)
  // obligation.
  [[nodiscard]] Result queue_manual_transition(NodeId      active_node,
                                               const Node& node,
                                               ConnectorId output_id);

  // The connector currently queued for `node` via queue_manual_transition(),
  // or nullopt if none is pending. Intended for tests and diagnostics;
  // resolve_sequential_transition() is the only path that consumes it.
  [[nodiscard]] std::optional<ConnectorId> find_manual_queue(NodeId node) const;

  // The full three-tier sequential-precedence resolution (see this
  // header's overview above): resolve_sequential_boundary() (tier 1),
  // falling through to the manual queue (tier 2), falling through to
  // weighted-random selection over `node`'s eligible outputs (tier 3,
  // drawing from this instance's own DeterministicPrng). A tier-1
  // kEventWinner or kStopPlayback ends resolution immediately, in either
  // case discarding any pending manual queue entry for `node` (it was
  // rendered moot this boundary either way). Reaching tier 2 or tier 3
  // always empties `node`'s manual queue entry, whether or not it, or
  // random selection, actually produces a winner.
  [[nodiscard]] SequentialTransitionResult resolve_sequential_transition(
      const Node& node);

  // Resolves a vertical transition among `candidates` (typically built by
  // assemble_vertical_candidates() above), enforcing "at most one vertical
  // transition may be selected from the node active at a given sample
  // offset" on top of resolve_vertical_match(): returns nullopt without even
  // consulting `candidates` if this instance already produced a vertical
  // winner at a `transport_instant` that was not strictly less than this
  // one; otherwise delegates to resolve_vertical_match() and, if it selects
  // a winner, remembers `transport_instant` so no further vertical jump can
  // be produced at or before it. See TransportInstant's own docs (above) for
  // exactly what this parameter identifies, why it is not
  // EventOccurrence::sample_offset(), and the monotonicity precondition it
  // places on the caller.
  [[nodiscard]] std::optional<ConnectorId> resolve_vertical_transition(
      TransportInstant                         transport_instant,
      const std::vector<ArbitrationCandidate>& candidates);

  // Read-only inspection of the persistent queue for (node, event), or
  // nullptr if nothing has ever been recorded for that pair. Intended for
  // tests and diagnostics.
  [[nodiscard]] const EventQueue* find_queue(NodeId node, EventId event) const;

  // Discards every pending occurrence recorded against `node`, across
  // every event, and that node's pending manual queue entry (if any),
  // without touching any other node's queues or queued connector. Each
  // affected (node, event) pair's EventQueue is emptied in place
  // (EventQueue::clear()), not forgotten -- find_queue() still finds it
  // afterward, and its dropped_count() diagnostic is left untouched. Never
  // touches the project-wide vertical-jump guard (see clear_all()).
  void clear_node(NodeId node) noexcept;

  // Discards every pending occurrence project-wide, the same way
  // clear_node() does for a single node, every node's pending manual queue
  // entry, and the vertical one-jump-per-transport-instant guard. stop,
  // transport reset, and activating a node's play button (node-play) all
  // call this (see this header's overview above); pause never calls it or
  // clear_node(). Never reseeds the PRNG -- see reset_random().
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
  std::unordered_map<NodeId, ConnectorId>      manual_queue_;
  DeterministicPrng                            prng_;
  std::optional<TransportInstant>              last_vertical_jump_instant_;
};

}  // namespace graphscore
