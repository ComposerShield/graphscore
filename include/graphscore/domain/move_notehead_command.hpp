// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Which direction a selected notehead moves one diatonic staff step.
enum class NoteheadStepDirection : std::uint8_t {
  kUp = 0,
  kDown,
};

// The target spelling one diatonic staff step `direction` above/below
// `pitch`, preserving its exact accidental. Octave wrapping follows
// scientific pitch notation (B -> C upward increments the octave, C -> B
// downward decrements it). Returns std::nullopt when the step would leave
// SpelledPitch's valid octave range or would not resolve to a sounding MIDI
// pitch. This is the single shared source of the step MoveNoteheadCommand
// applies and graphscore_notation::audition_for_notehead_move auditions, so
// the audited pitch can never drift from the pitch the command produces.
[[nodiscard]] std::optional<SpelledPitch> step_notehead_pitch(
    const SpelledPitch& pitch, NoteheadStepDirection direction);

// The inclusive measure range a move of `notehead` touches (see
// notehead_move_scope below). `first_measure <= last_measure`; the two differ
// exactly when the connected tie chain crosses a measure boundary.
struct NoteheadMoveScope {
  std::size_t first_measure = 0;
  std::size_t last_measure  = 0;

  [[nodiscard]] bool operator==(const NoteheadMoveScope&) const = default;
};

// The inclusive measure range a move of `notehead` touches: the selected
// notehead's own measure plus, when it is tied, every measure its connected
// tie chain (incoming and outgoing, including cross-measure chains) spans. A
// grace notehead's scope is the measure of its principal event. This is the
// shared source of the exact NotationInvalidation scope the app layer builds
// for incremental layout after a successful move, so a cross-measure tie chain
// never leaves a remote tied notehead's retained layout stale.
//
// A pure query over the project; a pitch-only move never changes measure
// boundaries, so the pre-execution scope is the post-execution scope. Returns
// std::nullopt when `notehead` does not resolve to a Note/ChordNote/GraceNote
// in the addressed voice, when the node has no timeline, or when a chain
// member's onset falls outside the timeline's main region.
[[nodiscard]] std::optional<NoteheadMoveScope> notehead_move_scope(
    const Project& project, const NoteheadItem& notehead);

// Moves one notehead -- a top-level Note, one ChordNote inside a Chord, or one
// GraceNote inside a GraceGroup -- one diatonic staff step up or down while
// preserving its exact accidental, its event/notehead identity, and every
// unrelated event, chord, or grace field.
//
// The notehead is addressed by its persistent NotationEntityId rather than by
// a Rational position, because a NoteheadItem (graphscore/domain/selection.hpp)
// names the notehead by id and because a GraceNote has no onset of its own.
//
// Octave wrapping follows scientific pitch notation: B -> C upward increments
// the octave, and C -> B downward decrements it. The accidental is carried
// through unchanged. When the target spelling would fall outside
// SpelledPitch's valid octave range, or would not resolve to a sounding MIDI
// pitch (e.g. a double-sharp pushed past MIDI 127), execute() fails and leaves
// the project unchanged -- it never clamps, changes the accidental, or clears
// a selection.
//
// Tied noteheads move as one: the complete connected tie chain through the
// selected notehead (incoming and outgoing, including chord noteheads and
// ties that cross a measure boundary) is stepped together by the same
// diatonic step, so the tie is never severed by a move. Every chain member
// keeps its own identity and accidental. If any member cannot move (an out-of-
// range spelling or a sounding-MIDI boundary), the whole command fails
// atomically and the project is unchanged.
//
// The edit is pitch-only and index-preserving: VoiceContent is mutated through
// set_notehead_pitch, which changes only the addressed notehead(s)' pitch and
// never normalizes, contracts, expands, inserts, removes, or reorders events
// or grace groups. Moving a notehead in an incomplete voice therefore changes
// nothing about the voice's rhythm and creates no unrelated rests.
//
// Snapshot: the entire VoiceContent before the edit, exactly like
// SetEventCommand/SetTieCommand, so undo/redo restore the exact pre-edit
// voice (including an incomplete one). execute() re-resolves the notehead
// against the project, so a stale identity fails atomically rather than
// mutating a different notehead.
class MoveNoteheadCommand : public Command {
 public:
  MoveNoteheadCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                      Voice voice, NotationEntityId notehead_id,
                      NoteheadStepDirection direction)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        notehead_id_(notehead_id),
        direction_(direction) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                node_id_;
  TrackId               track_id_;
  StaveId               stave_id_;
  Voice                 voice_;
  NotationEntityId      notehead_id_;
  NoteheadStepDirection direction_;

  // Saved pre-edit state for undo restoration.
  std::optional<VoiceContent> pre_snapshot_;
  // Saved post-edit state, verified before undo to reject stale context.
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
