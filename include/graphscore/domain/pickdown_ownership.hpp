// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_event.hpp>

namespace graphscore {

// This header's file-level overview -- crossing-note ownership and
// pickdown MIDI-only tail material (docs/plan/README.md's locked
// decisions) -- spanning every declaration below (TiedNoteSpan through
// classify_voice_ownership()), not only the one immediately following it:
//
//   "Notes/ties crossing the transition boundary transfer their
//   post-boundary logical ownership to the pickdown tail."
//   "A sequential destination starts when the pickdown starts. Pickdown
//   notes continue concurrently on the source tempo curve, may contain
//   new attacks, emit MIDI only, and survive later active-node
//   transitions until naturally complete."
//
// node_timeline.hpp already names this file's mechanism:
// NodeTimeline::classify(const MusicalSpan&) "is the mechanism later
// phases use to give a crossing note or tie its main and tail ownership
// at the boundary" -- this header is that later phase. It does not
// duplicate classify()'s boundary-splitting logic; it only supplies what
// classify() needs but does not itself compute -- the actual sounding
// span of a logical note, resolved from a voice's raw VoiceEvent
// sequence by following its tie chain -- and packages the result under
// names that state the ownership semantics explicitly.
//
// -- Building a logical note's span from a tie chain --
// VoiceContent (voice_content.hpp) stores one VoiceEvent per notated
// column with no explicit position of its own; a column's absolute
// position is always the running sum of every earlier column's resolved
// duration (Duration::resolved(), duration.hpp), starting from wherever
// the voice itself starts (0/1 for every "0.1.0" voice, since there is
// no opening partial measure -- measure_map.hpp -- though this file
// accepts an explicit `start` rather than hard-coding 0/1, so it composes
// cleanly with any future multi-region voice layout). A single sounding
// Note, or a single notehead within a Chord (chords tie note-by-note --
// notation_event.hpp's Chord/ChordNote comment -- so two noteheads in the
// same chord column may belong to different, differently-lengthed tie
// chains), may extend across several columns via tied_to_next. This
// file's job is to walk that chain and produce ONE MusicalSpan per
// logical note/notehead, from the column where it first sounds through
// the last column its chain ties into, mirroring the chain validate_ties()
// (notation_event.hpp) already checks for structural validity. A Rest
// never begins or extends a chain -- "a Rest owns nothing" -- and any
// active tie not carried into the following column (because that column
// is a Rest, or does not sound the tied pitch) simply ends its chain at
// the column that set tied_to_next, exactly as if that flag had been
// false; this file performs no validation of its own and never fails on
// malformed input -- notation_validation.hpp's referential validator is
// the place that flags a broken tie as a diagnostic. Callers that need
// clean input should run that validator first. If a single column
// carries two or more noteheads/active ties sharing the identical pitch
// (unusual, invalid-sounding music the model does not otherwise forbid),
// the first match found in scan order is the one extended; this is a
// pragmatic, deterministic tie-break, not a claim that such a column is
// well-formed.
//
// -- Ownership --
// Once a chain's full MusicalSpan is known, NoteOwnership wraps
// NodeTimeline::classify(span) under names that state its meaning for
// this file's purpose, rather than reusing SpanClassification's generic
// main_part/pickdown_part fields directly:
//   - `main_part`: the portion, if any, that plays as ordinary
//     main-region material -- unchanged behavior, still owned by
//     whichever node is currently active.
//   - `pickdown_tail_part`: the portion, if any, that has crossed into
//     the pickdown -- "transfers ... post-boundary logical ownership to
//     the pickdown tail" for a genuinely crossing chain, or, for a chain
//     that begins fresh inside the pickdown itself ("may contain new
//     attacks"), the chain's entire span.
//
// -- MIDI-only tail material --
// is_pickdown_tail_material() is this file's explicit, queryable
// classification for "emit[s] MIDI only" (README, above): true whenever
// `pickdown_tail_part` is present, false otherwise. This is a semantic
// flag only -- the domain layer has no audio, MIDI, or plugin dependency
// (docs/plan/02-domain-model.md's own acceptance bar) -- naming which
// material a writer/runtime playback engine must render as sound-only
// (no longer the visually "active" node's own notation) and must let
// "survive later active-node transitions until naturally complete"
// rather than stopping it at the next boundary/jump; the actual clearing
// rules that DO eventually end tail ownership (vertical jump/stop/
// reset/node-play/panic/snapshot retirement) are Phase 6b-ii's
// lifecycle concern, deliberately out of scope here. The flag applies
// uniformly to both ways material ends up MIDI-only: a chain crossing
// the boundary (its `pickdown_tail_part` is the post-boundary remainder)
// and a chain that starts fresh inside the pickdown (its whole span is
// the tail, so `main_part` is nullopt and `pickdown_tail_part` covers it
// entirely) -- NodeTimeline::classify() already produces exactly that
// shape for a span wholly on one side, so no special case is needed
// here.
struct TiedNoteSpan {
  std::size_t  start_event_index = 0;
  SpelledPitch pitch;
  MusicalSpan  span;

  [[nodiscard]] bool operator==(const TiedNoteSpan&) const = default;
};

// Walks `events` -- one voice's ordered VoiceEvents, e.g.
// VoiceContent::events() -- starting at absolute position `start`, and
// returns one TiedNoteSpan per logical note/notehead: a Note or a
// Chord's individual ChordNote, extended through however many further
// columns its tied_to_next chain (Note::tied_to_next /
// ChordNote::tied_to_next) actually reaches. A Chord column with N
// noteheads that are not themselves continuations of an earlier chain
// contributes up to N entries, one per notehead, since each may tie
// independently; a Rest contributes nothing. Entries are returned in the
// order their chain begins (the order columns are scanned), not the
// order chains end.
[[nodiscard]] std::vector<TiedNoteSpan> tied_note_spans(
    const std::vector<VoiceEvent>& events, Rational start);

// One logical note/notehead's boundary ownership -- see this header's
// overview above for exactly what `main_part`/`pickdown_tail_part` mean
// and for is_pickdown_tail_material()'s exact semantics.
struct NoteOwnership {
  TiedNoteSpan               note;
  std::optional<MusicalSpan> main_part;
  std::optional<MusicalSpan> pickdown_tail_part;

  [[nodiscard]] bool is_pickdown_tail_material() const noexcept {
    return pickdown_tail_part.has_value();
  }

  [[nodiscard]] bool operator==(const NoteOwnership&) const = default;
};

// Classifies one already-resolved `note` (typically produced by
// tied_note_spans() above) against `timeline`'s main-region/pickdown
// boundary; a thin, explicitly named wrapper over
// NodeTimeline::classify(const MusicalSpan&) (node_timeline.hpp) -- see
// this header's overview for why no boundary-splitting logic is
// duplicated here.
[[nodiscard]] NoteOwnership classify_note_ownership(
    const NodeTimeline& timeline, const TiedNoteSpan& note);

// Convenience composition of tied_note_spans() and
// classify_note_ownership() for a whole voice at once: every logical
// note/notehead in `events`, starting at `start`, with its boundary
// ownership already resolved against `timeline`.
[[nodiscard]] std::vector<NoteOwnership> classify_voice_ownership(
    const NodeTimeline& timeline, const std::vector<VoiceEvent>& events,
    Rational start);

}  // namespace graphscore
