// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_fragment.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

// Where a paste writes: a node, a track, a stave, and a musical-time
// position. Voice is deliberately NOT part of the anchor -- a fragment
// part's voice is preserved exactly at the destination (fragment voice V
// always writes destination voice V), since a stave's four voices are a
// fixed set, not a sequence to be remapped by ordinal.
struct PasteAnchor {
  NodeId   node;
  TrackId  track;
  StaveId  stave;
  Rational position;

  [[nodiscard]] bool operator==(const PasteAnchor&) const = default;
};

// Pastes a NotationFragment (see notation_fragment.hpp) into a live Project
// at `anchor`, reversibly.
//
// Ordinal mapping ("anchor + project order", locked): a fragment part's
// (track_ordinal, stave_ordinal) maps onto the destination as follows.
//
// Stave ordinals are compacted by distinct referenced ordinals per track:
// the smallest referenced source stave ordinal maps to the anchor stave
// (for track_ordinal 0) or to that track's first stave (for later tracks);
// each further distinct referenced ordinal maps to the next destination
// stave in layout order within that track.  Voices sharing an ordinal
// share a single destination stave.  Overflow counts distinct referenced
// staves only -- a fragment naming only stave_ordinal 1 on a track
// consumes one destination stave, not two.
//
// Increasing track_ordinal walks forward through the project's active
// tracks in ascending Track::index() order starting at the anchor track.
// Each track's stave_ordinal 0 is that track's own first stave -- not the
// anchor stave's offset (only track_ordinal 0 inherits the anchor's stave
// offset). Voice is copied unchanged (fragment voice V always writes
// destination voice V). If mapping any (track_ordinal, stave_ordinal)
// actually referenced by the fragment would require more active tracks, or
// more staves within a destination track, than exist, execute() fails
// kInvalidArgument and the project is left unchanged.
//
// Overflow ("paste must fit", locked): the destination range is
// [anchor.position, anchor.position + fragment.span_length()). execute()
// fails kInvalidArgument, leaving the project unchanged, if
// anchor.position is negative or if the range's end exceeds the
// destination node's NodeTimeline::node_end(). Paste never grows the node
// -- MeasureMap gains mutators only in a later increment (8h).
//
// Reconnection (the paste-side half of notation_fragment.hpp's R1-R12):
// per destination voice actually referenced by a fragment part, the voice
// is rebuilt wholesale as three concatenated regions: (1) destination
// events entirely before anchor.position, with a sounding event straddling
// the left boundary truncated at that boundary with its original attack
// preserved — its musical content (pitch/noteheads/articulations/stem) is
// kept, its outgoing tie is severed, and if the retained span decomposes
// into multiple durations the chain carries valid internal ties; (2) the
// fragment part's events, with every event id, ChordNote::id, and
// GraceNote::id freshly regenerated; (3) destination events entirely after
// the range's end, with a sounding event straddling the right boundary
// having its in-range portion converted to normalized rests (attack was
// inside the replaced range, so no partial note is preserved). An event
// whose onset lies exactly on either boundary is never straddling and is
// handled as wholly before/after the range. A destination tuplet event
// straddling either boundary fails the whole paste with kInvalidArgument,
// leaving the project unchanged — same reasoning as copy-side R4. Only
// voices actually named by a fragment part are touched: an
// ArbitraryRangeSet-derived fragment may carry fewer than four voices per
// stave, and every other voice of that stave is left completely
// unmodified.
//
// Destination markings (dynamics, hairpins, slurs, beam overrides, grace
// groups) anchored to an event the paste removed are dropped -- they would
// otherwise dangle. A hairpin or slur with one endpoint inside the pasted
// range and the other outside is dropped entirely rather than clipped: its
// interior meaning no longer holds once the interior is replaced. Every
// marking surviving from the destination keeps its original id and
// referenced ids unchanged; every marking carried by the fragment is added
// with a freshly regenerated id, re-pointed at the freshly regenerated
// destination ids of its own referenced events.
//
// Pedal spans (TrackLane-scoped, not per-voice) intersecting the
// destination range are clipped to the portion(s) outside the range (a
// span entirely inside the range is removed outright; a span entirely
// outside the range is left completely untouched, including its id).
// Fragment pedal spans are added at anchor.position-relative offsets with
// freshly regenerated ids.
//
// Clef changes are NOT applied by this increment. The fragment carries
// them (NotationFragment::clef_changes()), and ClefLane and MeasureMap now
// have the container-level mutators an application would need -- add_change,
// remove_change, move_change, set_change (Phase 8h-i) -- but wiring paste
// to actually apply a fragment's clef and signature context to the
// destination is command-level work this increment does not own: Phase
// 8h-iv wires paste to apply copied clef and signature context, once
// 8h-ii/8h-iii land the clef-change and time/key-signature commands those
// applications compose with. Until then, measure_contexts()/
// stave_contexts()/clef_changes() remain informational only, never
// applied to the destination.
//
// Reversibility: whole-lane snapshot, the 8d-iv/8e-i precedent. Every
// TrackLane the paste actually touches is snapshotted by value before any
// mutation and restored on undo; undo/redo reject stale context (the live
// lane no longer matching the expected snapshot) with kInvalidArgument
// rather than blindly restoring, and remain retryable afterward.
class PasteFragmentCommand : public Command {
 public:
  PasteFragmentCommand(NotationFragment fragment, PasteAnchor anchor)
      : fragment_(std::move(fragment)), anchor_(anchor) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NotationFragment fragment_;
  PasteAnchor      anchor_;

  std::optional<std::vector<std::pair<TrackId, TrackLane>>> pre_snapshot_;
  std::optional<std::vector<std::pair<TrackId, TrackLane>>> post_snapshot_;
  State state_ = State::kFresh;
};

}  // namespace graphscore
