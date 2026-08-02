// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// One measure of a node's shared main-region timeline: the time signature
// and key signature in effect for that measure, both shared by every track
// in the node (product decisions: "Time signatures may change per measure
// and are shared by all tracks in a node"; standard key signatures change
// per measure and are shared the same way).
struct Measure {
  TimeSignature time_signature;
  KeySignature  key_signature;

  [[nodiscard]] bool operator==(const Measure&) const = default;
};

// The ordered main-region measure map every active track in a node shares.
//
// Canonical musical position/duration unit: exact whole notes, expressed as
// a Rational (Rational(1) == one whole note). Every position and duration
// on the node timeline — measure starts/lengths, clef changes, the
// pickdown boundary, tempo-point positions — is measured this way, never as
// floating point. Position 0/1 is always the start of a complete measure:
// the `0.1.0` model has no opening partial/pickup measure.
class MeasureMap {
 public:
  MeasureMap() = delete;

  // Fails if `measures` is empty: a node's main region requires at least
  // one complete measure.
  [[nodiscard]] static std::optional<MeasureMap> create(
      std::vector<Measure> measures);

  [[nodiscard]] std::size_t measure_count() const noexcept {
    return measures_.size();
  }

  // Precondition: index < measure_count().
  [[nodiscard]] const Measure& measure(std::size_t index) const;

  // Exact whole-note length of measure `index`: its time signature's
  // numerator/denominator, e.g. 6/8 is Rational(6, 8) == 3/4 of a whole
  // note. Precondition: index < measure_count().
  [[nodiscard]] Rational measure_length(std::size_t index) const;

  // Exact whole-note position of the start of measure `index`, measured
  // from the start of the node's main region. Precondition:
  // index < measure_count().
  [[nodiscard]] Rational measure_start(std::size_t index) const;

  // The index of the measure containing `position`, or nullopt if
  // `position` is negative or at/past the end of the main region.
  [[nodiscard]] std::optional<std::size_t> measure_index_at(
      Rational position) const;

  // Exact whole-note length of the entire main region: the position just
  // past its last measure, i.e. the main-region/pickdown boundary.
  [[nodiscard]] Rational total_length() const;

  // Inserts `measure` at `index`, shifting every measure currently at or
  // after `index` one position later. `index` must be in
  // [0, measure_count()]; `index == measure_count()` appends. Fails with
  // kInvalidArgument if `index` is out of range, leaving the map
  // unchanged. Always changes total_length(), and, when
  // `index == measure_count()`, also changes the boundary meter to
  // `measure`'s time signature; the inheriting overload cannot, since it
  // copies the current last measure's signature. A NodeTimeline-level
  // caller must revalidate pickdown duration and rebuild the tempo lane
  // after this call, the same obligation remove_measure and set_measure
  // carry below.
  [[nodiscard]] Result insert_measure(std::size_t index, Measure measure);

  // Inserts a new measure at `index`, inheriting the time and key
  // signature of the measure currently at `index`. When
  // `index == measure_count()` (append), the new measure inherits the
  // signature of the current last measure instead. Fails under the same
  // index bound as the two-argument overload, leaving the map unchanged.
  // Always changes total_length(); carries the same revalidate-pickdown/
  // rebuild-tempo-lane obligation as the two-argument overload above.
  [[nodiscard]] Result insert_measure(std::size_t index);

  // Removes the measure at `index`, shifting every later measure one
  // position earlier. Fails with kInvalidArgument if `index` is out of
  // range, or if `index` names the map's only remaining measure --
  // removing it would leave the main region with no complete measure,
  // the same invariant create() enforces on construction. The map is
  // unchanged on failure. Always changes total_length(), and when
  // `index` names the last measure also changes the boundary meter (the
  // time signature governing the pickdown region). A NodeTimeline-level
  // caller must revalidate pickdown duration (the bound set_pickdown
  // enforces) and rebuild the tempo lane (whose validated end tracks
  // node_end()) after this call; this class has no pickdown or tempo
  // knowledge of its own and does neither.
  [[nodiscard]] Result remove_measure(std::size_t index);

  // Replaces the time and key signature of the measure at `index` in
  // place; the measure count is unchanged. Fails with kInvalidArgument if
  // `index` is out of range, leaving the map unchanged. Changes
  // total_length() whenever the replacement's time signature has a
  // different length, and when `index` names the last measure also
  // changes the boundary meter. Carries the same revalidate-pickdown/
  // rebuild-tempo-lane obligation as remove_measure above.
  [[nodiscard]] Result set_measure(std::size_t index, Measure measure);

  [[nodiscard]] bool operator==(const MeasureMap&) const = default;

 private:
  explicit MeasureMap(std::vector<Measure> measures);

  std::vector<Measure>  measures_;
  std::vector<Rational> starts_;
  Rational              total_length_;
};

}  // namespace graphscore
