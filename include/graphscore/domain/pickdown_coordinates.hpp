// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/node_timeline.hpp>

namespace graphscore {

// This header's file-level overview -- pickdown meter/tempo coordinates,
// and the "at least one complete main-region measure" invariant
// (docs/plan/README.md; docs/plan/02-domain-model.md's "Node timeline"
// section) -- spanning every declaration below (PickdownCoordinate through
// pickdown_coordinate_at_offset()), not only the one immediately following
// it:
//
//   "A node contains at least one complete main-region measure and may
//   end with one explicit partial pickdown region greater than zero and
//   shorter than one complete measure under the time signature active at
//   the boundary."
//   "A sequential destination starts when the pickdown starts. Pickdown
//   notes continue concurrently on the source tempo curve ..."
//   "The source tempo curve extends through the pickdown."
//
// -- Coordinate system --
// A NodeTimeline's pickdown is not a second, independent timeline: it is
// the trailing region of the SAME timeline, past boundary_position()
// (node_timeline.hpp). There is therefore no unit conversion to perform
// between "a position inside the pickdown" and "source-node musical
// coordinates" -- every Rational position on a NodeTimeline, pickdown
// included, is already expressed in that one node's own whole-note
// coordinate system (measure_map.hpp). What genuinely needs resolving is
// which METER and which TEMPO SEGMENT govern a position once it is
// inside the pickdown, since the pickdown owns neither structure itself:
// it has no Measure entry of its own in MeasureMap (only the main region
// does -- there is no opening partial measure, and symmetrically no
// trailing partial measure either; NodeTimeline's own class comment), and
// its TempoLane coverage is the tail of the SAME lane that governs the
// main region ("the source tempo curve extends through the pickdown" --
// there is no second, pickdown-only TempoLane to select between;
// NodeTimeline::set_tempo() always validates the one lane through
// node_end(), pickdown included). PickdownCoordinate below packages both
// answers, plus the offset/position values, so a caller holding either
// direction can recover the other without re-deriving the arithmetic:
//   - `position`: the absolute node-local Rational -- what
//     NodeTimeline::classify()/node_end() already operate on.
//   - `offset`: `position - timeline.boundary_position()`, i.e. distance
//     into the pickdown, in [0, *timeline.pickdown_duration()).
//   - `boundary_measure_index` / `governing_time_signature`: "the
//     pickdown's meter is the time signature active at the boundary" --
//     the last main-region measure's index and TimeSignature, read once
//     here rather than re-derived by every call site.
//   - `tempo_segment_index`: the index into `timeline.tempo()->points()`
//     of the segment governing `position`, i.e.
//     TempoLane::segment_index_at(position) (tempo_lane.hpp) -- which
//     segment's tempo/beat-unit/TempoSegmentKind data applies. This is
//     std::nullopt whenever the timeline has no tempo lane at all
//     (NodeTimeline::tempo() == nullptr is a legal, if incomplete, state
//     -- node.hpp). This establishes WHICH curve/segment governs; it does
//     not evaluate the BPM that segment actually produces at `position`
//     -- for a TempoSegmentKind::kSmooth segment that evaluation is
//     Phase 7's cubic-curve integration (see tempo_lane.hpp's own note
//     on that, deliberately out of scope here).
//
// -- Minimum main-region duration --
// "At least one complete main-region measure" is enforced structurally,
// not by a runtime check this file adds: MeasureMap::create()
// (measure_map.hpp) rejects an empty `measures` vector outright, and
// NodeTimeline::create() (node_timeline.hpp) builds its MeasureMap
// through that same factory and fails whenever it does (node_timeline.
// cpp). No NodeTimeline mutator can reduce that count afterward:
// set_pickdown()/clear_pickdown() only ever write pickdown_duration_ and
// (transitively, on revalidation) tempo_ -- neither ever touches
// measures_ -- and NodeTimeline exposes no other measure-mutating API at
// all (see its public interface, node_timeline.hpp). The invariant
// therefore already holds, by construction, on every path that can
// create or mutate a NodeTimeline today; pickdown_coordinates_test.cpp
// pins this with both NodeTimeline::create()'s existing empty-measures
// rejection and an explicit check that pickdown mutation never changes
// measures().measure_count(). A future phase that adds a measure-insert/
// -delete mutator (docs/plan/02-domain-model.md's Phase 8 "atomic
// measure insert/delete") must preserve this invariant itself when it
// lands -- there is no separate guard here for it to inherit.
struct PickdownCoordinate {
  Rational                   position;
  Rational                   offset;
  std::size_t                boundary_measure_index = 0;
  TimeSignature              governing_time_signature;
  std::optional<std::size_t> tempo_segment_index;

  [[nodiscard]] bool operator==(const PickdownCoordinate&) const = default;
};

// Resolves `position` -- an absolute node-local Rational -- to its
// PickdownCoordinate. Fails if `timeline` has no pickdown at all, or if
// `position` does not fall within the half-open interval
// [timeline.boundary_position(), timeline.node_end()) -- deliberately not
// NodeTimeline::classify()'s own
// TimelineRegion::kPickdown test, since classify() only checks the lower
// bound (boundary_position()) and has no notion of node_end() as an
// upper bound at all; this function checks both explicitly, matching
// vertical_transition.hpp's map_vertical_position() precedent of not
// reusing classify() where its semantics diverge from what is actually
// needed.
[[nodiscard]] std::optional<PickdownCoordinate> pickdown_coordinate_at_position(
    const NodeTimeline& timeline, Rational position);

// Resolves `offset` -- a distance into the pickdown, [0,
// *timeline.pickdown_duration()) -- to its PickdownCoordinate. Fails
// identically to pickdown_coordinate_at_position() (no pickdown, or
// offset out of range); the result's `position` is always
// `timeline.boundary_position() + offset`.
[[nodiscard]] std::optional<PickdownCoordinate> pickdown_coordinate_at_offset(
    const NodeTimeline& timeline, Rational offset);

}  // namespace graphscore
