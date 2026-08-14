// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_interval_command.hpp>
#include <graphscore/domain/move_notehead_command.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_editing.hpp>

#include "note_entry_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace graphscore {

std::vector<Articulation> NotePaletteState::armed_articulations() const {
  std::vector<Articulation> armed;
  for (const Articulation articulation : kAllArticulations) {
    if (has_articulation(articulation))
      armed.push_back(articulation);
  }
  return armed;
}

NotePaletteEntrySpec NotePaletteState::next_entry_spec() const {
  return NotePaletteEntrySpec{
      .duration      = resolved_duration(),
      .entry_kind    = entry_kind(),
      .voice         = voice(),
      .articulations = armed_articulations(),
      .dynamic       = dynamic(),
      .hairpin       = hairpin_direction(),
      .tie_to_next   = tie_to_next_armed(),
      .slur          = slur_armed(),
      .pedal         = pedal_armed(),
      .beam_override = beam_override_kind(),
  };
}

std::optional<NoteAuditionRequest> audition_for_notehead_move(
    const Project& project, const NoteheadItem& notehead,
    NoteheadStepDirection direction) {
  // The same notehead-arm check make_move_notehead_command uses, so an
  // invalid or stale notehead auditions nothing exactly when the command
  // would build nothing.
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value())
    return std::nullopt;
  if (!validate_selection(project, Selection{*set}).empty())
    return std::nullopt;

  // Resolve the notehead's current spelling. validate_selection already
  // proved the id names a Note, ChordNote, or GraceNote in the addressed
  // voice; the scan here recovers its SpelledPitch (the same value
  // MoveNoteheadCommand's own resolution reads), so the audited post-edit
  // pitch can never disagree with the pitch the command writes.
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

  std::optional<SpelledPitch> current;
  for (const VoiceEvent& event : voice.events()) {
    if (const auto* n = std::get_if<Note>(&event)) {
      if (n->id == notehead.entity)
        current = n->pitch;
    } else if (const auto* c = std::get_if<Chord>(&event)) {
      for (const ChordNote& chord_note : c->notes) {
        if (chord_note.id == notehead.entity)
          current = chord_note.pitch;
      }
    }
    if (current.has_value())
      break;
  }
  if (!current.has_value()) {
    for (const GraceGroup& group : voice.grace_groups()) {
      for (const GraceNote& grace : group.notes) {
        if (grace.id == notehead.entity)
          current = grace.pitch;
      }
      if (current.has_value())
        break;
    }
  }
  if (!current.has_value())
    return std::nullopt;

  const std::optional<SpelledPitch> target =
      step_notehead_pitch(*current, direction);
  if (!target.has_value())
    return std::nullopt;
  const std::optional<MidiPitch> midi = target->to_midi_pitch();
  if (!midi.has_value())
    return std::nullopt;

  NoteAuditionRequest request;
  request.track_id = notehead.track;
  request.velocity = velocity_for_dynamic(project.default_dynamic());
  request.pitches.push_back(*midi);
  return request;
}

std::unique_ptr<Command> make_step_accidental_command(
    const Project& project, const NoteheadItem& notehead,
    AccidentalStepDirection direction) {
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value())
    return nullptr;
  if (!validate_selection(project, Selection{*set}).empty())
    return nullptr;
  return std::make_unique<StepAccidentalCommand>(notehead.node, notehead.track,
                                                 notehead.stave, notehead.voice,
                                                 notehead.entity, direction);
}

std::optional<NoteAuditionRequest> audition_for_accidental_step(
    const Project& project, const NoteheadItem& notehead,
    AccidentalStepDirection direction) {
  // The same notehead-arm check make_step_accidental_command uses, so an
  // invalid or stale notehead auditions nothing exactly when the command
  // would build nothing.
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value())
    return std::nullopt;
  if (!validate_selection(project, Selection{*set}).empty())
    return std::nullopt;

  // Resolve the notehead's current spelling. validate_selection already
  // proved the id names a Note, ChordNote, or GraceNote in the addressed
  // voice; the scan here recovers its SpelledPitch (the same value
  // StepAccidentalCommand's own resolution reads), so the audited post-edit
  // pitch can never disagree with the pitch the command writes.
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

  std::optional<SpelledPitch> current;
  for (const VoiceEvent& event : voice.events()) {
    if (const auto* n = std::get_if<Note>(&event)) {
      if (n->id == notehead.entity)
        current = n->pitch;
    } else if (const auto* c = std::get_if<Chord>(&event)) {
      for (const ChordNote& chord_note : c->notes) {
        if (chord_note.id == notehead.entity)
          current = chord_note.pitch;
      }
    }
    if (current.has_value())
      break;
  }
  if (!current.has_value()) {
    for (const GraceGroup& group : voice.grace_groups()) {
      for (const GraceNote& grace : group.notes) {
        if (grace.id == notehead.entity)
          current = grace.pitch;
      }
      if (current.has_value())
        break;
    }
  }
  if (!current.has_value())
    return std::nullopt;

  const std::optional<SpelledPitch> target =
      step_notehead_accidental(*current, direction);
  if (!target.has_value())
    return std::nullopt;
  const std::optional<MidiPitch> midi = target->to_midi_pitch();
  if (!midi.has_value())
    return std::nullopt;

  NoteAuditionRequest request;
  request.track_id = notehead.track;
  request.velocity = velocity_for_dynamic(project.default_dynamic());
  request.pitches.push_back(*midi);
  return request;
}

std::unique_ptr<Command> make_add_interval_command(
    const Project& project, const NoteheadItem& notehead, std::uint8_t interval,
    IntervalDirection direction) {
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value())
    return nullptr;
  if (!validate_selection(project, Selection{*set}).empty())
    return nullptr;
  // notehead_key_signature rejects exactly the sources that have no key
  // signature to spell against: a GraceNote (no rhythmic event), a stale id,
  // a node with no timeline, or an onset outside the main region.
  if (!notehead_key_signature(project, notehead).has_value())
    return nullptr;
  return std::make_unique<AddIntervalCommand>(
      notehead.node, notehead.track, notehead.stave, notehead.voice,
      notehead.entity, interval, direction);
}

std::optional<NoteAuditionRequest> audition_for_add_interval(
    const Project& project, const NoteheadItem& notehead, std::uint8_t interval,
    IntervalDirection direction) {
  // The same source check make_add_interval_command uses, so an invalid,
  // stale, or grace notehead auditions nothing exactly when the command would
  // build nothing.
  const std::optional<NoteheadSet> set = NoteheadSet::create({notehead});
  if (!set.has_value())
    return std::nullopt;
  if (!validate_selection(project, Selection{*set}).empty())
    return std::nullopt;
  const std::optional<KeySignature> key =
      notehead_key_signature(project, notehead);
  if (!key.has_value())
    return std::nullopt;

  // Resolve the notehead's current spelling and the event it sits in, so the
  // audited sounding set can never disagree with the event AddIntervalCommand
  // mutates. validate_selection already proved the id names a Note or
  // ChordNote in the addressed voice; the scan here recovers its SpelledPitch
  // and, for a chord, the whole chord's pitches.
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

  std::optional<SpelledPitch> source_pitch;
  bool                        chord_source = false;
  std::vector<SpelledPitch>   chord_pitches;
  for (const VoiceEvent& event : voice.events()) {
    if (const auto* n = std::get_if<Note>(&event)) {
      if (n->id == notehead.entity) {
        source_pitch = n->pitch;
        break;
      }
    } else if (const auto* c = std::get_if<Chord>(&event)) {
      const auto it =
          std::ranges::find_if(c->notes, [&](const ChordNote& chord_note) {
            return chord_note.id == notehead.entity;
          });
      if (it != c->notes.end()) {
        source_pitch = it->pitch;
        chord_source = true;
        chord_pitches.reserve(c->notes.size());
        for (const ChordNote& chord_note : c->notes)
          chord_pitches.push_back(chord_note.pitch);
        break;
      }
    }
  }
  if (!source_pitch.has_value())
    return std::nullopt;

  const std::optional<SpelledPitch> target =
      interval_target_pitch(*source_pitch, interval, direction, *key);
  if (!target.has_value())
    return std::nullopt;

  std::vector<SpelledPitch> sounding;
  sounding.push_back(*target);
  if (chord_source) {
    // Duplicate-pitch rejection, mirroring AddIntervalCommand: a repeated
    // interval that would create the same spelled pitch sounds nothing.
    if (std::ranges::any_of(chord_pitches, [&](const SpelledPitch& pitch) {
          return pitch == *target;
        }))
      return std::nullopt;
    for (const SpelledPitch& pitch : chord_pitches)
      sounding.push_back(pitch);
  } else {
    sounding.push_back(*source_pitch);
  }

  // The newly inserted pitch failing to convert leaves nothing meaningful to
  // audition, so the whole request is dropped (mirroring
  // audition_for_note_entry).
  if (!target->to_midi_pitch().has_value())
    return std::nullopt;

  NoteAuditionRequest request;
  request.track_id = notehead.track;
  request.velocity = velocity_for_dynamic(project.default_dynamic());
  request.pitches.reserve(sounding.size());
  for (const SpelledPitch& pitch : sounding) {
    const std::optional<MidiPitch> midi = pitch.to_midi_pitch();
    if (midi.has_value())
      request.pitches.push_back(*midi);
  }

  // NoteAuditionRequest's contract: ascending MidiPitch order, deduplicated
  // so enharmonic spellings collapsing to one MidiPitch sound once.
  std::ranges::sort(request.pitches);
  const auto duplicates = std::ranges::unique(request.pitches);
  request.pitches.erase(duplicates.begin(), duplicates.end());
  return request;
}

std::optional<NoteAuditionRequest> audition_for_note_entry(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch) {
  const std::optional<NoteEntryResolution> resolution = resolve_note_entry(
      project, node_id, track_id, stave_id, position, armed, candidate_pitch);
  // An empty sounding set is exactly the "nothing newly sounds" case: both
  // duration-only branches and every kRest branch.
  if (!resolution.has_value() || resolution->sounding_pitches.empty())
    return std::nullopt;

  // The newly inserted pitch failing to convert leaves nothing meaningful
  // to audition, so the whole request is dropped rather than reduced to the
  // retained chord pitches.
  if (!resolution->inserted_pitch().to_midi_pitch().has_value())
    return std::nullopt;

  NoteAuditionRequest request;
  request.track_id = track_id;
  request.velocity = velocity_for_dynamic(project.default_dynamic());
  request.pitches.reserve(resolution->sounding_pitches.size());
  for (const SpelledPitch& pitch : resolution->sounding_pitches) {
    const std::optional<MidiPitch> midi = pitch.to_midi_pitch();
    // A pre-existing chord pitch that cannot sound is silently skipped.
    if (midi.has_value())
      request.pitches.push_back(*midi);
  }

  // NoteAuditionRequest's contract: ascending MidiPitch order, deduplicated
  // so enharmonic spellings collapsing to one MidiPitch sound once.
  std::ranges::sort(request.pitches);
  const auto duplicates = std::ranges::unique(request.pitches);
  request.pitches.erase(duplicates.begin(), duplicates.end());
  return request;
}

}  // namespace graphscore
