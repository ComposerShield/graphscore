// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/tempo.hpp>

namespace graphscore {

// Interpolation applied to the tempo segment that starts at a TempoPoint's
// position and runs to the next point's position (or to the lane's end,
// for the lane's final point). Evaluating what a segment's kind actually
// produces at a position -- the cubic curve equations, legal control
// handles, deterministic integration/inversion, and integer sample
// rounding -- is graphscore/core/tempo_curve.hpp's concern, not this
// header's; this enum only names the segment's shape.
enum class TempoSegmentKind : std::uint8_t {
  kStep = 0,
  kLinear,
  kSmooth,
};

// One tempo lane anchor: an exact whole-note position (see
// graphscore/domain/measure_map.hpp for the canonical whole-note position
// unit), a BPM/beat-unit pair reusing the validated Tempo type, and the
// interpolation kind governing the segment that starts here.
//
// This is a pure value type with no invariant of its own to enforce --
// every field is independently valid on its own terms (Rational and Tempo
// are each already validated/exact where that matters) and no combination
// of position/tempo/segment_kind is illegal in isolation; ordering and
// coverage invariants belong to the lane that holds a sequence of these
// (graphscore/domain/tempo_lane.hpp's TempoLane::create), not to one point
// by itself. That is what keeps this a plain aggregate rather than a
// validated-construction class with a private constructor, matching Tempo
// and Rational's own precedent for value types with no per-instance
// invariant.
//
// Lives in graphscore_core, not graphscore_domain, for the same reason
// DeterministicPrng does (graphscore/core/deterministic_prng.hpp): the ADR
// 0003 runtime closure (graphscore_runtime and everything it depends on)
// can see graphscore_core but never graphscore_domain, and the writer and
// runtime must be able to reproduce identical tempo/timing results from
// the same TempoPoint data. Keeping the data type itself in core, next to
// the tempo-curve math that consumes it (tempo_curve.hpp), means neither
// side needs a domain-layer dependency to agree on what a tempo point is.
struct TempoPoint {
  Rational         position;
  Tempo            tempo;
  TempoSegmentKind segment_kind = TempoSegmentKind::kStep;

  [[nodiscard]] bool operator==(const TempoPoint&) const = default;
};

}  // namespace graphscore
