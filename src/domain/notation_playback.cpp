// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/notation_playback.hpp>

#include <algorithm>
#include <optional>
#include <vector>

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

}  // namespace graphscore
