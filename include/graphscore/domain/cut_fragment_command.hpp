// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_fragment.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

// Cuts `selection` out of a live Project, reversibly: extracts a
// NotationFragment for the clipboard (see notation_fragment.hpp's
// extract_fragment) and, in the same operation, replaces the selected
// musical-time range from every selected voice with normalized rests.
// Boundary-reconnection rules match PasteFragmentCommand (see
// paste_fragment_command.hpp): a destination event straddling the left
// (earlier) boundary is truncated at that boundary with its original attack
// preserved and its outgoing tie severed; a destination event straddling
// the right (later) boundary has its in-range portion converted to
// normalized rests (attack was inside the cut range). A straddling tuplet
// fails the whole cut with kInvalidArgument, leaving the project unchanged.
// Only the selected (track, stave, voice) triples are touched — for a
// FullMeasureSet selection that is every voice of each selected stave; for
// an ArbitraryRangeSet selection that is exactly the named (stave, voice)
// pairs.
//
// Markings anchored to a removed event are dropped, exactly as
// PasteFragmentCommand documents. Pedal spans (TrackLane-scoped)
// intersecting the cut range are clipped to their portion(s) outside the
// range; a span entirely inside is removed; a span entirely outside is
// left completely untouched.
//
// On first successful execute(), fragment() becomes engaged with the
// extracted NotationFragment -- the caller places it on the clipboard.
// redo() re-applies the same range-clearing from the command's own
// snapshots; it never re-calls extract_fragment.
//
// Reversibility: whole-lane snapshot, the same 8d-iv/8e-i precedent
// PasteFragmentCommand uses. Every TrackLane the cut actually touches is
// snapshotted by value before any mutation and restored on undo; undo/redo
// reject stale context with kInvalidArgument rather than blindly
// restoring, and remain retryable afterward. If extraction itself fails,
// execute() returns that failure and the project is left unchanged (no
// lane is ever snapshotted or mutated).
class CutFragmentCommand : public Command {
 public:
  explicit CutFragmentCommand(Selection selection)
      : selection_(std::move(selection)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

  [[nodiscard]] const std::optional<NotationFragment>& fragment()
      const noexcept {
    return fragment_;
  }

 private:
  Selection                       selection_;
  std::optional<NotationFragment> fragment_;
  std::optional<NodeId>           node_id_;

  std::optional<std::vector<std::pair<TrackId, TrackLane>>> pre_snapshot_;
  std::optional<std::vector<std::pair<TrackId, TrackLane>>> post_snapshot_;
  State state_ = State::kFresh;
};

}  // namespace graphscore
