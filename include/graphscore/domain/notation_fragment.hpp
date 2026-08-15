// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// One track's shape within the fragment: how many staves it contributes.
// A fragment never stores TrackId/StaveId directly (that would retain
// source identity across a paste into a different track); every reference
// elsewhere in the fragment is an ordinal into tracks() (and, for a stave,
// an ordinal into the referenced track's own stave_count).
struct FragmentTrackShape {
  std::size_t stave_count = 0;

  [[nodiscard]] bool operator==(const FragmentTrackShape&) const = default;
};

// One copied voice, relative to the fragment origin (the copied span's
// start position). `content` exactly tiles [0, span_length) of the owning
// NotationFragment -- it is a fully self-contained VoiceContent, complete
// with its own dynamics/hairpins/slurs/beam-overrides/grace-groups already
// clipped and re-pointed at fragment-local ids (see extract_fragment).
struct FragmentVoicePart {
  std::size_t  track_ordinal = 0;
  std::size_t  stave_ordinal = 0;
  Voice        voice;
  VoiceContent content;

  [[nodiscard]] bool operator==(const FragmentVoicePart&) const = default;
};

// A sustain-pedal span intersected with the copied range, positioned
// relative to the fragment origin (see PedalSpan; pedal is stave-scoped,
// not per-voice, per TrackLane::pedal_spans).
struct FragmentPedalSpan {
  std::size_t track_ordinal = 0;
  std::size_t stave_ordinal = 0;
  Rational    start;
  Rational    end;

  [[nodiscard]] bool operator==(const FragmentPedalSpan&) const = default;
};

// A clef change strictly inside the copied range, positioned relative to
// the fragment origin (see ClefChange). The clef in effect *at* the
// origin is recorded separately as FragmentStaveContext::clef_at_origin,
// not as a change at relative position 0.
struct FragmentClefChange {
  std::size_t track_ordinal = 0;
  std::size_t stave_ordinal = 0;
  Rational    position;
  Clef        clef = Clef::kTreble;

  [[nodiscard]] bool operator==(const FragmentClefChange&) const = default;
};

// Informational per-stave context captured at the fragment origin: the
// clef in effect there (ClefLane::clef_at(origin), or the stave's
// StaveDefinition::default_clef if the source node has no clef lane for
// it). Emitted once per stave that appears in the fragment.
struct FragmentStaveContext {
  std::size_t track_ordinal  = 0;
  std::size_t stave_ordinal  = 0;
  Clef        clef_at_origin = Clef::kTreble;

  [[nodiscard]] bool operator==(const FragmentStaveContext&) const = default;
};

// Informational measure-structure context: the measure containing the
// fragment origin (at relative position 0) and every subsequent measure
// start strictly inside the copied range. Node-wide, not per track/stave
// (time/key signatures are shared by every track in a node). Paste (a
// later increment) never rewrites destination measure structure from
// this; it exists for editor display only.
//
// If the fragment origin lies in the pickdown region (at or past
// MeasureMap::total_length(), where MeasureMap::measure_index_at returns
// std::nullopt), there is no containing measure to report; the position-0
// context instead carries the last main-region measure's time and key
// signature, since the pickdown is by construction shorter than one
// measure under that governing meter (see NodeTimeline::set_pickdown).
// There is always exactly one context at relative position 0.
struct FragmentMeasureContext {
  Rational      position;
  TimeSignature time_signature;
  KeySignature  key_signature;

  [[nodiscard]] bool operator==(const FragmentMeasureContext&) const = default;
};

// A toolkit-independent, source-identity-free snapshot of a copied
// musical-time range: relative positions, staff/voice mapping by ordinal,
// and every notation entity that fits inside it, clipped deterministically
// at its boundaries. Never retains a source NodeId/TrackId/StaveId or any
// source NotationEntityId -- see extract_fragment's identity rules.
//
// Clipping rules (locked; extract_fragment is the only producer of a
// NotationFragment from a live Project, and applies these exactly):
//
//   R1 -- head-straddling sounding event (onset < origin, end > origin):
//     not copied as a sounding event. Its in-range portion
//     [origin, min(end, span_end)) becomes plain rests
//     (voice_content.hpp's decompose_rest). No attack is invented at the
//     origin, because the source attack lies outside the copied range.
//
//   R2 -- tail-straddling sounding event (origin <= onset < span_end,
//     end > span_end): copied, truncated to [onset, span_end). The
//     truncated length is decomposed into plain durations; the first
//     duration carries the full note/chord content (pitch, noteheads,
//     articulations, stem), every later duration is a tied continuation
//     carrying only the same pitch content (no articulations).
//     tied_to_next is true on every piece but the last; false on the
//     last, severing the tie that would otherwise reach past the
//     fragment's end.
//
//   R3 -- a rest overlapping either boundary is clipped to the
//     intersection and re-decomposed into plain rests.
//
//   R4 -- a stable tuplet group (any mix of Note, Chord, or Rest) is copied
//     verbatim only when the complete group is wholly contained in the copied
//     range. A range containing only part of a group fails the whole
//     extraction: nested/renormalized tuplets are out of scope, and a
//     deterministic failure is preferable to silently corrupting the tuplet's
//     rhythm. This precedes R1-R3: a straddling tuplet event never becomes a
//     head/tail-straddle rest/tie chain. The copied group receives one fresh
//     TupletGroupId shared by all of its copied members.
//
//   R5 -- ties on events fully inside the copied range (contained plain
//     or tuplet copies) keep their source tied_to_next state, except the
//     very last sounding event actually produced for a voice part always
//     has tied_to_next forced false, so no fragment voice part can tie
//     past its own end.
//
//   R6 -- Slur/Hairpin: let S be the ordered (by source position) list of
//     sounding events that actually survive into the fragment for that
//     voice. new_start is the first element of S at or after the source
//     start event's position; new_end is the last element of S at or
//     before the source end event's position. The span is dropped if
//     either endpoint is missing, or if new_start and new_end resolve to
//     the same (or an out-of-order) surviving event; otherwise it is
//     copied re-pointed at those two surviving events.
//
//   R7 -- BeamOverride: its `events` list is filtered to the referenced
//     events that survive (each mapped to its first surviving piece,
//     preserving original relative order); dropped if fewer than two
//     remain.
//
//   R8 -- DynamicMarking: copied, re-pointed at its at_event's first
//     surviving piece, iff at_event survives; dropped otherwise.
//
//   R9 -- GraceGroup: copied, re-pointed at its principal_event's first
//     surviving piece, iff principal_event survives; dropped otherwise.
//     Every GraceNote::id is regenerated.
//
//   R10 -- PedalSpan (TrackLane::pedal_spans, stave-scoped): intersected
//     with the copied range and stored relative to its origin; dropped
//     if the intersection is empty or zero-length. Emitted only for
//     staves that appear in the fragment.
//
//   R11 -- ClefChange strictly inside the copied range is copied with a
//     relative position. The clef in effect at the origin is never
//     emitted as a synthetic change at relative position 0; it is
//     recorded once per stave as FragmentStaveContext::clef_at_origin.
//
//   R12 -- Measure structure is informational only: a FragmentMeasureContext
//     for the measure containing the origin (at relative position 0) and
//     for every later measure start strictly inside the copied range. If
//     the origin lies in the pickdown region, the relative-0 context
//     instead carries the last main-region measure's time and key
//     signature (see FragmentMeasureContext); there is always exactly one
//     context at relative position 0.
//
// Identity rule: every id inside a NotationFragment -- every top-level
// event id, every embedded ChordNote::id and GraceNote::id, and every
// marking id -- is freshly generated during extraction and therefore
// never collides with any id in the source project or with any other id
// in the fragment itself; create() enforces this.
class NotationFragment {
 public:
  NotationFragment() = delete;

  // Validates and (on success) sorts every collection into its documented
  // deterministic order. Fails, returning std::nullopt, if:
  //   - span_length is not strictly positive;
  //   - tracks or parts is empty;
  //   - any (track_ordinal, stave_ordinal) reference (in parts,
  //     pedal_spans, clef_changes, or stave_contexts) is out of range for
  //     `tracks`;
  //   - two parts share the same (track_ordinal, stave_ordinal, voice);
  //   - any part's content does not exactly tile [0, span_length)
  //     (VoiceContent::check_complete) or fails VoiceContent::validate();
  //   - any pedal span does not satisfy 0 <= start < end <= span_length;
  //   - any clef change's position does not satisfy
  //     0 <= position < span_length;
  //   - any measure context's position does not satisfy
  //     0 <= position < span_length;
  //   - any id (event, ChordNote, GraceNote, or marking) appears more
  //     than once anywhere in the fragment;
  //   - two stave_contexts share the same (track_ordinal, stave_ordinal);
  //   - two clef_changes share the same (track_ordinal, stave_ordinal,
  //     position);
  //   - two measure_contexts share the same position;
  //   - two pedal_spans share the same (track_ordinal, stave_ordinal,
  //     start).
  // These duplicate checks reject mutually exclusive statements about the
  // same coordinate (e.g. two different clefs both claimed to be in
  // effect at the same instant), which a defaulted operator== combined
  // with an unstable sort could otherwise let through non-deterministically.
  [[nodiscard]] static std::optional<NotationFragment> create(
      Rational span_length, std::vector<FragmentTrackShape> tracks,
      std::vector<FragmentVoicePart>      parts,
      std::vector<FragmentPedalSpan>      pedal_spans,
      std::vector<FragmentClefChange>     clef_changes,
      std::vector<FragmentStaveContext>   stave_contexts,
      std::vector<FragmentMeasureContext> measure_contexts);

  [[nodiscard]] Rational span_length() const noexcept { return span_length_; }

  [[nodiscard]] const std::vector<FragmentTrackShape>& tracks() const noexcept {
    return tracks_;
  }

  // Sorted by (track_ordinal, stave_ordinal, voice).
  [[nodiscard]] const std::vector<FragmentVoicePart>& parts() const noexcept {
    return parts_;
  }

  // Sorted by (track_ordinal, stave_ordinal, start).
  [[nodiscard]] const std::vector<FragmentPedalSpan>& pedal_spans()
      const noexcept {
    return pedal_spans_;
  }

  // Sorted by (track_ordinal, stave_ordinal, position).
  [[nodiscard]] const std::vector<FragmentClefChange>& clef_changes()
      const noexcept {
    return clef_changes_;
  }

  // Sorted by (track_ordinal, stave_ordinal).
  [[nodiscard]] const std::vector<FragmentStaveContext>& stave_contexts()
      const noexcept {
    return stave_contexts_;
  }

  // Sorted by position.
  [[nodiscard]] const std::vector<FragmentMeasureContext>& measure_contexts()
      const noexcept {
    return measure_contexts_;
  }

  [[nodiscard]] bool operator==(const NotationFragment&) const = default;

 private:
  NotationFragment(Rational span_length, std::vector<FragmentTrackShape> tracks,
                   std::vector<FragmentVoicePart>      parts,
                   std::vector<FragmentPedalSpan>      pedal_spans,
                   std::vector<FragmentClefChange>     clef_changes,
                   std::vector<FragmentStaveContext>   stave_contexts,
                   std::vector<FragmentMeasureContext> measure_contexts);

  Rational                            span_length_;
  std::vector<FragmentTrackShape>     tracks_;
  std::vector<FragmentVoicePart>      parts_;
  std::vector<FragmentPedalSpan>      pedal_spans_;
  std::vector<FragmentClefChange>     clef_changes_;
  std::vector<FragmentStaveContext>   stave_contexts_;
  std::vector<FragmentMeasureContext> measure_contexts_;
};

// The result of extract_fragment: either a successful Result with an
// engaged fragment, or a failing Result with std::nullopt.
struct FragmentExtraction {
  Result                          status;
  std::optional<NotationFragment> fragment;
};

// Extracts a toolkit-independent NotationFragment for `selection` from
// `project`. A pure, non-mutating query: `project` is never modified,
// even on failure.
//
// Accepted selection arms: FullMeasureSet and ArbitraryRangeSet only;
// every other arm fails with kInvalidArgument and no fragment. This is a
// whitelist, so a new Selection arm is rejected until it is deliberately
// supported. RestSet and MarkingSet are rejected on purpose rather than
// pending: a fragment is a contiguous musical-time span with rhythmically
// complete voices, whereas both of those name individual, possibly
// discontiguous entities. Copying a run of rests is expressed as the
// ArbitraryRangeSet covering them; a marking has no time span of its own
// to paste into.
//
// Preconditions (each failing kInvalidArgument, leaving no fragment):
//   - validate_selection(project, selection) returns no diagnostics.
//   - Every item shares one NodeId.
//   - FullMeasureSet: every item shares one measure_index; the copied
//     span is [measure_start(index), measure_start(index) +
//     measure_length(index)); all four voices of each selected stave are
//     copied -- an unpopulated or shorter-than-span voice is rest-filled
//     to the span, not rejected (a stave normally holds four voices of
//     which only one is written).
//   - ArbitraryRangeSet: every item has an identical span, and that span
//     satisfies 0 <= start < end <= NodeTimeline::node_end(); only the
//     named (stave, voice) pairs are copied.
//
// Track ordinals are assigned by the distinct TrackIds present in the
// selection, sorted ascending by Track::index(). Stave ordinals are the
// index of each item's StaveId within that track's layout().staves().
// The fragment origin is the copied span's start; every fragment position
// is source-absolute minus that origin.
//
// Also fails with kInvalidArgument if a clipping rule above cannot be
// satisfied (e.g. voice_content.hpp's decompose_rest fails to represent a
// clipped span, or a tuplet event straddles a boundary), or if the
// resulting collections fail NotationFragment::create.
[[nodiscard]] FragmentExtraction extract_fragment(const Project&   project,
                                                  const Selection& selection);

}  // namespace graphscore
