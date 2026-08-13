// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

#include <graphscore/core/accidental.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Which direction a selected notehead's accidental steps along the ladder
// double-flat -> flat -> natural -> sharp -> double-sharp. kLower is one rung
// down that ladder, kRaise one rung up.
enum class AccidentalStepDirection : std::uint8_t {
  kLower = 0,
  kRaise,
};

// The target spelling one accidental rung `direction` from `pitch`, keeping
// its letter and octave fixed. Returns std::nullopt at either end of the
// ladder -- lowering a double-flat or raising a double-sharp -- and when the
// resulting spelling would not resolve to a sounding MIDI pitch. The step is
// never clamped: an end of the ladder is a hard reject, not a no-op edit.
// This is the single shared source of the step StepAccidentalCommand applies
// and graphscore_notation::audition_for_accidental_step auditions, so the
// audited pitch can never drift from the pitch the command produces.
[[nodiscard]] std::optional<SpelledPitch> step_notehead_accidental(
    const SpelledPitch& pitch, AccidentalStepDirection direction);

// Steps one notehead's accidental -- a top-level Note, one ChordNote inside a
// Chord, or one GraceNote inside a GraceGroup -- one rung along the
// double-flat/flat/natural/sharp/double-sharp ladder while preserving its
// letter, octave, its event/notehead identity, and every unrelated event,
// chord, or grace field.
//
// The notehead is addressed by its persistent NotationEntityId rather than by
// a Rational position, because a NoteheadItem (graphscore/domain/selection.hpp)
// names the notehead by id and because a GraceNote has no onset of its own.
//
// The ladder does not wrap and is never clamped: lowering a double-flat or
// raising a double-sharp fails, leaving the project unchanged rather than
// producing a triple accidental, wrapping to the far end, or silently doing
// nothing to the notehead while still recording a command. The same is true
// when the new spelling would not resolve to a sounding MIDI pitch.
//
// Tied noteheads step together: the complete connected tie chain through the
// selected notehead (incoming and outgoing, including chord noteheads and
// ties that cross a measure boundary) is stepped by the same accidental rung,
// so the tie is never severed. This is forced by the tie invariant, not a
// preference -- SpelledPitch carries the accidental, and validate_ties()
// rejects a tied notehead whose pitch differs from its successor's
// (kTiePitchMismatch) -- so altering one member alone could not validate. If
// any member cannot step (a ladder end or a sounding-MIDI boundary), the
// whole command fails atomically and the project is unchanged.
//
// The edit is pitch-only and index-preserving: VoiceContent is mutated through
// set_notehead_pitch, which changes only the addressed notehead(s)' pitch and
// never normalizes, contracts, expands, inserts, removes, or reorders events
// or grace groups. Stepping an accidental in an incomplete voice therefore
// changes nothing about the voice's rhythm and creates no unrelated rests.
//
// Snapshot: the entire VoiceContent before the edit, exactly like
// SetEventCommand/MoveNoteheadCommand, so undo/redo restore the exact pre-edit
// voice (including an incomplete one). execute() re-resolves the notehead
// against the project, so a stale identity fails atomically rather than
// mutating a different notehead.
class StepAccidentalCommand : public Command {
 public:
  StepAccidentalCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                        Voice voice, NotationEntityId notehead_id,
                        AccidentalStepDirection direction)
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
  NodeId                  node_id_;
  TrackId                 track_id_;
  StaveId                 stave_id_;
  Voice                   voice_;
  NotationEntityId        notehead_id_;
  AccidentalStepDirection direction_;

  // Saved pre-edit state for undo restoration.
  std::optional<VoiceContent> pre_snapshot_;
  // Saved post-edit state, verified before undo to reject stale context.
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
