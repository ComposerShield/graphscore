// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// Interpolation applied to the tempo segment that starts at a TempoPoint's
// position and runs to the next point's position (or to the lane's end,
// for the lane's final point).
enum class TempoSegmentKind : std::uint8_t {
  kStep = 0,
  kLinear,
  kSmooth,
};

// One tempo lane anchor: an exact whole-note position (see measure_map.hpp
// for the canonical whole-note position unit), a BPM/beat-unit pair reusing
// the validated core Tempo type, and the interpolation kind governing the
// segment that starts here.
struct TempoPoint {
  Rational         position;
  Tempo            tempo;
  TempoSegmentKind segment_kind = TempoSegmentKind::kStep;

  [[nodiscard]] bool operator==(const TempoPoint&) const = default;
};

// A node-wide continuous tempo lane, spanning the main region and any
// pickdown (product decision: "Tempo is a node-wide continuous lane
// anchored to musical positions" and "the source tempo curve extends
// through the pickdown").
//
// This models only the point/segment DATA and its structural validity:
// strictly ordered positions, the lane starting exactly at its declared
// start, and full coverage through its declared end. Evaluating a kSmooth
// segment — the cubic curve equations, legal control handles, deterministic
// integration/inversion tolerances, and integer sample rounding at
// boundaries — is the Phase 7 normative playback specification.
//
// TODO(Phase 7): cubic smooth-tempo curve integration/inversion, tolerances,
// and integer sample rounding at segment boundaries.
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

  [[nodiscard]] bool operator==(const TempoLane&) const = default;

 private:
  TempoLane(std::vector<TempoPoint> points, Rational start,
            Rational end) noexcept;

  std::vector<TempoPoint> points_;
  Rational                start_;
  Rational                end_;
};

}  // namespace graphscore
