// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <graphscore/core/note_audition.hpp>
#include <graphscore/domain/event_style_command.hpp>
#include <graphscore/domain/marking_style_command.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/notation/notation_palette.hpp>
#include <graphscore/notation/notation_types.hpp>

namespace graphscore {

class Command;
class Project;
enum class AccidentalStepDirection : std::uint8_t;
enum class IntervalDirection : std::uint8_t;
enum class NoteheadStepDirection : std::uint8_t;

// A toolkit-neutral factory result used both for command-palette availability
// and execution. An unavailable edit carries a composer-facing reason and no
// command; an available edit carries the reversible domain command.
struct NotationEditCommandResult {
  std::unique_ptr<Command> command;
  std::string              unavailable_reason;

  [[nodiscard]] bool available() const noexcept { return command != nullptr; }
};

// Applies an articulation to one selected Note/Chord event. Change and remove
// address one selected articulation marking. Duplicate and conflicting
// duration articulations are rejected rather than silently normalized.
[[nodiscard]] NotationEditCommandResult make_articulation_edit_command(
    const Project& project, const Selection& selection, ArticulationEdit edit,
    Articulation articulation);

// Sets Auto/Up/Down on one selected Note/Chord event. Auto is the model's
// explicit representation of clearing a manual override.
[[nodiscard]] NotationEditCommandResult make_stem_edit_command(
    const Project& project, const Selection& selection, StemDirection stem);

// Applies a point dynamic to one selected Note/Chord event; change and remove
// address one selected dynamic marking. A dynamic is anchored to a top-level
// event, so a selected chord notehead resolves to its own chord.
[[nodiscard]] NotationEditCommandResult make_dynamic_edit_command(
    const Project& project, const Selection& selection, MarkingEdit edit,
    Dynamic value);

// Applies a hairpin across one selected range: an exact, non-empty
// ArbitraryRangeSet of complete contiguous events on one staff and voice,
// carrying at least two events, whose first and last become the span's
// endpoints. Change and remove address one selected hairpin marking.
[[nodiscard]] NotationEditCommandResult make_hairpin_edit_command(
    const Project& project, const Selection& selection, MarkingEdit edit,
    HairpinDirection direction);

// Applies a stave-scoped pedal span across one selected range, or removes one
// selected pedal span. Pedal is scoped per stave rather than per voice, so a
// range covering several voices of one stave is accepted and the span's exact
// musical bounds are the union of the range's own bounds. kChange is rejected:
// a pedal span carries no style attribute, only endpoints, which a later
// endpoint-drag gesture owns.
[[nodiscard]] NotationEditCommandResult make_pedal_span_edit_command(
    const Project& project, const Selection& selection, MarkingEdit edit);

// A conventional tuplet number omits M when M is the greatest power of two
// strictly below N (3:2, 5:4, 7:4, 9:8, ...). This is the standard simple
// subdivision family; every other ratio is printed explicitly as N:M.
[[nodiscard]] constexpr bool is_conventional_tuplet_ratio(
    TupletRatio ratio) noexcept {
  std::uint32_t normal = 1;
  while (normal * 2u < ratio.played())
    normal *= 2u;
  return ratio.played() > 1u && ratio.normal() == normal;
}

[[nodiscard]] std::string tuplet_label(TupletRatio ratio);

// Creates a single-level tuplet from exactly one non-empty ArbitraryRangeItem.
// The span must coincide with complete contiguous events in one voice; partial
// events, multi-staff/voice ranges, and any existing tuplet membership reject.
[[nodiscard]] std::unique_ptr<Command> make_tuplet_create_command(
    const Project& project, const Selection& selection, TupletRatio ratio);

// Changes/removes exactly one selected tuplet marking. Group identity fixes the
// complete bounds, so a partial group cannot be represented by these routes.
[[nodiscard]] std::unique_ptr<Command> make_tuplet_change_command(
    const Project& project, const Selection& selection, TupletRatio ratio);
[[nodiscard]] std::unique_ptr<Command> make_tuplet_remove_command(
    const Project& project, const Selection& selection);

// The marking selection to install after a successful create. It is computed
// from the pre-edit range and names the first selected event.
[[nodiscard]] std::optional<Selection> selection_after_tuplet_create(
    const Project& project, const Selection& selection);

// Constructs a reversible domain command for the note-entry pointer action
// described in docs/plan/05-notation-editor.md ("Clicking an existing
// rhythmic event in the selected voice changes its selected duration;
// clicking another pitch at the onset builds a chord").
//
// `position` is the onset of the existing event to replace (typically
// NotationPreview::candidate_onset from the same click). `armed` is the
// palette's current next_entry_spec(); its `.voice` field is the sole
// source of truth for which voice this command targets.
// `candidate_pitch` is NotationPreview::candidate_pitch from the same
// click; it must be present when armed.entry_kind is kNote, and is
// ignored when kRest.
//
// Duration-only replacement preserves the existing event's top-level
// identity (Note::id, Chord::id, Rest::id) and every existing embedded
// identity (ChordNote::id).  A new notehead on an existing event adds a
// ChordNote with a fresh NotationEntityId; the existing ChordNote ids are
// carried forward unchanged.  A Note promoted to a Chord keeps the Note's
// id as one of the Chord's ChordNotes.  A Rest at `position` is replaced
// with a Note when kNote is armed; a Note or Chord is replaced with a Rest
// when kRest is armed.  Duplicate-pitch clicks are treated as
// duration-only: neither duplicate noteheads nor duplicate ChordNotes are
// created.
//
// Explicit voice-stream workflow: this is the composer's only path to
// start a rhythmic line in a voice that has never held anything (a stave
// always carries four structural VoiceContent slots, but an untouched one
// is empty until something fills it, and every case above operates on an
// *existing* event boundary that an empty voice does not have). When the
// armed voice is entirely empty, `position` is matched against the onsets
// of that voice's hypothetical measure-aligned rest fill
// (decompose_measure_aligned_rests, voice_content.hpp -- the same
// computation preview_note_entry above previews) rather than against any
// existing event, and on a match the returned Command is a
// CommandTransaction (command_transaction.hpp) composing, in order, a
// CreateVoiceStreamCommand that materializes the fill followed by the same
// SetEventCommand the non-empty path above would build at that onset.
// Because SetEventCommand replaces by exact Rational position rather than
// by id, it needs no knowledge of the ids CreateVoiceStreamCommand mints.
// The transaction is one CommandHistory entry: a single undo removes the
// note (or rest) and the entire stream together, returning the voice to
// completely empty, exactly as if the composer had never clicked; redo is
// id-for-id identical to the original execute. Arming kRest into an empty
// voice still materializes the stream -- the result is simply a voice of
// normalized rests -- rather than being special-cased into a no-op, so
// arming a rest duration and clicking an empty voice has the same
// voice-creating effect as arming a note.
//
// Returns nullptr when the armed voice is non-empty and no event starts at
// `position` in it, when the armed voice is empty and `position` is not an
// onset of its hypothetical measure-aligned fill (or that fill cannot be
// produced exactly), when the armed voice is empty and the node has no
// NodeTimeline (the non-empty path above never needs one, so this is a
// failure mode unique to the empty-voice path), when armed.entry_kind is
// kNote but candidate_pitch is absent, when the constructed replacement
// would violate domain invariants (fewer than two notes in a Chord,
// duplicate IDs, etc.), or when `project` does not own the specified
// node/track/stave.  The non-empty path's behavior (including every
// rejection above it) is unchanged by this addition: it still returns a
// bare SetEventCommand, never a transaction.  The returned command has not
// been executed; the caller applies it through a CommandHistory.
//
// This function is toolkit-neutral, consumes only the armed entry kind/
// duration/voice and candidate pitch, and does not apply markings
// (articulations, dynamics, etc.) — those are Structural editing scope.
[[nodiscard]] std::unique_ptr<Command> make_note_entry_command(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch);

// Constructs a reversible domain command for the keyboard pitch action
// described in docs/plan/05-notation-editor.md M5-phase-20: "Up/Down moves a
// selected notehead one diatonic staff step and preserves its accidental."
//
// `notehead` is the single selected NoteheadItem
// (graphscore/domain/selection.hpp); `direction` is up or down. The returned
// MoveNoteheadCommand re-resolves `notehead.entity` against the project at
// execute time, so a stale identity fails atomically rather than mutating a
// different notehead. Pitch arithmetic and identity preservation live entirely
// in MoveNoteheadCommand; this helper owns only the selection-to-command
// translation, mirroring make_note_entry_command.
//
// Returns nullptr when `notehead` does not name a single valid notehead in the
// project -- an unknown node/track/stave/lane, an archived track, or an entity
// that is not a Note/ChordNote/GraceNote in the addressed voice -- which is
// exactly validate_selection's own notehead-arm check. Building the command
// never mutates the project; the caller applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_move_notehead_command(
    const Project& project, const NoteheadItem& notehead,
    NoteheadStepDirection direction);

// Constructs the reversible command for deleting one selected notehead. A
// complete Note or Chord becomes a same-duration Rest; removing one pitch from
// a Chord preserves the remaining pitches and contracts a two-note Chord to a
// normal Note. Grace-note deletion removes its note from its GraceGroup.
// Returns nullptr unless `notehead` is a valid single notehead selection.
[[nodiscard]] std::unique_ptr<Command> make_delete_notehead_command(
    const Project& project, const NoteheadItem& notehead);

// Resolves the selection to hold after deleting `notehead`, using the state
// immediately before the delete. The preceding event onset in the same voice
// is selected when one exists; otherwise an insertion caret is placed at the
// deleted event's onset. The returned selection is intended to be installed
// after the delete command succeeds.
[[nodiscard]] std::optional<Selection> selection_after_notehead_delete(
    const Project& project, const NoteheadItem& notehead);

// Constructs the reversible command for the keyboard action described in
// docs/plan/05-notation-editor.md M5-phase-23: "`R` converts the entire
// selected note/chord event to an equal-duration rest."
//
// Takes the complete Selection (graphscore/domain/selection.hpp) rather
// than a single item type, because deciding whether `selection` is even
// eligible -- and, when it is a NoteheadSet, whether its entity names a
// top-level Note, a ChordNote (whose WHOLE containing Chord then converts,
// unlike make_delete_notehead_command's per-pitch semantics), or a
// GraceNote (rejected) -- is itself the arm-dispatch this function owns; a
// caller holding a full Selection would otherwise have to replicate that
// dispatch before it could pick which single-item overload to call.
// Accepts a single-item NoteheadSet or a single-item ChordSet;
// VoiceContent::position_of_event resolves either arm's entity to the
// exact Rational position ConvertEventToRestCommand addresses.
//
// Every other selection is a no-op returning nullptr: an empty or
// multi-item set on either accepted arm, any other Selection arm (RestSet
// -- already a rest -- MarkingSet, FullMeasureSet, ArbitraryRangeSet,
// InsertionCaretSet, NodeSet, ConnectorSet), no selection at all, a stale
// selection that no longer resolves, and a NoteheadSet entity that names a
// GraceNote. Building the command never mutates the project; the caller
// applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_convert_event_to_rest_command(
    const Project& project, const Selection& selection);

// Resolves the selection to hold after converting `selection`'s single
// note/chord event to a rest, using the state immediately before the
// conversion: a single-item RestSet addressing the converted event's own
// preserved NotationEntityId. Returns std::nullopt under exactly the same
// conditions make_convert_event_to_rest_command returns nullptr for, so a
// caller checks one before invoking the other without needing to check
// both. The returned selection is intended to be installed after the
// convert-to-rest command succeeds.
[[nodiscard]] std::optional<Selection> selection_after_convert_to_rest(
    const Project& project, const Selection& selection);

// Which neighbouring staff a keyboard staff step moves to, in the score
// order score_ordered_staves defines: kPrevious is one position earlier
// (visually higher), kNext one position later (visually lower).
enum class StaffStepDirection : std::uint8_t { kPrevious, kNext };

// Resolves the selection to hold after the keyboard action described in
// docs/plan/05-notation-editor.md M5-phase-24: "Primary+Up/Down moves to
// the prior/next staff, wraps within the node, and selects the same-voice
// note nearest the musical position, then the visually nearest note on a
// tie, or places a caret."
//
// This is a PURE SELECTION change. It builds no Command, mutates nothing,
// and invalidates no layout; the caller simply installs the returned
// Selection. `layout` must be the layout of `selection`'s own node
// (produced by a prior layout_notation()/NotationLayoutCache::update()
// call for the same project/node); it is read only for the tie tie-break
// below.
//
// Eligible source selections are a single-item NoteheadSet, ChordSet,
// RestSet, or InsertionCaretSet. Every other arm (FullMeasureSet,
// ArbitraryRangeSet, NodeSet, ConnectorSet, MarkingSet), a multi-item set,
// and a selection that no longer validates against `project` are no-ops
// returning std::nullopt.
//
// Resolution, in order:
//
//   Staff walk  -- the candidate staves are score_ordered_staves(project)
//     FILTERED to the staves the source's own node actually carries (its
//     Node::lane(TrackId) exists and that TrackLane::stave(StaveId)
//     exists), so a project-wide staff the node does not engrave is never
//     stepped onto. The step wraps at both ends of that filtered list. A
//     node carrying exactly ONE such staff is a no-op returning
//     std::nullopt rather than a wrap onto itself: re-resolving the source
//     staff would silently move the composer's selection sideways with no
//     staff change to explain it.
//
//   Voice       -- carries over verbatim from the source item's own Voice.
//     When that voice slot on the target staff holds no events at all, the
//     result is immediately an insertion caret at position 0 (the only
//     legal caret in an empty voice); no other voice is consulted.
//
//   Nearest     -- the source's musical position (a note/chord/rest
//     selection's own event onset, or an insertion caret's own position)
//     is compared against each candidate event's ONSET; the smallest
//     absolute distance wins, ties resolving to the EARLIER onset. Only
//     top-level Note and Chord events are candidates: rests are not (the
//     caret fallback below is what a rest-only target produces, which is
//     the behavior "or places a caret" describes), and neither are grace
//     notes.
//
//   Tie         -- when the winning candidate belongs to a tie chain (see
//     graphscore/domain/tie_chain.hpp), the chain member whose notehead is
//     VISUALLY nearest the source notehead horizontally is taken instead,
//     ties resolving to the earlier musical onset. This is a literal
//     visual rule, not a restatement of the musical one: a tie crossing a
//     system break puts the musically-nearest chain member far to the
//     right on the previous system, and the visually-nearest member is
//     then a different note. The chain of a chord candidate is the union
//     of its noteheads' own chains, so a partially-tied chord still
//     resolves.
//
//     DEGRADATION: the source's own notehead x comes from the
//     GlyphCommand whose id is "<entity>/notehead" in `layout` (a chord
//     source uses the first of its ChordNotes that has one -- first in
//     storage order, which is not necessarily the leftmost of a
//     second-displaced chord column). When there is no such x -- a RestSet
//     or InsertionCaretSet source, which has no notehead at all, or a
//     notehead absent from the supplied layout because it was never laid
//     out -- this step falls back to choosing the chain member nearest by
//     musical ONSET, which is exactly the candidate the nearest rule
//     already picked. The same fallback applies when no chain member has a
//     notehead glyph in `layout`.
//
//   Result      -- a single-item ChordSet when the resolved event is a
//     Chord and a single-item NoteheadSet when it is a Note. The SOURCE
//     arm never carries over (stepping from a rest can land on a note),
//     and no attempt is made to match the source's pitch to a particular
//     notehead of a target chord.
//
//   Caret       -- when the target staff/voice yields no Note or Chord
//     candidate at all, the result is a single-item InsertionCaretSet at
//     the carried position SNAPPED to the nearest legal caret position in
//     that voice, ties resolving to the earlier position. The legal
//     positions are exactly the ones validate_selection accepts: position
//     0, an exact event boundary in that voice, and
//     TrackLane::total_length() (which is what a carried position past the
//     end of the target lane snaps to).
//
// The returned Selection always satisfies
// validate_selection(project, selection).empty(); a result that would not
// is rejected with std::nullopt instead. A pure query: never mutates
// `project` or `layout`.
[[nodiscard]] std::optional<Selection> selection_after_staff_step(
    const Project& project, const NotationLayout& layout,
    const Selection& selection, StaffStepDirection direction);

// The short audition request for the same keyboard pitch action
// make_move_notehead_command above builds a command for, mirroring
// audition_for_note_entry: this milestone produces the request as a value and
// nothing plays it (graphscore_writer_audio consumes it in Milestone 08).
//
// The request sounds the single post-edit sounding MIDI pitch of the moved
// notehead (a tied notehead moves its whole chain, every member of which
// shares one pitch, so the one post-edit pitch is still the whole audible
// change), through the notehead's own track at the project's default dynamic.
// A chord notehead move auditions that notehead's post-edit pitch alone -- not
// the whole chord -- because the composer is re-pitching one notehead, unlike
// note entry, where building a harmony auditions the whole resulting chord.
//
// A pure query over the PRE-execution project: it never mutates `project` and
// builds no command. It returns std::nullopt -- auditions nothing -- whenever
// the move itself would fail or change nothing audible: an invalid/stale
// notehead (the same notehead-arm check make_move_notehead_command uses), or
// a step that would leave the SpelledPitch/MIDI range (the boundary the
// command also rejects atomically). A caller invokes it before executing the
// command and discards the request when execution fails.
[[nodiscard]] std::optional<NoteAuditionRequest> audition_for_notehead_move(
    const Project& project, const NoteheadItem& notehead,
    NoteheadStepDirection direction);

// Constructs a reversible domain command for the keyboard accidental action
// described in docs/plan/05-notation-editor.md M5-phase-21: "`-` and `=` step
// through double-flat, flat, natural, sharp, and double-sharp."
//
// `notehead` is the single selected NoteheadItem
// (graphscore/domain/selection.hpp); `direction` is one rung down (`-`) or up
// (`=`) that ladder. The returned StepAccidentalCommand re-resolves
// `notehead.entity` against the project at execute time, so a stale identity
// fails atomically rather than mutating a different notehead. Spelling
// arithmetic and identity preservation live entirely in
// StepAccidentalCommand; this helper owns only the selection-to-command
// translation, mirroring make_move_notehead_command.
//
// Returns nullptr when `notehead` does not name a single valid notehead in the
// project -- an unknown node/track/stave/lane, an archived track, or an entity
// that is not a Note/ChordNote/GraceNote in the addressed voice -- which is
// exactly validate_selection's own notehead-arm check. Building the command
// never mutates the project; the caller applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_step_accidental_command(
    const Project& project, const NoteheadItem& notehead,
    AccidentalStepDirection direction);

// The short audition request for the same keyboard accidental action
// make_step_accidental_command above builds a command for. An accidental step
// changes the sounding pitch, so it auditions exactly like a diatonic move
// (M5-phase-15's "pitch-edited notes issue a short preview request"); this
// milestone produces the request as a value and nothing plays it
// (graphscore_writer_audio consumes it in Milestone 08).
//
// The request sounds the single post-edit sounding MIDI pitch of the stepped
// notehead (a tied notehead steps its whole chain, every member of which
// shares one pitch, so the one post-edit pitch is still the whole audible
// change), through the notehead's own track at the project's default dynamic.
// A chord notehead auditions that notehead's post-edit pitch alone -- not the
// whole chord -- because the composer is re-spelling one notehead.
//
// A pure query over the PRE-execution project: it never mutates `project` and
// builds no command. It returns std::nullopt -- auditions nothing -- whenever
// the step itself would fail: an invalid/stale notehead (the same
// notehead-arm check make_step_accidental_command uses), or a step off either
// end of the ladder or out of the sounding MIDI range (the boundaries the
// command also rejects atomically). A caller invokes it before executing the
// command and discards the request when execution fails.
[[nodiscard]] std::optional<NoteAuditionRequest> audition_for_accidental_step(
    const Project& project, const NoteheadItem& notehead,
    AccidentalStepDirection direction);

// Constructs a reversible domain command for the keyboard interval action
// described in docs/plan/05-notation-editor.md M5-phase-25: "`2` through `8`
// add a key-spelled diatonic interval above; Shift variants add below."
//
// `notehead` is the single selected NoteheadItem
// (graphscore/domain/selection.hpp); `interval` is the diatonic interval
// number (2..8), and `direction` is above (unmodified) or below (Shift). The
// returned AddIntervalCommand re-resolves `notehead.entity` against the
// project at execute time, so a stale identity fails atomically rather than
// mutating a different notehead. Spelling arithmetic (interval_target_pitch,
// graphscore/domain/add_interval_command.hpp) and identity generation live
// entirely in AddIntervalCommand; this helper owns only the
// selection-to-command translation, mirroring make_move_notehead_command.
//
// Returns nullptr when `notehead` does not name a single valid interval
// source -- an unknown node/track/stave/lane, an archived track, an entity
// that is not a Note/ChordNote in the addressed voice (a GraceNote has no
// rhythmic event to grow and is rejected), a node with no timeline, or a
// notehead whose onset falls outside the timeline's main region. Building
// the command never mutates the project; the caller applies it through a
// CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_add_interval_command(
    const Project& project, const NoteheadItem& notehead, std::uint8_t interval,
    IntervalDirection direction);

// The short audition request for the same keyboard interval action
// make_add_interval_command above builds a command for. Adding a pitch to an
// event changes what sounds, so it auditions exactly like note entry
// (M5-phase-15's "Newly inserted or pitch-edited notes issue a short preview
// request"): promoting a Note to a two-note Chord auditions both pitches,
// and extending a Chord auditions every pitch of the resulting chord,
// pre-existing ones included. This milestone produces the request as a value
// and nothing plays it (graphscore_writer_audio consumes it in Milestone 08).
//
// A pure query over the PRE-execution project: it never mutates `project` and
// builds no command. It returns std::nullopt -- auditions nothing -- whenever
// the interval would fail or change nothing audible: an invalid/stale
// notehead, a GraceNote, a node with no timeline or an onset outside the main
// region (the same source check make_add_interval_command uses), an interval
// outside [2, 8], a target spelling that leaves the SpelledPitch/MIDI range,
// or a target that would duplicate a pitch already in the event. A caller
// invokes it before executing the command and discards the request when
// execution fails. The target pitch is resolved through the shared
// notehead_key_signature/interval_target_pitch helpers, so the audited pitch
// can never disagree with the pitch the command writes.
[[nodiscard]] std::optional<NoteAuditionRequest> audition_for_add_interval(
    const Project& project, const NoteheadItem& notehead, std::uint8_t interval,
    IntervalDirection direction);

// The short audition request for the same note-entry pointer action
// make_note_entry_command above builds a command for
// (docs/plan/05-notation-editor.md: "Newly inserted or pitch-edited notes
// issue a short preview request; actual plugin audition is connected in
// Milestone 08"). The parameter list mirrors make_note_entry_command's
// exactly so a caller invokes both with the same arguments; this milestone
// produces the request as a value and nothing plays it, so there is no
// consumer in this repository yet -- graphscore_writer_audio picks it up in
// Milestone 08 (ADR 0003 assigns "note-preview insertion audition" there,
// which is also why NoteAuditionRequest itself is declared in
// graphscore_core rather than here; see core/note_audition.hpp).
//
// A pure query: it never mutates `project` and builds no command. It is
// evaluated against the PRE-execution project state, i.e. a caller calls it
// before executing make_note_entry_command's command, since it reads the
// event currently at `position` to decide what the click newly sounds.
//
// Both functions resolve the click through one shared internal branch
// resolution, so they can never disagree about which case a click falls
// into. What each branch auditions:
//
//   * a new note into an entirely empty armed voice: the one new pitch;
//   * a rest replaced by a note: the one new pitch;
//   * a note promoted to a two-note chord: BOTH pitches;
//   * a notehead added to an existing chord: EVERY pitch of the resulting
//     chord, pre-existing ones included;
//   * a same-pitch click on a note, or a duplicate-pitch click on a chord
//     (both pure duration changes): std::nullopt;
//   * armed.entry_kind == kRest: std::nullopt;
//   * any input make_note_entry_command rejects (returns nullptr for):
//     std::nullopt.
//
// The two product decisions behind that table, in the composer's terms:
// adding a pitch to a chord auditions the WHOLE resulting chord, because
// the composer is building a harmony and wants to hear it rather than the
// single pitch they clicked; and a pure duration change is SILENT, because
// no pitch was inserted and none changed, so clicking through rhythms on
// one note never re-sounds it.
//
// Velocity is velocity_for_dynamic(project.default_dynamic())
// (core/playback_mapping.hpp) and nothing more. Accent/marcato emphasis
// (apply_emphasis), hairpin interpolation, and resolving whichever
// DynamicMarking actually governs `position` in the timeline are all
// deliberately NOT applied: armed articulations and markings are not yet
// applied on note entry at all (see make_note_entry_command's last
// paragraph -- that is Structural editing scope), and position-based
// dynamic-context resolution is explicitly outside playback_mapping.hpp's
// own scope ("Scope: math only, not context resolution"). When a later
// phase resolves governing context across a timeline, this call site is
// where the richer resolution belongs.
//
// Pitch conversion: SpelledPitch::to_midi_pitch() (core/spelled_pitch.hpp)
// fails at extreme octaves. When the NEWLY INSERTED pitch does not convert,
// this returns std::nullopt outright -- there is nothing meaningful to
// audition. Otherwise every pre-existing chord pitch that converts is
// included and any that does not is silently skipped, so one unsoundable
// notehead already in a chord never suppresses the audition of the pitch
// the composer just added.
[[nodiscard]] std::optional<NoteAuditionRequest> audition_for_note_entry(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch);

// Which side of the selected measure make_insert_measure_command/
// selection_after_insert_measure insert a new measure on -- the composer's
// "insert before" affordance, or "append" a fresh final measure regardless
// of where in the node the selection happens to be.
enum class MeasureInsertMode : std::uint8_t { kBefore, kAppend };

// Constructs the reversible domain command for the measure-structure half
// of docs/plan/05-notation-editor.md's M5-phase-28: "Insert/delete measures
// across every track and update signatures, clefs, tempo anchors, spans,
// selection, and rests atomically." The domain's own InsertMeasureCommand
// (graphscore/domain/insert_measure_command.hpp) already performs the
// entire cascade atomically -- every active and archived track, every
// stave, every voice, signatures, clef changes, tempo anchors (including
// re-anchoring the mandatory origin), ties, slurs, hairpins, pedal spans,
// beam overrides, grace groups, automatic rest normalization, and
// tuplet-boundary/pickdown revalidation -- and is not touched here; this
// function's only job is picking which index that command targets from
// `selection`, and rejecting selections the domain cascade was never meant
// to interpret.
//
// `selection` must be a FullMeasureSet whose items are ALIGNED: every item
// shares one NodeId and one measure_index, exactly the precondition
// extend_measure_selection (graphscore/notation/notation_selection.hpp)
// already documents and enforces. Every other Selection arm (NoteheadSet,
// ChordSet, RestSet, MarkingSet, ArbitraryRangeSet, InsertionCaretSet,
// NodeSet, ConnectorSet) and a misaligned FullMeasureSet are rejected with
// nullptr -- never silently normalized to one item.
//
// `mode == kBefore` targets `selection`'s own shared measure_index: the new
// measure is built with the domain's two-argument InsertMeasureCommand
// constructor, so it inherits the signature of the measure it is inserted
// before. `mode == kAppend` targets the node's own timeline
// measure_count() -- the domain's own two-argument constructor inherits the
// FINAL measure's signature at that index -- regardless of which measure
// `selection` itself names; for kAppend, `selection`'s own measure_index is
// used only to identify the anchor node and the (track, stave) scopes the
// edit applies to, not the insertion point.
//
// Returns nullptr when `selection` is not a FullMeasureSet, is misaligned,
// or fails validate_selection(project, selection) -- which is what rejects
// an unknown node, a node with no NodeTimeline, an item naming a track that
// is not active, a stave absent from that track's own StaffLayout, a
// track/stave with no usable lane in the anchor node, and a measure_index
// at or beyond measure_count(). This is distinct from a rejection the
// DOMAIN itself only detects at execute() time -- a boundary that would cut
// a tuplet group, or a pickdown the edit would invalidate: this function
// still returns a valid command for those inputs, and
// CommandHistory::execute_new reports that failure atomically when the
// command actually runs. Building the command never mutates `project`; the
// caller applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_insert_measure_command(
    const Project& project, const Selection& selection, MeasureInsertMode mode);

// Resolves the selection to hold after make_insert_measure_command's edit
// succeeds, using the state immediately BEFORE the insert -- a pure query,
// exactly like selection_after_notehead_delete/selection_after_convert_to_rest
// above. The result is always a FullMeasureSet naming the identical
// (track, stave) scopes `selection` itself named, in the same deterministic
// score order extend_measure_selection guarantees
// (Project::active_tracks() order, then each track's own
// StaffLayout::staves() order), with the measure index remapped per this
// rule: the post-edit selection names the same musical material where that
// material still exists; where the edit created the material at the
// composer's focus, it names the measure the composer will work in next.
//
//   kBefore -- the selected music shifts right by one measure, so the
//     result names index + 1: the same measure of music the source
//     selection named, now one measure later.
//   kAppend -- no existing material moved, and the composer asked for a
//     measure at node end specifically to write there, so the result names
//     the new final measure: measure_count() evaluated BEFORE the insert
//     (equivalently new_count - 1), regardless of the source selection's
//     own measure_index.
//
// Computed arithmetically from the pre-edit project state; this function
// never mutates `project` and is callable before the command returned by
// make_insert_measure_command is executed. Returns std::nullopt under
// exactly the same conditions make_insert_measure_command returns nullptr
// for, so a caller checks one without needing to check both.
[[nodiscard]] std::optional<Selection> selection_after_insert_measure(
    const Project& project, const Selection& selection, MeasureInsertMode mode);

// Constructs the reversible domain command for the deletion half of
// M5-phase-28 (see make_insert_measure_command's own comment for the
// cascade the domain's DeleteMeasureCommand already performs atomically).
// `selection` must be a FullMeasureSet whose items are aligned exactly as
// make_insert_measure_command requires; every other arm and a misaligned
// set are rejected with nullptr. The command targets `selection`'s own
// shared measure_index.
//
// Returns nullptr under every condition make_insert_measure_command
// rejects for (wrong arm, misalignment, a validate_selection failure), and
// additionally when the node's own timeline carries exactly one measure --
// pre-rejecting the domain's own sole-measure guard
// (DeleteMeasureCommand::execute) rather than returning a command that is
// guaranteed to fail. As with insert, a rejection the domain itself only
// detects at execute() time (a boundary cutting a tuplet group, or a
// pickdown the edit would invalidate) is NOT this function's concern: it
// still returns a valid command, and the domain reports that failure
// atomically at execute. Building the command never mutates `project`; the
// caller applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_delete_measure_command(
    const Project& project, const Selection& selection);

// Resolves the selection to hold after make_delete_measure_command's edit
// succeeds, using the state immediately BEFORE the delete -- a pure query
// mirroring selection_after_insert_measure above. The result is always a
// FullMeasureSet naming the identical (track, stave) scopes `selection`
// itself named, in the same deterministic score order, with the measure
// index remapped: the material the source selection named is gone, so the
// selection falls back to the measure that shifted into its old index;
// when that index was the last measure, it clamps to the new last index,
// new_count - 1. The sole-measure rejection make_delete_measure_command
// applies guarantees new_count >= 1, so the clamp is always well defined.
//
// Computed arithmetically from the pre-edit project state; never mutates
// `project`. Returns std::nullopt under exactly the same conditions
// make_delete_measure_command returns nullptr for.
[[nodiscard]] std::optional<Selection> selection_after_delete_measure(
    const Project& project, const Selection& selection);

}  // namespace graphscore
