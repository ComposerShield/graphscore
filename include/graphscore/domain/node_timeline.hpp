// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/clef_lane.hpp>
#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/staff_layout.hpp>
#include <graphscore/domain/tempo_lane.hpp>

namespace graphscore {

// An exact whole-note [start, end) span (see measure_map.hpp for the
// canonical whole-note position unit). Precondition throughout this file:
// start <= end; span classification does not itself validate this, since
// the notes/ties that will eventually supply spans are Phase 4.
struct MusicalSpan {
  Rational start;
  Rational end;

  [[nodiscard]] bool operator==(const MusicalSpan&) const = default;
};

// Which side of the main-region/pickdown boundary a position or span part
// belongs to.
enum class TimelineRegion : std::uint8_t {
  kMain = 0,
  kPickdown,
};

// The result of classifying a MusicalSpan against the main-region/pickdown
// boundary: the part (if any) that falls in the main region, and the part
// (if any) that falls in the pickdown. A span entirely on one side yields
// only that side; a span straddling the boundary yields both, split
// exactly at the boundary. This is the mechanism later phases use to give
// a crossing note or tie its main and tail ownership at the boundary; it
// classifies positions/spans only; it does not model notes or ties.
struct SpanClassification {
  std::optional<MusicalSpan> main_part;
  std::optional<MusicalSpan> pickdown_part;

  [[nodiscard]] bool operator==(const SpanClassification&) const = default;
};

// A node's musical timeline: the measure map every active track shares, an
// optional trailing pickdown region, each stave's independent clef-change
// lane, and the node-wide tempo lane. All positions/durations are exact
// whole-note Rationals (see measure_map.hpp).
class NodeTimeline {
 public:
  NodeTimeline() = delete;

  // Builds a timeline from a non-empty main-region `measures` map. `staves`
  // seeds one ClefLane per stave, starting at that stave's
  // StaveDefinition default clef; it is normally the flattened set of
  // staves across every track active in the owning node. Fails only if
  // `measures` is empty.
  [[nodiscard]] static std::optional<NodeTimeline> create(
      std::vector<Measure>                measures,
      const std::vector<StaveDefinition>& staves);

  [[nodiscard]] const MeasureMap& measures() const noexcept {
    return measures_;
  }

  // Sets (or replaces) the optional pickdown region trailing the main
  // region. Fails if `duration` is not strictly greater than zero and
  // strictly less than the length of the boundary's active measure (the
  // last main-region measure), per the "0.1.0" pickdown bounds. Changing
  // the region revalidates any existing tempo lane against the new
  // node_end(); the change is rejected, and the timeline left unchanged, if
  // it would leave the tempo lane invalid.
  [[nodiscard]] Result set_pickdown(Rational duration);

  // Clears the pickdown region, if any. Changing the region revalidates
  // any existing tempo lane against the new node_end(); the change is
  // rejected, and the timeline left unchanged, if it would leave the tempo
  // lane invalid.
  [[nodiscard]] Result clear_pickdown();

  [[nodiscard]] std::optional<Rational> pickdown_duration() const noexcept {
    return pickdown_duration_;
  }

  // End of the main region, i.e. the start of an optional pickdown.
  [[nodiscard]] Rational boundary_position() const {
    return measures_.total_length();
  }

  // Exclusive end of the whole node timeline: the pickdown's end if one is
  // set, otherwise the main-region boundary.
  [[nodiscard]] Rational node_end() const;

  [[nodiscard]] TimelineRegion classify(Rational position) const;

  [[nodiscard]] SpanClassification classify(const MusicalSpan& span) const;

  [[nodiscard]] bool has_clef_lane(StaveId stave_id) const {
    return clef_lanes_.contains(stave_id);
  }

  [[nodiscard]] ClefLane* clef_lane(StaveId stave_id);

  [[nodiscard]] const ClefLane* clef_lane(StaveId stave_id) const;

  // Sets (or replaces) the node-wide tempo lane. Fails unless `points`
  // satisfies TempoLane::create against [0/1, node_end()): non-empty,
  // starting exactly at 0/1, strictly ordered, and covering the whole node
  // including any pickdown. A subsequent region change (set_pickdown or
  // clear_pickdown) revalidates this lane against the region's new
  // node_end() and is rejected if it would leave the lane invalid.
  [[nodiscard]] Result set_tempo(std::vector<TempoPoint> points);

  [[nodiscard]] const TempoLane* tempo() const noexcept {
    return tempo_ ? &*tempo_ : nullptr;
  }

 private:
  explicit NodeTimeline(MeasureMap measures);

  // Rebuilds the current tempo lane against `new_end`, reusing its points
  // and start. Returns std::nullopt if the existing points no longer
  // satisfy TempoLane::create's structural rules against `new_end`.
  // Precondition: tempo_.has_value().
  [[nodiscard]] std::optional<TempoLane> rebuild_tempo_for_end(
      Rational new_end) const;

  MeasureMap                            measures_;
  std::optional<Rational>               pickdown_duration_;
  std::unordered_map<StaveId, ClefLane> clef_lanes_;
  std::optional<TempoLane>              tempo_;
};

}  // namespace graphscore
