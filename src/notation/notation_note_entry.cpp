// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/command_transaction.hpp>
#include <graphscore/domain/convert_event_to_rest_command.hpp>
#include <graphscore/domain/create_voice_stream_command.hpp>
#include <graphscore/domain/delete_notehead_command.hpp>
#include <graphscore/domain/move_notehead_command.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/set_event_command.hpp>
#include <graphscore/notation/notation_editing.hpp>

#include "note_entry_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

// The one case a note-entry click resolves to. Branch selection lives
// solely in resolve_note_entry below, and both make_note_entry_command and
// audition_for_note_entry consume its result, so the command a click builds
// and the audition it requests can never disagree about which case the
// click fell into.

// Resolves a note-entry click into its single branch. A pure query: it
// never mutates `project`. Returns std::nullopt for exactly the inputs
// make_note_entry_command rejects with nullptr.
[[nodiscard]] std::optional<NoteEntryResolution> resolve_note_entry(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    const std::optional<SpelledPitch>& candidate_pitch) {
  const Node* node = project.find_node(node_id);
  if (node == nullptr)
    return std::nullopt;
  const TrackLane* lane = node->lane(track_id);
  if (lane == nullptr)
    return std::nullopt;
  const StaveVoices* stave = lane->stave(stave_id);
  if (stave == nullptr)
    return std::nullopt;
  const VoiceContent& content = stave->voice(armed.voice);

  // Tuplets are structural groups. A single note-entry click cannot know the
  // complete group bounds, so it must use the range-based tuplet action rather
  // than minting one implicit group per event.
  if (armed.duration.tuplet().has_value())
    return std::nullopt;

  // Explicit voice-stream workflow: the armed voice has never held
  // anything, so there is no existing event boundary to click on. Match
  // `position` against the onsets of the same hypothetical measure-aligned
  // rest fill preview_note_entry previews; on a match the caller builds one
  // CommandTransaction that creates the stream and then replaces the rest
  // at `position`. Only the fill's onset shape is read here, never the Rest
  // ids, so this calls the duration-only core
  // (decompose_measure_aligned_rest_durations) exactly as
  // preview_note_entry does rather than
  // decompose_measure_aligned_rests, whose freshly minted ids were
  // discarded unused on this path; both derive from the same tiling and so
  // can never disagree on shape.
  if (content.events().empty()) {
    const NodeTimeline* timeline = node->timeline();
    if (timeline == nullptr)
      return std::nullopt;
    const std::optional<std::vector<Duration>> hypothetical_fill =
        decompose_measure_aligned_rest_durations(*timeline);
    if (!hypothetical_fill.has_value())
      return std::nullopt;

    bool     position_is_an_onset = false;
    Rational onset;
    for (const Duration& rest_duration : *hypothetical_fill) {
      if (onset == position) {
        position_is_an_onset = true;
        break;
      }
      onset = onset + rest_duration.resolved();
    }
    if (!position_is_an_onset)
      return std::nullopt;

    if (armed.entry_kind == NotePaletteEntryKind::kRest)
      return NoteEntryResolution{
          NoteEntryBranch::kCreateStreamRest, nullptr, {}};
    if (!candidate_pitch.has_value())
      return std::nullopt;
    return NoteEntryResolution{
        NoteEntryBranch::kCreateStreamNote, nullptr, {*candidate_pitch}};
  }

  const auto idx = content.find_event_index_at(position);
  if (!idx.has_value())
    return std::nullopt;
  const VoiceEvent& existing = content.events()[*idx];

  if (armed.entry_kind == NotePaletteEntryKind::kRest) {
    // A rest sounds nothing, so no kRest branch ever auditions. Discard any
    // candidate_pitch: rests have no pitch.
    if (std::holds_alternative<Rest>(existing))
      return NoteEntryResolution{
          NoteEntryBranch::kRestDurationOnly, &existing, {}};
    return NoteEntryResolution{NoteEntryBranch::kEventToRest, &existing, {}};
  }

  // --- kNote entry ---
  if (!candidate_pitch.has_value())
    return std::nullopt;

  const SpelledPitch& new_pitch = *candidate_pitch;

  if (std::holds_alternative<Rest>(existing))
    return NoteEntryResolution{
        NoteEntryBranch::kRestToNote, &existing, {new_pitch}};

  // Existing Note: pitch match → duration-only; mismatch → promote to Chord.
  if (const auto* old_note = std::get_if<Note>(&existing)) {
    if (old_note->pitch == new_pitch)
      return NoteEntryResolution{
          NoteEntryBranch::kNoteDurationOnly, &existing, {}};
    return NoteEntryResolution{
        NoteEntryBranch::kNoteToChord, &existing, {new_pitch, old_note->pitch}};
  }

  // Existing Chord.
  if (const auto* old_chord = std::get_if<Chord>(&existing)) {
    // Detect duplicate pitch.
    const bool pitch_already_present = std::ranges::any_of(
        old_chord->notes,
        [&](const ChordNote& cn) { return cn.pitch == new_pitch; });

    if (pitch_already_present)
      return NoteEntryResolution{
          NoteEntryBranch::kChordDurationOnly, &existing, {}};

    std::vector<SpelledPitch> sounding_pitches;
    sounding_pitches.reserve(old_chord->notes.size() + 1);
    sounding_pitches.push_back(new_pitch);
    for (const ChordNote& chord_note : old_chord->notes)
      sounding_pitches.push_back(chord_note.pitch);
    return NoteEntryResolution{NoteEntryBranch::kChordExtension, &existing,
                               std::move(sounding_pitches)};
  }

  return std::nullopt;
}

std::unique_ptr<Command> make_note_entry_command(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch) {
  const std::optional<NoteEntryResolution> resolution = resolve_note_entry(
      project, node_id, track_id, stave_id, position, armed, candidate_pitch);
  if (!resolution.has_value())
    return nullptr;

  const auto set_event = [&](VoiceEvent event) {
    return std::make_unique<SetEventCommand>(
        node_id, track_id, stave_id, armed.voice, position, std::move(event));
  };

  switch (resolution->branch) {
    case NoteEntryBranch::kCreateStreamRest:
    case NoteEntryBranch::kCreateStreamNote: {
      // One CommandTransaction that creates the stream and then replaces the
      // rest at `position` -- a single undoable action that either succeeds
      // completely or leaves the project untouched.
      VoiceEvent new_event;
      if (resolution->branch == NoteEntryBranch::kCreateStreamRest) {
        new_event = make_rest(armed.duration);
      } else {
        new_event = make_note(resolution->inserted_pitch(), armed.duration);
      }

      auto transaction = std::make_unique<CommandTransaction>();
      if (!transaction
               ->add_command(std::make_unique<CreateVoiceStreamCommand>(
                   node_id, track_id, stave_id, armed.voice))
               .ok())
        return nullptr;
      if (!transaction
               ->add_command(std::make_unique<SetEventCommand>(
                   node_id, track_id, stave_id, armed.voice, position,
                   std::move(new_event)))
               .ok())
        return nullptr;
      return transaction;
    }

    case NoteEntryBranch::kRestDurationOnly: {
      // Preserve identity on duration-only.
      Rest new_rest     = std::get<Rest>(*resolution->existing);
      new_rest.duration = armed.duration;
      return set_event(new_rest);
    }

    case NoteEntryBranch::kEventToRest:
      // For kind conversion (Note/Chord→Rest) the old identity is consumed
      // by replace_event per its documented ID-reuse rules.
      return set_event(make_rest(armed.duration));

    case NoteEntryBranch::kRestToNote:
      return set_event(make_note(resolution->inserted_pitch(), armed.duration));

    case NoteEntryBranch::kNoteDurationOnly: {
      Note new_note     = std::get<Note>(*resolution->existing);
      new_note.duration = armed.duration;
      return set_event(new_note);
    }

    case NoteEntryBranch::kNoteToChord: {
      // Promote Note to a 2-note Chord.  The original Note's id becomes the
      // first ChordNote; the new pitch gets a fresh id. Preserve articulations
      // and stem override from the original Note.
      const Note&            old_note = std::get<Note>(*resolution->existing);
      std::vector<ChordNote> chord_notes;
      chord_notes.push_back(
          {old_note.id, old_note.pitch, old_note.tied_to_next});
      chord_notes.push_back(
          {NotationEntityId::generate(), resolution->inserted_pitch(), false});
      return set_event(make_chord(armed.duration, std::move(chord_notes),
                                  old_note.articulations, old_note.stem));
    }

    case NoteEntryBranch::kChordDurationOnly: {
      // Duration-only: preserve every identity.
      Chord new_chord    = std::get<Chord>(*resolution->existing);
      new_chord.duration = armed.duration;
      return set_event(new_chord);
    }

    case NoteEntryBranch::kChordExtension: {
      // Add a new notehead to the existing chord. Preserve identity,
      // articulations and stem override.
      const Chord&           old_chord = std::get<Chord>(*resolution->existing);
      std::vector<ChordNote> new_notes = old_chord.notes;
      new_notes.push_back(
          {NotationEntityId::generate(), resolution->inserted_pitch(), false});
      Chord new_chord = make_chord(armed.duration, std::move(new_notes));
      new_chord.id    = old_chord.id;    // preserve top-level identity
      new_chord.stem  = old_chord.stem;  // preserve stem override
      new_chord.articulations =
          old_chord.articulations;  // preserve articulations
      return set_event(new_chord);
    }
  }

  return nullptr;
}

std::unique_ptr<Command> make_move_notehead_command(
    const Project& project, const NoteheadItem& notehead,
    NoteheadStepDirection direction) {
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value())
    return nullptr;
  if (!validate_selection(project, Selection{*set}).empty())
    return nullptr;
  return std::make_unique<MoveNoteheadCommand>(notehead.node, notehead.track,
                                               notehead.stave, notehead.voice,
                                               notehead.entity, direction);
}

std::unique_ptr<Command> make_delete_notehead_command(
    const Project& project, const NoteheadItem& notehead) {
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value() || !validate_selection(project, Selection{*set}).empty())
    return nullptr;
  return std::make_unique<DeleteNoteheadCommand>(notehead.node, notehead.track,
                                                 notehead.stave, notehead.voice,
                                                 notehead.entity);
}

std::optional<Selection> selection_after_notehead_delete(
    const Project& project, const NoteheadItem& notehead) {
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value() || !validate_selection(project, Selection{*set}).empty())
    return std::nullopt;

  const Node* node = project.find_node(notehead.node);
  if (node == nullptr)
    return std::nullopt;
  const TrackLane* lane = node->lane(notehead.track);
  if (lane == nullptr)
    return std::nullopt;
  const StaveVoices* stave = lane->stave(notehead.stave);
  if (stave == nullptr)
    return std::nullopt;
  const VoiceContent& voice = stave->voice(notehead.voice);

  std::optional<std::size_t> deleted_index;
  Rational                   deleted_onset(0);
  Rational                   onset(0);
  for (std::size_t index = 0; index < voice.events().size(); ++index) {
    const VoiceEvent& event   = voice.events()[index];
    bool              matches = event_id(event) == notehead.entity;
    if (const auto* chord = std::get_if<Chord>(&event)) {
      matches = matches ||
                std::ranges::any_of(chord->notes, [&](const ChordNote& note) {
                  return note.id == notehead.entity;
                });
    }
    if (matches) {
      deleted_index = index;
      deleted_onset = onset;
      break;
    }
    onset = onset + event_duration(event).resolved();
  }

  if (!deleted_index.has_value()) {
    const std::optional<Rational> grace_onset =
        voice.position_of_event(notehead.entity);
    if (!grace_onset.has_value())
      return std::nullopt;
    deleted_onset = *grace_onset;
    deleted_index = voice.find_event_index_at(deleted_onset);
    if (!deleted_index.has_value())
      return std::nullopt;
  }

  if (*deleted_index == 0u) {
    const auto caret = InsertionCaretSet::create(
        {InsertionCaretItem{notehead.node, notehead.track, notehead.stave,
                            notehead.voice, deleted_onset}});
    if (!caret.has_value())
      return std::nullopt;
    return Selection{*caret};
  }

  const VoiceEvent& previous = voice.events()[*deleted_index - 1u];
  if (const auto* note = std::get_if<Note>(&previous)) {
    const auto previous_set = NoteheadSet::create(
        {NoteheadItem{notehead.node, notehead.track, notehead.stave,
                      notehead.voice, note->id}});
    if (!previous_set.has_value())
      return std::nullopt;
    return Selection{*previous_set};
  }
  if (const auto* chord = std::get_if<Chord>(&previous)) {
    const auto previous_set = ChordSet::create(
        {ChordItem{notehead.node, notehead.track, notehead.stave,
                   notehead.voice, chord->id}});
    if (!previous_set.has_value())
      return std::nullopt;
    return Selection{*previous_set};
  }
  const auto previous_set =
      RestSet::create({RestItem{notehead.node, notehead.track, notehead.stave,
                                notehead.voice, event_id(previous)}});
  if (!previous_set.has_value())
    return std::nullopt;
  return Selection{*previous_set};
}

[[nodiscard]] const VoiceContent* resolve_voice_content(const Project& project,
                                                        NodeId         node_id,
                                                        TrackId        track_id,
                                                        StaveId        stave_id,
                                                        Voice          voice) {
  const Node* node = project.find_node(node_id);
  if (node == nullptr)
    return nullptr;
  const TrackLane* lane = node->lane(track_id);
  if (lane == nullptr)
    return nullptr;
  const StaveVoices* stave = lane->stave(stave_id);
  if (stave == nullptr)
    return nullptr;
  return &stave->voice(voice);
}

namespace {

// True when `id` names a GraceNote in `voice` -- the one NoteheadSet member
// kind make_convert_event_to_rest_command below must reject explicitly,
// since VoiceContent::position_of_event would otherwise resolve it through
// GraceGroup indirection to its principal's own position rather than to any
// position of the grace note's own.
[[nodiscard]] bool names_grace_note(const VoiceContent& voice,
                                    NotationEntityId    id) {
  for (const GraceGroup& group : voice.grace_groups()) {
    for (const GraceNote& grace : group.notes) {
      if (grace.id == id)
        return true;
    }
  }
  return false;
}

// The persistent id of the top-level event (Note, Chord, or Rest) starting
// exactly at `position`, or std::nullopt if none does. This is the id
// ConvertEventToRestCommand's replacement Rest preserves -- for a ChordNote
// selection that id is the owning Chord's own id, not the clicked
// ChordNote's, so selection_after_convert_to_rest below reads it here
// rather than assuming the selected item's own entity id.
[[nodiscard]] std::optional<NotationEntityId> top_level_event_id_at(
    const VoiceContent& voice, Rational position) {
  const std::optional<std::size_t> index = voice.find_event_index_at(position);
  if (!index.has_value() || *index >= voice.events().size())
    return std::nullopt;
  return event_id(voice.events()[*index]);
}

}  // namespace

// Constructs the reversible command for the keyboard action described in
// docs/plan/05-notation-editor.md M5-phase-23: "`R` converts the entire
// selected note/chord event to an equal-duration rest."
//
// Accepts a single-item NoteheadSet whose entity resolves to a top-level
// Note or to a ChordNote -- in the ChordNote case the WHOLE containing
// Chord converts, not just that one pitch, since the phase targets "the
// entire selected note/chord event" (this deliberately differs from
// make_delete_notehead_command's per-pitch semantics) -- or a single-item
// ChordSet. VoiceContent::position_of_event resolves either arm's entity to
// the exact Rational position ConvertEventToRestCommand addresses.
//
// Every other selection is a no-op returning nullptr: an empty or
// multi-item set on either accepted arm, any other Selection arm
// (RestSet -- already a rest -- MarkingSet, FullMeasureSet,
// ArbitraryRangeSet, InsertionCaretSet, NodeSet, ConnectorSet), no
// selection at all, a stale selection that no longer resolves, and a
// NoteheadSet entity that names a GraceNote (a grace note has no
// independent rhythmic duration, so there is no equal-duration rest for it
// to become). Building the command never mutates the project; the caller
// applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_convert_event_to_rest_command(
    const Project& project, const Selection& selection) {
  if (const auto* notehead_set = std::get_if<NoteheadSet>(&selection)) {
    if (notehead_set->items().size() != 1u)
      return nullptr;
    const NoteheadItem& item = notehead_set->items().front();
    if (!validate_selection(project, selection).empty())
      return nullptr;
    const VoiceContent* voice = resolve_voice_content(
        project, item.node, item.track, item.stave, item.voice);
    if (voice == nullptr || names_grace_note(*voice, item.entity))
      return nullptr;
    const std::optional<Rational> position =
        voice->position_of_event(item.entity);
    if (!position.has_value())
      return nullptr;
    return std::make_unique<ConvertEventToRestCommand>(
        item.node, item.track, item.stave, item.voice, *position);
  }

  if (const auto* chord_set = std::get_if<ChordSet>(&selection)) {
    if (chord_set->items().size() != 1u)
      return nullptr;
    const ChordItem& item = chord_set->items().front();
    if (!validate_selection(project, selection).empty())
      return nullptr;
    const VoiceContent* voice = resolve_voice_content(
        project, item.node, item.track, item.stave, item.voice);
    if (voice == nullptr)
      return nullptr;
    const std::optional<Rational> position =
        voice->position_of_event(item.entity);
    if (!position.has_value())
      return nullptr;
    return std::make_unique<ConvertEventToRestCommand>(
        item.node, item.track, item.stave, item.voice, *position);
  }

  return nullptr;
}

// Resolves the selection to hold after converting `selection`'s single
// note/chord event to a rest, using the state immediately before the
// conversion: a single-item RestSet addressing the converted event's
// persistent NotationEntityId, which ConvertEventToRestCommand preserves.
// For a ChordNote-addressed NoteheadSet item that id is the owning Chord's
// own id (the whole chord converts), not the clicked ChordNote's id, so it
// is read from the top-level event at the resolved position rather than
// assumed to be the selected item's own entity id. Returns std::nullopt
// under exactly the same conditions make_convert_event_to_rest_command
// returns nullptr for, so a caller that checks one before invoking the
// other never needs to check both.
[[nodiscard]] std::optional<Selection> selection_after_convert_to_rest(
    const Project& project, const Selection& selection) {
  if (const auto* notehead_set = std::get_if<NoteheadSet>(&selection)) {
    if (notehead_set->items().size() != 1u)
      return std::nullopt;
    const NoteheadItem& item = notehead_set->items().front();
    if (!validate_selection(project, selection).empty())
      return std::nullopt;
    const VoiceContent* voice = resolve_voice_content(
        project, item.node, item.track, item.stave, item.voice);
    if (voice == nullptr || names_grace_note(*voice, item.entity))
      return std::nullopt;
    const std::optional<Rational> position =
        voice->position_of_event(item.entity);
    if (!position.has_value())
      return std::nullopt;
    const std::optional<NotationEntityId> rest_id =
        top_level_event_id_at(*voice, *position);
    if (!rest_id.has_value())
      return std::nullopt;
    const auto rest_set = RestSet::create(
        {RestItem{item.node, item.track, item.stave, item.voice, *rest_id}});
    if (!rest_set.has_value())
      return std::nullopt;
    return Selection{*rest_set};
  }

  if (const auto* chord_set = std::get_if<ChordSet>(&selection)) {
    if (chord_set->items().size() != 1u)
      return std::nullopt;
    const ChordItem& item = chord_set->items().front();
    if (!validate_selection(project, selection).empty())
      return std::nullopt;
    const VoiceContent* voice = resolve_voice_content(
        project, item.node, item.track, item.stave, item.voice);
    if (voice == nullptr)
      return std::nullopt;
    const std::optional<Rational> position =
        voice->position_of_event(item.entity);
    if (!position.has_value())
      return std::nullopt;
    const std::optional<NotationEntityId> rest_id =
        top_level_event_id_at(*voice, *position);
    if (!rest_id.has_value())
      return std::nullopt;
    const auto rest_set = RestSet::create(
        {RestItem{item.node, item.track, item.stave, item.voice, *rest_id}});
    if (!rest_set.has_value())
      return std::nullopt;
    return Selection{*rest_set};
  }

  return std::nullopt;
}

}  // namespace graphscore
