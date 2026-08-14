// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include <graphscore/core/key_signature.hpp>
#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

class Project;

// Which side of the selected notehead a keyboard interval entry adds the new
// pitch on: above (unmodified `2`..`8`) or below (Shift+`2`..`8`).
enum class IntervalDirection : std::uint8_t { kAbove = 0, kBelow };

// The target spelling for adding a key-spelled diatonic interval `interval`
// (2..8, i.e. `interval - 1` letter steps) `direction` above/below `source`.
//
// The target letter and octave derive from `source`'s own diatonic letter
// and octave only -- an altered source pitch still names its diatonic source
// letter, and its accidental never carries into the result. The target's
// accidental is instead the standard accidental `key` implies for the target
// letter (the sharp/flat/natural a key signature assigns it), exactly what
// key_accidental (src/notation/notation_engraving.cpp) computes when the
// engraver decides whether a notehead needs a written accidental.
//
// Octave wrapping follows scientific pitch notation across multiple steps:
// stepping above B wraps into the next octave, stepping below C wraps into
// the previous one. Returns std::nullopt when `interval` is outside [2, 8],
// when the target spelling would leave SpelledPitch's valid octave range, or
// when it would not resolve to a sounding MIDI pitch -- never a clamped
// value. This is the single shared source of the target AddIntervalCommand
// writes and graphscore_notation::audition_for_add_interval auditions, so the
// audited pitch can never drift from the pitch the command produces.
[[nodiscard]] std::optional<SpelledPitch> interval_target_pitch(
    const SpelledPitch& source, std::uint8_t interval,
    IntervalDirection direction, KeySignature key);

// The measure ordinal containing `notehead`'s own rhythmic event, or
// std::nullopt when `notehead` does not resolve to a top-level Note or
// ChordNote in the addressed voice (a GraceNote has no rhythmic measure of
// its own and is rejected, as is an unknown or stale id), when the node has
// no timeline, or when the notehead's onset falls outside the timeline's
// main region. A pure query over the project.
[[nodiscard]] std::optional<std::size_t> notehead_measure_index(
    const Project& project, const NoteheadItem& notehead);

// The key signature active at `notehead`'s own measure -- the key the
// interval's target spelling is spelled against. Returns std::nullopt under
// exactly the conditions notehead_measure_index does. This is the shared
// source of the key both AddIntervalCommand and
// graphscore_notation::audition_for_add_interval resolve, so the two can
// never disagree about which accidental a measure implies. A pure query over
// the project.
[[nodiscard]] std::optional<KeySignature> notehead_key_signature(
    const Project& project, const NoteheadItem& notehead);

// Adds one key-spelled diatonic interval notehead to the selected notehead's
// own rhythmic event, for docs/plan/05-notation-editor.md M5-phase-25:
// "`2` through `8` add a key-spelled diatonic interval above; Shift variants
// add below; the inserted notehead becomes selected."
//
// The notehead is addressed by its persistent NotationEntityId rather than by
// a Rational position, because a NoteheadItem
// (graphscore/domain/selection.hpp) names the notehead by id. execute()
// re-resolves that id against the project, so a stale identity fails
// atomically rather than mutating a different notehead.
//
// The edit is index- and duration-preserving: a top-level Note is promoted
// to a two-note Chord (the Note's own id becomes the first ChordNote, exactly
// as make_note_entry_command's note-to-chord promotion does, and the Chord
// gets a fresh top-level id), while a Chord gains one fresh ChordNote. In
// both cases the event's duration, articulations, and stem override, and
// every existing notehead's id/pitch/tie state, are carried forward
// unchanged. The inserted notehead's id is generated at construction and
// exposed through inserted_notehead_id() so a caller can select it after
// execute succeeds.
//
// Rejections, all atomic (the project is left unchanged):
//   - the notehead is stale, or is a GraceNote (a grace note has no rhythmic
//     event to grow);
//   - the node has no timeline, or the notehead's onset is outside the main
//     region (so no measure key signature governs the spelling);
//   - the interval is outside [2, 8], or the target spelling leaves the
//     SpelledPitch/MIDI range (no clamping);
//   - the target spelling would duplicate a pitch already in the event (a
//     Note source never duplicates: the target letter always differs from
//     the source's own, since interval >= 2 steps at least one letter).
//
// Snapshot: the entire VoiceContent before the edit, exactly like
// SetEventCommand, so undo/redo restore the exact pre-edit voice.
class AddIntervalCommand : public Command {
 public:
  AddIntervalCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                     Voice voice, NotationEntityId notehead_id,
                     std::uint8_t interval, IntervalDirection direction)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        notehead_id_(notehead_id),
        interval_(interval),
        direction_(direction),
        inserted_id_(NotationEntityId::generate()) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

  // The fresh NotationEntityId the inserted ChordNote will carry. Generated
  // at construction, so redo re-applies the identical id; only meaningful to
  // a caller after execute() has succeeded.
  [[nodiscard]] NotationEntityId inserted_notehead_id() const noexcept {
    return inserted_id_;
  }

 private:
  NodeId            node_id_;
  TrackId           track_id_;
  StaveId           stave_id_;
  Voice             voice_;
  NotationEntityId  notehead_id_;
  std::uint8_t      interval_;
  IntervalDirection direction_;
  NotationEntityId  inserted_id_;

  // Saved pre-edit state for undo restoration.
  std::optional<VoiceContent> pre_snapshot_;
  // Saved post-edit state, verified before undo to reject stale context.
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
