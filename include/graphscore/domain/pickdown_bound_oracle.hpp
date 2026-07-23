// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

// This header's file-level overview -- the finite-overlap-through-cycles
// proof oracle (docs/plan/README.md's locked decision): "Export computes a
// finite upper bound for concurrent pickdown tails and rejects assets
// whose timing cannot be bounded." docs/plan/02-domain-model.md's Test
// Focus names this "pickdown-bound proof-oracle" explicitly. This overview
// spans every declaration below (PickdownBoundStatus through
// prove_pickdown_overlap_bound()), not only the one immediately following
// it.
//
// -- Cycles are valid, and bounded by construction --
// "Cycles are valid" (docs/plan/README.md) is a locked decision, and a
// pickdown-bearing node sitting on a cycle is not an export error: Adam's
// ruling (product decision, Phase 6b-i; see docs/plan/M02_HANDOFF.md's
// "Deferrals carried forward") is "Allow. In that scenario the pickdown
// measure still plays on every loop by overlapping with the start of the
// loop." -- intended musical behavior, not a defect. Two invariants locked
// well before this file existed make that provably safe with no timing
// analysis at all:
//   1. A node has at least one complete main-region measure
//      (docs/plan/README.md, docs/plan/02-domain-model.md;
//      NodeTimeline::create requires a non-empty `measures`).
//   2. A pickdown is strictly greater than zero and strictly shorter than
//      one complete measure under the boundary's meter (NodeTimeline::
//      set_pickdown, node_timeline.hpp).
// A pickdown tail is created the instant a sequential boundary is crossed
// out of a node that has a pickdown ("A sequential destination starts
// when the pickdown starts") and survives "until naturally complete"
// (docs/plan/README.md), taking less than one measure to do so.
//
// The musical time between two successive boundary crossings of the same
// node is always at least that node's full main-region length. Note
// carefully that this is NOT because the node replays a complete measure
// of its own on every revisit -- a vertical jump can re-enter a node very
// close to its own boundary, leaving only a fraction of a measure left to
// play. It is because the route around the cycle telescopes:
//   - A vertical transition requires vertical_regions_compatible()
//     (enforced in assemble_vertical_candidates() before a candidate is
//     eligible, and independently by map_vertical_position(), which
//     returns nullopt for an incompatible pair). Compatibility means an
//     identical main-region measure count and identical per-measure time
//     signatures, hence an identical total main-region length, and
//     map_vertical_position() is then the identity. A vertical jump
//     therefore skips exactly as much of its destination as had already
//     been played in its source: it consumes no musical time and no
//     musical position. Vertical hops telescope away.
//   - Only a sequential transition resets position, and always to 0 of
//     its destination.
// Partition the route from one boundary crossing to the next into runs
// separated by sequential transitions. Each run telescopes to (last exit
// position - first entry position), and every run after the first enters
// at 0; the final run ends at this node's boundary. Summing the runs, the
// elapsed musical position is at least this node's main-region length --
// no matter how many vertical jumps intervene. That length is at least
// one complete measure, which is strictly greater than the pickdown, so
// the previous tail has always finished sounding by the time the next one
// begins whenever tempo is comparable across the cycle: concurrency per
// pickdown-bearing node is at most 1.
//
// A degenerate zero-time chain of vertical jumps cannot defeat this:
// "At most one vertical transition may be selected from the node active
// at a given sample offset, so same-sample events cannot chain through a
// vertical cycle" (docs/plan/README.md), enforced by the fail-closed
// TransportInstant guard in resolve_vertical_transition().
//
// This is loop-overlap by design, not overflow -- the tail sounds on
// every iteration, overlapping the start of the NEXT iteration's main
// region, exactly as Adam's ruling describes.
//
// -- What remains unproven (Phase 7) --
// The one case this structural argument does not close is tempo
// disparity around a cycle: a tail running on a slow source tempo curve
// (docs/plan/README.md: "The source tempo curve extends through the
// pickdown") could in principle still be sounding when a much faster
// route back around the cycle -- through another node's own tempo curve,
// or a vertical jump landing close to this node's own boundary -- brings
// playback back around for a second tail before the first tail's real
// elapsed time has caught up. Proving or bounding that requires
// integrating real time over the cubic smooth-tempo curve
// (graphscore/core/tempo_curve.hpp's integrate_elapsed_seconds(), landed
// in Phase 7), which is a timing-analysis concern this increment does not
// take on. PickdownBoundStatus::kUnbounded (below) is retained --
// unreachable from any structural input today -- as the verdict Phase 7's
// timing analysis is expected to report if it ever finds such a case.
//
// -- The bound value --
// Because structure alone can never make a project unbounded, this oracle
// does not need to reason about the connector graph's shape at all: each
// pickdown-bearing node contributes at most 1 concurrently alive tail (per
// the argument above), so the bound is exactly the count of
// pickdown-bearing nodes in `project`.
enum class PickdownBoundStatus : std::uint8_t {
  kBounded = 0,

  // Reserved for Phase 7's tempo-curve-integration timing analysis (see
  // this header's overview's "What remains unproven" section). No
  // structural input reaches this today -- prove_pickdown_overlap_bound()
  // always returns kBounded -- but the enumerator, and
  // PickdownBoundResult::unbounded_node below, are kept so a future Phase
  // 7 verdict does not require an API break.
  kUnbounded,
};

struct PickdownBoundResult {
  PickdownBoundStatus status = PickdownBoundStatus::kBounded;

  // Valid only when status == kBounded: a finite upper bound on how many
  // pickdown tails this project can ever have concurrently alive at once
  // -- see this header's overview for the >= 1-measure-main /
  // < 1-measure-pickdown argument this counts against (currently: the
  // count of pickdown-bearing nodes, each contributing at most 1).
  std::optional<std::uint64_t> bound;

  // Valid only when status == kUnbounded. Reserved for Phase 7's timing
  // analysis (see this header's overview) -- no structural input sets
  // this today; prove_pickdown_overlap_bound() never returns kUnbounded.
  std::optional<NodeId> unbounded_node;

  [[nodiscard]] bool operator==(const PickdownBoundResult&) const = default;
};

// Proves a finite upper bound on concurrently active pickdown tails for
// `project` -- see this header's overview above for the argument and what
// remains open for Phase 7. A project with no pickdown-bearing node at
// all is vacuously kBounded with bound == 0. Cycles containing a
// pickdown-bearing node are valid and bounded (docs/plan/README.md's
// "Cycles are valid"; Adam's ruling above); this function does not
// perform graph reachability analysis over the connector graph.
[[nodiscard]] PickdownBoundResult prove_pickdown_overlap_bound(
    const Project& project);

}  // namespace graphscore
