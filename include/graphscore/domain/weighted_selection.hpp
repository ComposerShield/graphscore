// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// Sequential precedence's tier 3 (docs/plan/README.md: "matching
// persistent event intent wins, then a writer-only manually queued
// connector, then weighted random selection", event_state_machine.hpp)
// and the "100-percent random groups" / "zero-weight ineligibility"
// normative rules -- the specification of record for both.
//
// -- What a "random group" is --
// A random group is exactly the set of a node's ConnectorType::kSequential
// output connectors that also have a destination() (the same
// destination-required-for-eligibility rule event_state_machine.hpp
// applies to tier 1 -- a destination-less output can never be followed,
// so it is never a candidate for anything, including tier 3), evaluated
// at one node boundary. There is exactly one random group per node
// boundary resolution, never one group per event or per connector
// sub-set: every eligible kSequential output, whether or not it also
// carries an event_binding(), competes together in the same group once
// resolution has fallen through tiers 1 and 2 without a winner (see
// EventStateMachine::resolve_sequential_transition()). Assembling this
// group from Node::outputs() is the caller's responsibility (typically
// EventStateMachine); this header only defines the group's exact
// representation and the algorithm that selects from it.
//
// -- Weight representation: exact Rational, a fraction of the whole --
// OutputConnector::weight() (connector.hpp) is a Rational -- this
// candidate's exact fraction of the whole random group -- not a
// floating-point weight and not a whole percent, so the product decision
// "must total exactly 100 percent" is exactly equivalent to "the eligible
// group's weights sum to exactly Rational(1)": a fraction is the whole
// (100%) of a group precisely when every eligible sibling's fraction sums
// with it to Rational(1). Rational (rather than a whole-percent integer)
// is required because ordinary adaptive-music authoring needs splits a
// whole percent cannot express exactly -- a three-way even split is
// 1/3 + 1/3 + 1/3, which as whole percent is either 99 (33+33+33,
// rejected as not totaling 100) or 100 with a silently biased 34/33/33 --
// and this project's locked convention is that musical/structural values
// are exact rationals, never floating point.
//
// -- Negative weights --
// OutputConnector::set_weight() (connector.hpp) rejects a negative weight
// at the point it would be written, so a WeightedCandidate assembled from
// a real OutputConnector never carries one. validate_weight_group() and
// select_weighted_random() nonetheless check for a negative weight
// defensively, ahead of and independently of the exact-sum computation
// below, and treat any candidate with a negative weight as
// WeightGroupValidity::kInvalidTotal outright -- never merely folding it
// into the sum. This is deliberate: a negative weight is worse than an
// out-of-range non-negative one, because it can make an otherwise
// malformed group's total cross-cancel to exactly Rational(1) and be
// misread as valid (e.g. -1/3, 1/3, 1/3, 2/3 sums to exactly 1). Checking
// for negativity as its own, prior step closes that hole regardless of
// what the arithmetic sum would otherwise read.
//
// -- 100-percent groups and zero-weight ineligibility --
// validate_weight_group() classifies `candidates` in this order:
//   - Any candidate with a negative weight (weight().numerator() < 0):
//     kInvalidTotal, unconditionally (see "Negative weights" above).
//   - Otherwise, every zero-weight candidate (weight() == Rational(0)) is
//     excluded from consideration first -- structurally, not merely
//     assigned a zero-width bucket a careless walk could still fall
//     through to. If no candidate remains (the group was empty, or every
//     candidate, if any, was zero-weight): kNoEligibleCandidates. This is
//     not an authoring error -- a node boundary can legitimately have
//     every eligible output at zero weight (for example, while the writer
//     is still assigning weights) -- it simply has nothing tier 3 can
//     select.
//   - Otherwise, this header builds an exact integer common denominator
//     for the remaining (nonzero-weight) candidates -- see "Selection
//     algorithm" below -- and classifies the result:
//     - kDenominatorOverflow if building that common denominator, or any
//       candidate's integer share of it, would require more than 64 bits
//       to represent exactly (see "Overflow" below).
//     - kInvalidTotal if the shares are exactly representable but do not
//       sum to exactly the common denominator (equivalently: the weights
//       do not sum to exactly Rational(1)). This 1-total is over the
//       *eligible* group only (destination-having kSequential outputs --
//       see above): a correctly authored two-way branch that sums to
//       Rational(1) while both branches carry a destination reads
//       kInvalidTotal the moment one branch is disconnected while
//       editing, since the remaining branch's weight alone no longer
//       totals Rational(1). This is a deliberate consequence of composing
//       "destination required for eligibility" with "must total exactly
//       Rational(1)", not a separate rule -- resolve_sequential_
//       transition() reports it as playback stopping rather than letting
//       the remaining branch win outright. OutputConnector::weight() also
//       defaults to Rational(1), so a freshly authored node with two or
//       more destination-having sequential outputs sums to Rational(2) or
//       more and stops playback in exactly the same way until weights are
//       assigned.
//     - kValid if the shares sum to exactly the common denominator.
//
// -- Selection algorithm: exact, integer, no floating point --
// The common denominator D and each eligible candidate's integer share
// n_i of it are built as follows, in `candidates`' own sequence order,
// considering only candidates with a strictly positive weight (every
// zero- or negative-weight candidate is skipped entirely, per "100-percent
// groups and zero-weight ineligibility" above):
//   1. D starts at 1.
//   2. For each eligible candidate in turn, D is folded to
//      lcm(D, candidate.weight().denominator()), computed as
//      (D / gcd(D, denominator)) * denominator -- ordinary integer LCM.
//   3. Once D is final, each eligible candidate's integer share is
//      n_i = candidate.weight().numerator() * (D / candidate.weight()
//      .denominator()) -- exact, because D is by construction a multiple
//      of every eligible candidate's denominator.
//   4. The group is kValid exactly when sum(n_i) == D (see above); when it
//      is, D is this group's *roll bound* and each n_i is that
//      candidate's *bucket width*, in the same sequence order the
//      candidates were folded in.
// select_weighted_random() then walks the eligible candidates once, in
// that same sequence order, accumulating a running cumulative bound
// (cumulative += n_i for each in turn) and returns the first candidate
// for which `roll < cumulative` -- the same "first bucket the roll falls
// strictly below" comparison a whole-percent implementation would use,
// just over exact integer bucket widths instead of percentage points.
// Bucket order is therefore `candidates`' own sequence order, exactly as
// before this phase's Rational conversion: there is no independent
// connector-order field to keep in sync with it, and a second
// implementation transcribing this header need only preserve the
// caller's own vector order, fold lcm() in that same order, and apply the
// same `roll < cumulative` predicate to reproduce identical golden-vector
// winners bit for bit.
//
// -- The roll bound, and drawing exactly one PRNG value per selection --
// select_weighted_random()'s `roll` must be in [0, D) where D is this
// group's roll bound; weight_group_roll_bound() below computes exactly
// that D. The caller-side contract that keeps selection to exactly one
// PRNG draw, taken only once the group is known to be selectable, is:
//
//   if (validate_weight_group(candidates) != WeightGroupValidity::kValid)
//     /* no selection; no PRNG draw */;
//   const std::uint64_t roll =
//       prng.next_below(*weight_group_roll_bound(candidates));
//   const std::optional<ConnectorId> winner =
//       select_weighted_random(candidates, roll);
//
// weight_group_roll_bound(candidates) is guaranteed to return a value at
// that point, since it is nullopt exactly when validate_weight_group(
// candidates) is not kValid. This mirrors the contract this header has
// always had, only with the roll bound now the group's own computed D
// instead of the fixed constant 100 a whole-percent representation used.
//
// -- Overflow --
// Rational (core/rational.hpp) is exact, GCD-reduced, and cross-multiplied
// for comparisons, and can overflow only for non-musical extreme operands
// -- but this header's common-denominator construction (an LCM folded
// across every eligible candidate's denominator) is exactly that
// pathological case: a handful of large, pairwise-coprime denominators can
// make D, or a candidate's share of it, exceed what a 64-bit unsigned
// integer can represent exactly. Every step of the construction above (the
// lcm() fold, each share's multiplication, and the running sum used for
// the exact-1 check) is computed with 64-bit-unsigned-overflow detection,
// and the first step that would overflow makes the whole group
// WeightGroupValidity::kDenominatorOverflow -- select_weighted_random()
// then returns std::nullopt for it, exactly as it does for
// kInvalidTotal or kNoEligibleCandidates, and weight_group_roll_bound()
// returns std::nullopt rather than a truncated or otherwise-inexact bound.
// This is a deliberate, documented fail-closed outcome, not a silent
// wraparound: a group whose exact common denominator cannot be
// represented is treated as unselectable, the same as any other malformed
// group, rather than approximated.
struct WeightedCandidate {
  ConnectorId connector;
  Rational    weight;

  [[nodiscard]] bool operator==(const WeightedCandidate&) const = default;
};

enum class WeightGroupValidity : std::uint8_t {
  kValid = 0,
  kNoEligibleCandidates,
  kInvalidTotal,
  kDenominatorOverflow,
};

// Classifies `candidates` per this header's overview above. An empty
// `candidates` is kNoEligibleCandidates (its sum is vacuously 0).
[[nodiscard]] WeightGroupValidity validate_weight_group(
    std::span<const WeightedCandidate> candidates);

// The exact roll bound D this group's selection requires -- see this
// header's "The roll bound, and drawing exactly one PRNG value per
// selection" overview above -- or std::nullopt whenever
// validate_weight_group(candidates) is not WeightGroupValidity::kValid
// (including WeightGroupValidity::kDenominatorOverflow, where no exact
// bound exists to return). A caller draws its single roll for this group
// via `prng.next_below(*weight_group_roll_bound(candidates))`.
[[nodiscard]] std::optional<std::uint64_t> weight_group_roll_bound(
    std::span<const WeightedCandidate> candidates);

// Selects a winner from `candidates` using `roll`, a value the caller
// drew from [0, D) where D == *weight_group_roll_bound(candidates) (see
// this header's overview above for how D is derived and why using it as
// the draw's bound is what keeps selection to exactly one PRNG value).
// This function performs no PRNG advancement itself and recomputes D and
// every eligible candidate's bucket width internally from `candidates`
// alone, so it stays a pure, deterministic function of its inputs,
// directly testable against hand-computed rolls. Returns std::nullopt
// whenever validate_weight_group(candidates) is not kValid -- an invalid,
// entirely zero-weight, or denominator-overflowing group never yields a
// selection.
//
// Bucket order is `candidates`' own sequence order (see this header's
// "Selection algorithm" overview above) -- this function never sorts or
// reorders its input.
[[nodiscard]] std::optional<ConnectorId> select_weighted_random(
    std::span<const WeightedCandidate> candidates, std::uint64_t roll);

}  // namespace graphscore
