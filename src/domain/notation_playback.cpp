// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/notation_playback.hpp>

#include <algorithm>
#include <optional>
#include <vector>

#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

namespace {

[[nodiscard]] std::optional<Articulation> find_duration_articulation(
    const VoiceEvent& event) {
  const std::vector<Articulation>* articulations = event_articulations(event);
  if (articulations == nullptr)
    return std::nullopt;
  for (Articulation articulation : *articulations) {
    if (is_duration_articulation(articulation))
      return articulation;
  }
  return std::nullopt;
}

[[nodiscard]] bool has_articulation(const VoiceEvent& event,
                                    Articulation      target) {
  const std::vector<Articulation>* articulations = event_articulations(event);
  if (articulations == nullptr)
    return false;
  return std::find(articulations->begin(), articulations->end(), target) !=
         articulations->end();
}

[[nodiscard]] GraceNoteType group_grace_note_type(const GraceGroup& group) {
  if (group.notes.empty())
    return GraceNoteType::kAppoggiatura;
  return group.notes.front().type;
}

[[nodiscard]] const VoiceEvent* find_preceding_sounding_event(
    const VoiceContent& voice, const GraceGroup& group) {
  const std::vector<VoiceEvent>& events = voice.events();
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (event_id(events[index]) != group.principal_event)
      continue;
    if (index == 0 || std::holds_alternative<Rest>(events[index - 1]))
      return nullptr;
    return &events[index - 1];
  }
  return nullptr;
}

}  // namespace

Rational event_sounded_duration(
    const VoiceEvent& event, bool is_tied,
    std::optional<Rational> slurred_gap_to_next_onset) {
  const Rational notated_duration = event_duration(event).resolved();
  if (slurred_gap_to_next_onset.has_value())
    return legato_sounded_duration(notated_duration,
                                   *slurred_gap_to_next_onset);
  return sounded_duration_for_articulation(
      notated_duration, find_duration_articulation(event), is_tied);
}

MidiVelocity event_note_on_velocity(
    const VoiceEvent& event, Dynamic governing_dynamic,
    std::optional<HairpinVelocityContext> hairpin) {
  const MidiVelocity base =
      hairpin.has_value() ? interpolate_hairpin_velocity(
                                hairpin->from, hairpin->to, hairpin->position)
                          : velocity_for_dynamic(governing_dynamic);
  const bool accent  = has_articulation(event, Articulation::kAccent);
  const bool marcato = has_articulation(event, Articulation::kMarcato);
  return apply_emphasis(base, accent, marcato);
}

std::vector<Rational> grace_group_steal_durations(const GraceGroup& group,
                                                  Rational available_duration) {
  return grace_steal_durations(group_grace_note_type(group), group.notes.size(),
                               available_duration);
}

Rational grace_group_remaining_preceding_duration(const GraceGroup& group,
                                                  Rational available_duration) {
  return grace_steal_remaining_duration(group_grace_note_type(group),
                                        group.notes.size(), available_duration);
}

Rational grace_group_preceding_available_duration(const VoiceContent& voice,
                                                  const GraceGroup&   group) {
  const VoiceEvent* preceding = find_preceding_sounding_event(voice, group);
  if (preceding == nullptr)
    return Rational(0);
  return event_duration(*preceding).resolved();
}

std::vector<Rational> grace_group_steal_durations(const VoiceContent& voice,
                                                  const GraceGroup&   group) {
  return grace_group_steal_durations(
      group, grace_group_preceding_available_duration(voice, group));
}

Rational grace_group_remaining_preceding_duration(const VoiceContent& voice,
                                                  const GraceGroup&   group) {
  return grace_group_remaining_preceding_duration(
      group, grace_group_preceding_available_duration(voice, group));
}

}  // namespace graphscore
