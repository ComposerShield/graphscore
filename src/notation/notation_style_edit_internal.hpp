// SPDX-License-Identifier: Apache-2.0
//
// Selection-to-event resolution shared by the toolkit-neutral style-edit
// factories (notation_event_style_edit.cpp, notation_marking_style_edit.cpp).
// Not installed; included only by src/notation/* translation units.

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <graphscore/domain/project.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/notation/notation_editing.hpp>

namespace graphscore {
namespace style_edit {

// One resolved top-level Note/Chord event and the exact scope addressing it.
// `value` points into the project the resolution ran against.
struct EventTarget {
  NodeId            node;
  TrackId           track;
  StaveId           stave;
  Voice             voice;
  NotationEntityId  event;
  const VoiceEvent* value;
};

[[nodiscard]] inline NotationEditCommandResult unavailable(std::string reason) {
  return {nullptr, std::move(reason)};
}

[[nodiscard]] inline const VoiceContent* resolve_voice(const Project& project,
                                                       NodeId         node_id,
                                                       TrackId        track_id,
                                                       StaveId        stave_id,
                                                       Voice voice_number) {
  const Node*        node  = project.find_node(node_id);
  const TrackLane*   lane  = node == nullptr ? nullptr : node->lane(track_id);
  const StaveVoices* stave = lane == nullptr ? nullptr : lane->stave(stave_id);
  return stave == nullptr ? nullptr : &stave->voice(voice_number);
}

// The top-level non-rest event `selected_id` names directly, if any.
[[nodiscard]] inline std::optional<EventTarget> resolve_top_level_event(
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

[[nodiscard]] inline bool names_grace_note(const VoiceContent& voice,
                                           NotationEntityId    selected_id) {
  return std::ranges::any_of(
      voice.grace_groups(), [&](const GraceGroup& group) {
        return std::ranges::any_of(group.notes, [&](const GraceNote& note) {
          return note.id == selected_id;
        });
      });
}

// The top-level event a notehead selection addresses: the Note itself, or the
// Chord that owns the selected ChordNote.
[[nodiscard]] inline std::optional<EventTarget> resolve_notehead_event(
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

// The single live Note/Chord event a one-item NoteheadSet or ChordSet names.
[[nodiscard]] inline std::optional<EventTarget> resolve_selected_event(
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

// Whether `selection` is a one-item notehead selection naming a grace note --
// the case every event-anchored style factory rejects with its own reason
// rather than silently resolving to the grace note's principal event.
[[nodiscard]] inline bool selects_grace_note(const Project&   project,
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

}  // namespace style_edit
}  // namespace graphscore
