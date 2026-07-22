// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/node_timeline.hpp>

namespace graphscore {

// Vertical compatibility and position mapping (docs/plan/README.md): "A
// vertical transition jumps immediately at the event's sample offset to
// the same measure and exact rational offset within that measure in its
// destination"; "Source and destination must have the same main-region
// measure count and corresponding time signatures. Pickdowns are excluded
// from compatibility"; "The destination begins using its own tempo curve
// at the mapped position." These two functions are the exact,
// toolkit-independent definitions of "compatible" and "mapped position";
// deciding which vertical connectors are eligible to be evaluated in the
// first place (destination-having, event-fired, active-main-region-only,
// one-jump-per-sample-offset) is event_state_machine.hpp's concern
// (assemble_vertical_candidates(), EventStateMachine::
// resolve_vertical_transition()), which composes with these.
//
// Both operate purely on NodeTimeline's own structural data (MeasureMap,
// exact Rational positions) and never on floating point, matching
// measure_map.hpp's canonical whole-note position unit.

// True iff `source` and `destination` are vertical-transition compatible:
// an identical main-region measure count, and, for every corresponding
// measure index, an identical TimeSignature (KeySignature is not part of
// compatibility -- only "corresponding time signatures" is the product
// decision). Pickdown presence, duration, or absence on either side never
// affects this result ("Pickdowns are excluded from compatibility"):
// only measures()/MeasureMap state is ever consulted here.
[[nodiscard]] bool vertical_regions_compatible(const NodeTimeline& source,
                                               const NodeTimeline& destination);

// Maps `position` -- an exact whole-note position within `source`'s main
// region -- to the corresponding position in `destination`'s main region:
// the same measure index, at the same exact rational offset within that
// measure ("the same measure and exact rational offset within that
// measure in its destination"). Once mapped, subsequent playback at that
// position reads destination.tempo() (its own, independent TempoLane) --
// "the destination begins using its own tempo curve at the mapped
// position" requires no further mechanism here, since NodeTimeline never
// shares a TempoLane between nodes to begin with.
//
// Note for anyone tempted to simplify this to `return position`: the
// mapping is mathematically the identity whenever source and destination
// are vertical_regions_compatible() (an identical main-region measure
// count and, per measure, an identical TimeSignature necessarily gives
// each measure the same start position in both timelines). This function
// exists so that fact, and the two ways mapping can instead fail
// (incompatible timelines; a position outside source's main region), are
// stated once, normatively, rather than re-derived by every caller or by a
// second implementation.
//
// Returns std::nullopt if source and destination are not
// vertical_regions_compatible(), or if `position` does not fall within
// source's main region. This function checks that with
// source.measures().measure_index_at(position), not
// source.classify(position) -- the two agree for every position this
// function is ever reasonably called with, but diverge for a negative
// position (classify() reports TimelineRegion::kMain for a negative
// position; measure_index_at() reports nullopt), and this function's
// nullopt-on-negative behavior is the one that is actually correct here.
// By construction this also excludes any pickdown-tail position --
// "pickdown tails never route" is enforced by the candidate-eligibility
// layer above this function, but this function independently refuses to
// map a pickdown-region position regardless.
[[nodiscard]] std::optional<Rational> map_vertical_position(
    const NodeTimeline& source, const NodeTimeline& destination,
    Rational position);

}  // namespace graphscore
