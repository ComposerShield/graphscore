// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/core/tempo_point.hpp>

namespace graphscore {

// A node-wide continuous tempo lane, spanning the main region and any
// pickdown (product decision: "Tempo is a node-wide continuous lane
// anchored to musical positions" and "the source tempo curve extends
// through the pickdown").
//
// This models only the point/segment DATA and its structural validity:
// strictly ordered positions, the lane starting exactly at its declared
// start, and full coverage through its declared end. Evaluating a segment
// — the cubic curve equations, legal control handles, deterministic
// integration/inversion, and integer sample rounding at boundaries — is
// graphscore/core/tempo_curve.hpp's concern: tempo_rate_at(),
// integrate_elapsed_seconds(), and invert_elapsed_seconds() there operate
// directly on this class's points() (and, for inversion, end()) once a
// caller has resolved which segment governs a position via
// segment_index_at() below.
class TempoLane {
 public:
  TempoLane() = delete;

  // Fails unless `points` is non-empty, strictly ordered by position,
  // begins exactly at `start`, and every point lies within [start, end).
  // `start`/`end` are the lane's inclusive start and exclusive end, e.g.
  // start == 0/1 and end == the node's total length including any
  // pickdown.
  [[nodiscard]] static std::optional<TempoLane> create(
      std::vector<TempoPoint> points, Rational start, Rational end);

  [[nodiscard]] const std::vector<TempoPoint>& points() const noexcept {
    return points_;
  }

  [[nodiscard]] Rational start() const noexcept { return start_; }

  [[nodiscard]] Rational end() const noexcept { return end_; }

  // The index into points() of the segment governing `position`: the last
  // point whose position() is <= `position`. This identifies WHICH
  // segment's data (tempo, beat unit, TempoSegmentKind) applies at
  // `position` -- it does not evaluate the BPM that segment actually
  // produces there, which is graphscore/core/tempo_curve.hpp's
  // tempo_rate_at() for every TempoSegmentKind, kStep and kLinear included,
  // deliberately not this purely structural lookup. Returns
  // std::nullopt if `position` is outside [start(), end()) -- the lane's
  // own declared coverage -- since create()'s own invariants (points()
  // non-empty, strictly ordered, and the first point exactly at start())
  // guarantee that any position inside that range is governed by some
  // point.
  [[nodiscard]] std::optional<std::size_t> segment_index_at(
      Rational position) const;

  [[nodiscard]] bool operator==(const TempoLane&) const = default;

 private:
  TempoLane(std::vector<TempoPoint> points, Rational start,
            Rational end) noexcept;

  std::vector<TempoPoint> points_;
  Rational                start_;
  Rational                end_;
};

}  // namespace graphscore
