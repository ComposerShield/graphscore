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

  // Replaces only the key signature of one measure, preserving its time
  // signature and therefore every measure start and timeline length. Fails
  // when `measure_index` is out of range.
  [[nodiscard]] Result set_measure_key_signature(std::size_t  measure_index,
                                                 KeySignature key_signature);

  // Measure-structure entry points. These rebuild the complete timeline
  // candidate (measure map, clef lanes, tempo lane, and unchanged exact
  // pickdown duration) before publishing it. Absolute clef/tempo positions
  // follow the edited musical time. A pickdown that is no longer shorter
  // than the final measure rejects the edit. Deleting the first measure uses
  // a later point shifted to zero as the new tempo origin; when none exists,
  // the old mandatory origin is retained as the active-tempo fallback.
  [[nodiscard]] Result insert_measure(std::size_t index, Measure measure);
  [[nodiscard]] Result insert_measure(std::size_t index);
  [[nodiscard]] Result remove_measure(std::size_t index);
  [[nodiscard]] Result set_measure_time_signature(std::size_t   measure_index,
                                                  TimeSignature time_signature);

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

  // Stable enumeration for validation; order is UUID-lexicographic rather
  // than the unspecified order of the internal hash table.
  [[nodiscard]] std::vector<StaveId> clef_stave_ids() const;

  // Narrow clef-lane mutation entry points. Each resolves `stave_id` and
  // delegates to the corresponding ClefLane operation. A missing stave or
  // invalid lane edit fails without mutation.
  [[nodiscard]] Result add_clef_change(StaveId stave_id, Rational position,
                                       Clef clef);

  [[nodiscard]] Result remove_clef_change(StaveId stave_id, Rational position);

  [[nodiscard]] Result move_clef_change(StaveId stave_id, Rational from,
                                        Rational to);

  // Replaces one stave's complete lane from an already-prepared snapshot.
  // Fails if the stave is missing or the replacement's default clef differs
  // from the target lane's; committing an accepted replacement does not
  // allocate.
  [[nodiscard]] Result restore_clef_lane(StaveId  stave_id,
                                         ClefLane replacement) noexcept;

  // Creates a brand-new clef lane for `stave_id` from `lane`. Fails with
  // kInvalidArgument, leaving the timeline unchanged, if a lane already
  // exists for `stave_id` -- use restore_clef_lane to replace one that
  // exists. May allocate. Paired with remove_clef_lane so a reversible
  // command that must apply notation to a stave with no clef lane yet
  // (e.g. a paste touching a track added after the node's timeline was
  // created) can undo the creation exactly, mirroring the
  // Project::hard_remove_track/add_track_with_id undo-only precedent.
  //
  // Precondition (caller-enforced): `lane.default_clef()` must equal
  // `stave_id`'s own StaveDefinition::default_clef. NodeTimeline has no
  // track/layout view of its own, so it cannot check stave ownership
  // itself -- restore_clef_lane relies on the lane it later replaces having
  // been created with the correct default clef in the first place.
  [[nodiscard]] Result create_clef_lane(StaveId stave_id, ClefLane lane);

  // Removes stave_id's clef lane entirely, restoring "no clef lane" for
  // that stave. No-op if none is present. Undo-only counterpart to
  // create_clef_lane; never allocates.
  void remove_clef_lane(StaveId stave_id) noexcept;

  // Sets (or replaces) the node-wide tempo lane. Fails unless `points`
  // satisfies TempoLane::create against [0/1, node_end()): non-empty,
  // starting exactly at 0/1, strictly ordered, and covering the whole node
  // including any pickdown. A subsequent region change (set_pickdown or
  // clear_pickdown) revalidates this lane against the region's new
  // node_end() and is rejected if it would leave the lane invalid.
  [[nodiscard]] Result set_tempo(std::vector<TempoPoint> points);

  // Clears the node-wide tempo lane, if any. Always succeeds: a node with
  // no tempo lane is the valid default state (see the tempo_ member's
  // optionality), and unlike set_pickdown/clear_pickdown there is nothing
  // to revalidate.
  void clear_tempo() noexcept;

  [[nodiscard]] const TempoLane* tempo() const noexcept {
    return tempo_ ? &*tempo_ : nullptr;
  }

  [[nodiscard]] bool operator==(const NodeTimeline&) const = default;

 private:
  explicit NodeTimeline(MeasureMap measures);

  // Rebuilds the current tempo lane against `new_end`, reusing its points
  // and start. Returns std::nullopt if the existing points no longer
  // satisfy TempoLane::create's structural rules against `new_end`.
  // Precondition: tempo_.has_value().
  [[nodiscard]] std::optional<TempoLane> rebuild_tempo_for_end(
      Rational new_end) const;

  enum class MeasureEditKind { kInsert, kDelete };

  [[nodiscard]] Result transform_absolute_lanes(MeasureEditKind kind,
                                                Rational        start,
                                                Rational        length);

  MeasureMap                            measures_;
  std::optional<Rational>               pickdown_duration_;
  std::unordered_map<StaveId, ClefLane> clef_lanes_;
  std::optional<TempoLane>              tempo_;
};

}  // namespace graphscore
