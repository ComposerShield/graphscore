// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/clef_lane.hpp>
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
// destination node's NodeTimeline::node_end(). Paste never grows the node --
// MeasureMap contents are never rewritten by a paste; see the meter
// compatibility gate below for the one place a paste's meaning depends on
// the destination's time signature.
//
// Meter compatibility gate (locked, Phase 8h-iv): time and key signatures
// live on Measure inside MeasureMap, per-measure and node-wide -- shared by
// every one of a node's tracks, not stave-scoped like clef. Applying a
// copied time signature would change a measure's length, hence node_end(),
// colliding with "paste never grows the node" above and shifting every
// track's later material, contradicting "modify no music outside the
// destination range". Paste therefore never applies a time signature, and
// instead validates compatibility before touching anything: the set of
// distinct TimeSignature values across fragment.measure_contexts() and the
// set of distinct TimeSignature values of the destination measures
// overlapping [anchor.position, range_end) must each contain exactly one
// value, and those two values must be equal, or execute() fails
// kInvalidArgument with the project completely unmutated. A destination
// range extending into the pickdown region (where MeasureMap has no
// containing measure) is governed there by the last main-region measure's
// time signature, matching NotationFragment's own pickdown-origin
// extraction rule. Same-meter paste -- including non-measure-aligned paste
// -- is unaffected; cross-meter paste, which previously succeeded while
// silently misaligning barlines, now fails loudly. A caller wanting a
// meter change composes SetMeasureTimeSignatureCommand with
// PasteFragmentCommand in one CommandTransaction. Key signature
// (FragmentMeasureContext::key_signature) is never applied and never
// validated: pitches are stored absolutely, so a key difference changes no
// sounding content, and silently re-keying a node-wide measure across
// every track would be a worse outcome than ignoring it.
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
// Clef changes (locked, Phase 8h-iv): unlike time/key signature, clef is
// per-stave, not node-wide, so applying it does not touch any other
// track. Every entry of fragment.clef_changes() is applied at
// anchor.position + change.position, on the destination stave the entry's
// (track_ordinal, stave_ordinal) resolves to through the same paste
// mapping used for voice/pedal reconnection above -- there is no second
// mapping path. fragment.stave_contexts()/clef_at_origin is NEVER applied:
// the destination stave's own clef correctly governs display of
// absolutely-stored pitches (8g-ii's clef-independent pitch invariance),
// so clef_at_origin remains a reference value only. Replace, don't
// interleave: on every stave the paste's own reconnection touches --
// every stave a fragment voice part or pedal span actually maps onto, the
// same set used above, unioned with every stave a fragment clef change
// names -- existing destination clef changes already inside
// [anchor.position, range_end) are removed before that stave's own
// fragment changes (if any) are added. This holds even when the fragment
// names no clef change at all for a stave whose notes it wholly replaces:
// a stale destination clef change stranded inside the overwritten range
// does not survive just because the fragment is silent about that stave's
// clef. A clef change naming a stave no voice part or pedal span
// references is likewise never dropped -- it resolves through the same
// paste mapping as everything else. Containment: each affected stave's
// prevailing clef at range_end is captured from the pre-edit lane before
// any change, and re-asserted with a clef change at range_end if and only
// if the post-edit prevailing clef there would otherwise differ --
// skipped when range_end equals node_end() (no later music to protect) or
// when a destination clef change already sits exactly at range_end. This
// proves the paste alters no notation after its own range. A stave whose
// resulting clef lane would be unchanged from its pre-edit state gains
// neither a snapshot nor a newly created lane. A destination stave with no
// clef lane yet (e.g. one on a track added after the node's timeline was
// created) gains one via NodeTimeline::create_clef_lane on first use and
// loses it again via NodeTimeline::remove_clef_lane if the paste is
// undone; a stave that already has a lane is updated in place via
// NodeTimeline::restore_clef_lane. Every clef-lane creation this
// increment performs happens only after every other fallible step of the
// paste has already succeeded, so a creation failure (out of memory) still
// leaves the whole paste, including every other stave's clef lane,
// completely unmutated.
//
// Reversibility: whole-lane snapshot, the 8d-iv/8e-i precedent, extended to
// clef lanes. Every TrackLane the paste actually touches is snapshotted by
// value before any mutation and restored on undo; every stave whose clef
// lane the paste actually changes is likewise snapshotted -- as
// std::optional<ClefLane>, where an empty optional means "this stave had
// no clef lane before the paste" -- and restored (or removed, if it had
// none) on undo. Undo/redo reject stale context (the live lane, TrackLane
// or ClefLane, no longer matching the expected snapshot) with
// kInvalidArgument rather than blindly restoring, and remain retryable
// afterward.
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

  // Per stave whose clef lane the paste actually changes, matched by index
  // between the two vectors (mirroring pre_snapshot_/post_snapshot_ above).
  // clef_pre_snapshot_[i].second is std::nullopt iff that stave had no
  // clef lane before execute(); clef_post_snapshot_[i].second is always
  // engaged (execute() creates a lane on first use).
  std::optional<std::vector<std::pair<StaveId, std::optional<ClefLane>>>>
                                                           clef_pre_snapshot_;
  std::optional<std::vector<std::pair<StaveId, ClefLane>>> clef_post_snapshot_;

  State state_ = State::kFresh;
};

}  // namespace graphscore
