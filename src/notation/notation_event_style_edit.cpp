// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/notation_editing.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/event_style_command.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {
namespace {

struct EventTarget {
  NodeId            node;
  TrackId           track;
  StaveId           stave;
  Voice             voice;
  NotationEntityId  event;
  const VoiceEvent* value;
};

[[nodiscard]] bool valid_articulation(Articulation articulation) {
  return std::find(kAllArticulations.begin(), kAllArticulations.end(),
                   articulation) != kAllArticulations.end();
}

[[nodiscard]] bool valid_stem(StemDirection stem) {
  return stem == StemDirection::kAuto || stem == StemDirection::kUp ||
         stem == StemDirection::kDown;
}

[[nodiscard]] const VoiceContent* resolve_voice(const Project& project,
                                                NodeId         node_id,
                                                TrackId        track_id,
                                                StaveId        stave_id,
                                                Voice          voice_number) {
  const Node*        node  = project.find_node(node_id);
  const TrackLane*   lane  = node == nullptr ? nullptr : node->lane(track_id);
  const StaveVoices* stave = lane == nullptr ? nullptr : lane->stave(stave_id);
  return stave == nullptr ? nullptr : &stave->voice(voice_number);
}

[[nodiscard]] std::optional<EventTarget> resolve_top_level_event(
    const VoiceContent& voice, NodeId node_id, TrackId track_id,
    StaveId stave_id, Voice voice_number, NotationEntityId selected_id) {
  for (const VoiceEvent& event : voice.events()) {
    if (event_id(event) == selected_id &&
        !std::holds_alternative<Rest>(event)) {
      return EventTarget{node_id,      track_id,    stave_id,
                         voice_number, selected_id, &event};
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool names_grace_note(const VoiceContent& voice,
                                    NotationEntityId    selected_id) {
  return std::ranges::any_of(
      voice.grace_groups(), [&](const GraceGroup& group) {
        return std::ranges::any_of(group.notes, [&](const GraceNote& note) {
          return note.id == selected_id;
        });
      });
}

[[nodiscard]] std::optional<EventTarget> resolve_notehead_event(
    const VoiceContent& voice, NodeId node_id, TrackId track_id,
    StaveId stave_id, Voice voice_number, NotationEntityId selected_id) {
  if (const auto top_level = resolve_top_level_event(
          voice, node_id, track_id, stave_id, voice_number, selected_id);
      top_level.has_value() &&
      std::holds_alternative<Note>(*top_level->value)) {
    return top_level;
  }
  for (const VoiceEvent& event : voice.events()) {
    const auto* chord = std::get_if<Chord>(&event);
    if (chord != nullptr &&
        std::ranges::any_of(chord->notes, [&](const ChordNote& note) {
          return note.id == selected_id;
        })) {
      return EventTarget{node_id,      track_id,  stave_id,
                         voice_number, chord->id, &event};
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<EventTarget> resolve_selected_event(
    const Project& project, const Selection& selection) {
  if (!validate_selection(project, selection).empty())
    return std::nullopt;
  if (const auto* notes = std::get_if<NoteheadSet>(&selection);
      notes != nullptr && notes->items().size() == 1u) {
    const NoteheadItem& item = notes->items().front();
    const VoiceContent* voice =
        resolve_voice(project, item.node, item.track, item.stave, item.voice);
    return voice == nullptr
               ? std::nullopt
               : resolve_notehead_event(*voice, item.node, item.track,
                                        item.stave, item.voice, item.entity);
  }
  if (const auto* chords = std::get_if<ChordSet>(&selection);
      chords != nullptr && chords->items().size() == 1u) {
    const ChordItem&    item = chords->items().front();
    const VoiceContent* voice =
        resolve_voice(project, item.node, item.track, item.stave, item.voice);
    const auto target =
        voice == nullptr
            ? std::nullopt
            : resolve_top_level_event(*voice, item.node, item.track, item.stave,
                                      item.voice, item.entity);
    return target.has_value() && std::holds_alternative<Chord>(*target->value)
               ? target
               : std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::pair<EventTarget, Articulation>>
resolve_selected_articulation(const Project&   project,
                              const Selection& selection) {
  const auto* markings = std::get_if<MarkingSet>(&selection);
  if (markings == nullptr || markings->items().size() != 1u ||
      !validate_selection(project, selection).empty())
    return std::nullopt;
  const MarkingItem& item = markings->items().front();
  if (item.kind != MarkingKind::kArticulation || !item.voice.has_value() ||
      !item.articulation.has_value())
    return std::nullopt;
  const VoiceContent* voice =
      resolve_voice(project, item.node, item.track, item.stave, *item.voice);
  auto target = voice == nullptr ? std::nullopt
                                 : resolve_top_level_event(
                                       *voice, item.node, item.track,
                                       item.stave, *item.voice, item.anchor);
  if (!target.has_value())
    return std::nullopt;
  return std::pair{*target, *item.articulation};
}

[[nodiscard]] NotationEditCommandResult unavailable(std::string reason) {
  return {nullptr, std::move(reason)};
}

[[nodiscard]] bool selects_grace_note(const Project&   project,
                                      const Selection& selection) {
  const auto* notes = std::get_if<NoteheadSet>(&selection);
  if (notes == nullptr || notes->items().size() != 1u ||
      !validate_selection(project, selection).empty()) {
    return false;
  }
  const NoteheadItem& item = notes->items().front();
  const VoiceContent* voice =
      resolve_voice(project, item.node, item.track, item.stave, item.voice);
  return voice != nullptr && names_grace_note(*voice, item.entity);
}

}  // namespace

NotationEditCommandResult make_articulation_edit_command(
    const Project& project, const Selection& selection, ArticulationEdit edit,
    Articulation articulation) {
  if (edit != ArticulationEdit::kRemove && !valid_articulation(articulation))
    return unavailable("unknown articulation");
  if (selects_grace_note(project, selection))
    return unavailable("grace notes do not support event style editing");
  std::optional<EventTarget>  target;
  std::optional<Articulation> replaced;
  if (edit == ArticulationEdit::kApply) {
    target = resolve_selected_event(project, selection);
    if (!target.has_value())
      return unavailable("requires one live note or chord event");
  } else {
    const auto marking = resolve_selected_articulation(project, selection);
    if (!marking.has_value())
      return unavailable("requires one live articulation marking");
    target   = marking->first;
    replaced = marking->second;
    if (edit == ArticulationEdit::kRemove)
      articulation = *replaced;
  }
  const std::vector<Articulation>& values =
      *event_articulations(*target->value);
  if (edit != ArticulationEdit::kRemove &&
      std::find(values.begin(), values.end(), articulation) != values.end()) {
    return unavailable("articulation is already present");
  }
  if (edit != ArticulationEdit::kRemove &&
      is_duration_articulation(articulation) &&
      std::any_of(values.begin(), values.end(), [&](Articulation value) {
        return is_duration_articulation(value) && value != replaced;
      })) {
    return unavailable("conflicts with the existing duration articulation");
  }
  auto command = std::make_unique<EventStyleCommand>(
      target->node, target->track, target->stave, target->voice, target->event,
      edit, articulation, replaced);
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return unavailable("target changed or the edit is invalid");
  return {std::make_unique<EventStyleCommand>(
              target->node, target->track, target->stave, target->voice,
              target->event, edit, articulation, replaced),
          ""};
}

NotationEditCommandResult make_stem_edit_command(const Project&   project,
                                                 const Selection& selection,
                                                 StemDirection    stem) {
  if (!valid_stem(stem))
    return unavailable("unknown stem direction");
  if (selects_grace_note(project, selection))
    return unavailable("grace notes do not support event style editing");
  const auto target = resolve_selected_event(project, selection);
  if (!target.has_value())
    return unavailable("requires one live note or chord event");
  if (event_stem(*target->value) == stem)
    return unavailable("stem direction is already set");
  auto command = std::make_unique<EventStyleCommand>(
      target->node, target->track, target->stave, target->voice, target->event,
      stem);
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return unavailable("target changed or the edit is invalid");
  return {std::make_unique<EventStyleCommand>(target->node, target->track,
                                              target->stave, target->voice,
                                              target->event, stem),
          ""};
}

}  // namespace graphscore
