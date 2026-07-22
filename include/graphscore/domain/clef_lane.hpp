// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// One clef change within a stave's clef-change lane: the exact whole-note
// position (see measure_map.hpp for the canonical whole-note position
// unit), measured from the start of the node's main region, at which the
// stave's notated clef changes.
struct ClefChange {
  Rational position;
  Clef     clef = Clef::kTreble;

  [[nodiscard]] bool operator==(const ClefChange&) const = default;
};

// A single stave's clef-change lane.
//
// Clef is a per-stave attribute, unlike time/key signatures: a grand staff
// shows two clefs simultaneously, so clef changes cannot be shared across a
// node's tracks the way measure time/key signatures are. Each stave in a
// node therefore owns an independent ClefLane, starting from its
// StaveDefinition default clef, kept in a NodeTimeline collection keyed by
// StaveId (mirroring the existing Node lane collection keyed by TrackId).
class ClefLane {
 public:
  explicit ClefLane(Clef default_clef) noexcept : default_clef_(default_clef) {}

  [[nodiscard]] Clef default_clef() const noexcept { return default_clef_; }

  // Records a clef change at `position`. Fails if `position` is negative or
  // a change is already recorded at that exact position.
  [[nodiscard]] Result add_change(Rational position, Clef clef);

  [[nodiscard]] const std::vector<ClefChange>& changes() const noexcept {
    return changes_;
  }

  // The clef in effect at `position`: the most recent change at or before
  // `position`, or default_clef() if none has occurred yet.
  [[nodiscard]] Clef clef_at(Rational position) const;

  [[nodiscard]] bool operator==(const ClefLane&) const = default;

 private:
  Clef                    default_clef_;
  std::vector<ClefChange> changes_;
};

}  // namespace graphscore
