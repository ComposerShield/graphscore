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
//
// No ClefChange can ever exist at a negative position: add_change and
// move_change are the only paths that insert into changes_, and both
// reject a negative position outright (add_change rejects position,
// move_change rejects to); remove_change and set_change never write a
// position at all. remove_change/set_change therefore need no
// negative-position check of their own -- a lookup at a negative position
// necessarily misses. This is a structural invariant of this class, so a
// future deserializing constructor (e.g. Milestone 03 persistence) that
// populates changes_ by any other path must preserve it itself.
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

  // Removes the change recorded at exactly `position`. Fails with
  // kInvalidArgument if no change is recorded there, leaving the lane
  // unchanged.
  [[nodiscard]] Result remove_change(Rational position);

  // Moves the change recorded at exactly `from` to `to`, preserving its
  // clef. Fails with kInvalidArgument if no change is recorded at `from`,
  // if `to` is negative, or if a change is already recorded at `to`
  // (checked after the `from` entry is notionally removed, so moving a
  // change to its own position is not a self-conflict). Leaves the lane
  // unchanged on failure.
  [[nodiscard]] Result move_change(Rational from, Rational to);

  // Replaces the clef of the change recorded at exactly `position`. Fails
  // with kInvalidArgument if no change is recorded there, leaving the
  // lane unchanged. Unlike add_change, this requires an existing change
  // at `position` rather than rejecting one.
  [[nodiscard]] Result set_change(Rational position, Clef clef);

  // The clef in effect at `position`: the most recent change at or before
  // `position`, or default_clef() if none has occurred yet.
  [[nodiscard]] Clef clef_at(Rational position) const;

  [[nodiscard]] bool operator==(const ClefLane&) const = default;

 private:
  Clef                    default_clef_;
  std::vector<ClefChange> changes_;
};

}  // namespace graphscore
