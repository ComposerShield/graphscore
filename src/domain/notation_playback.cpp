// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/notation_playback.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/domain/project.hpp>
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

[[nodiscard]] std::optional<std::size_t> top_level_event_index(
    const VoiceContent& voice, NotationEntityId event_id_value) {
  const std::vector<VoiceEvent>& events = voice.events();
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (event_id(events[index]) == event_id_value)
      return index;
  }
  return std::nullopt;
}

[[nodiscard]] Dynamic dynamic_at_or_before(const VoiceContent& voice,
                                           std::size_t         event_index,
                                           Dynamic default_dynamic) {
  Dynamic     result       = default_dynamic;
  std::size_t result_index = 0;
  bool        found        = false;
  for (const DynamicMarking& marking : voice.dynamics()) {
    const std::optional<std::size_t> marking_index =
        top_level_event_index(voice, marking.at_event);
    if (!marking_index.has_value() || *marking_index > event_index)
      continue;
    if (!found || *marking_index >= result_index) {
      result       = marking.value;
      result_index = *marking_index;
      found        = true;
    }
  }
  return result;
}

[[nodiscard]] std::optional<Dynamic> dynamic_at_event(const VoiceContent& voice,
                                                      std::size_t event_index) {
  std::optional<Dynamic> result;
  for (const DynamicMarking& marking : voice.dynamics()) {
    const std::optional<std::size_t> marking_index =
        top_level_event_index(voice, marking.at_event);
    if (marking_index.has_value() && *marking_index == event_index)
      result = marking.value;
  }
  return result;
}

[[nodiscard]] Rational event_onset(const std::vector<VoiceEvent>& events,
                                   std::size_t                    index) {
  Rational onset(0);
  for (std::size_t prior = 0; prior < index; ++prior)
    onset = onset + event_duration(events[prior]).resolved();
  return onset;
}

}  // namespace

std::vector<MidiCc64Event> pedal_span_cc64_events(
    std::span<const PedalSpan> spans, MidiChannel channel) {
  struct Endpoint {
    Rational position;
    int      delta = 0;
  };

  std::vector<Endpoint> endpoints;
  endpoints.reserve(spans.size() * 2);
  for (const PedalSpan& span : spans) {
    if (span.start >= span.end)
      continue;
    endpoints.push_back(Endpoint{span.start, 1});
    endpoints.push_back(Endpoint{span.end, -1});
  }

  std::sort(endpoints.begin(), endpoints.end(),
            [](const Endpoint& lhs, const Endpoint& rhs) {
              return lhs.position < rhs.position;
            });

  std::vector<MidiCc64Event> events;
  int                        active_spans = 0;
  std::size_t                index        = 0;
  while (index < endpoints.size()) {
    const Rational position = endpoints[index].position;
    int            delta    = 0;
    do {
      delta += endpoints[index].delta;
      ++index;
    } while (index < endpoints.size() && endpoints[index].position == position);

    const bool was_active = active_spans > 0;
    active_spans += delta;
    const bool is_active = active_spans > 0;
    if (was_active == is_active)
      continue;

    events.push_back(MidiCc64Event{
        position, channel,
        is_active ? MidiCc64Event::kDownValue : MidiCc64Event::kUpValue});
  }
  return events;
}

Rational event_sounded_duration(
    const VoiceEvent& event, bool is_tied,
    std::optional<Rational> slurred_gap_to_next_onset) {
  const Rational notated_duration = event_duration(event).resolved();
  const std::optional<Articulation> duration_articulation =
      find_duration_articulation(event);
  // A slur supplies legato overlap unless an explicit duration articulation
  // gives this event a different sounded-duration instruction. Accent and
  // marcato are velocity-only, so they do not suppress the slur.
  if (slurred_gap_to_next_onset.has_value() &&
      !duration_articulation.has_value())
    return legato_sounded_duration(notated_duration,
                                   *slurred_gap_to_next_onset);
  return sounded_duration_for_articulation(notated_duration,
                                           duration_articulation, is_tied);
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

std::optional<NoteOnVelocityContext> resolve_note_on_velocity_context(
    const VoiceContent& voice, NotationEntityId event_id_value,
    Dynamic project_default) {
  const std::optional<std::size_t> event_index =
      top_level_event_index(voice, event_id_value);
  if (!event_index.has_value())
    return std::nullopt;

  NoteOnVelocityContext context;
  context.governing_dynamic =
      dynamic_at_or_before(voice, *event_index, project_default);

  const std::vector<VoiceEvent>& events = voice.events();
  for (const Hairpin& hairpin : voice.hairpins()) {
    const std::optional<std::size_t> start_index =
        top_level_event_index(voice, hairpin.start_event);
    const std::optional<std::size_t> end_index =
        top_level_event_index(voice, hairpin.end_event);
    if (!start_index.has_value() || !end_index.has_value() ||
        *start_index >= *end_index || *start_index > *event_index ||
        *event_index > *end_index)
      continue;

    const Dynamic from_dynamic =
        dynamic_at_or_before(voice, *start_index, project_default);
    const std::optional<Dynamic> endpoint_dynamic =
        dynamic_at_event(voice, *end_index);
    const Dynamic to_dynamic =
        endpoint_dynamic.has_value()
            ? *endpoint_dynamic
            : (hairpin.direction == HairpinDirection::kCrescendo
                   ? Dynamic::kFff
                   : Dynamic::kPpp);

    const Rational start_position = event_onset(events, *start_index);
    const Rational end_position   = event_onset(events, *end_index);
    const Rational event_position = event_onset(events, *event_index);
    const Rational span           = end_position - start_position;
    if (span > Rational(0)) {
      context.hairpin = HairpinVelocityContext{
          velocity_for_dynamic(from_dynamic), velocity_for_dynamic(to_dynamic),
          (event_position - start_position) / span};
    }
    break;
  }
  return context;
}

std::optional<MidiVelocity> event_note_on_velocity(
    const VoiceContent& voice, NotationEntityId event_id_value,
    Dynamic project_default) {
  const std::optional<std::size_t> event_index =
      top_level_event_index(voice, event_id_value);
  if (!event_index.has_value())
    return std::nullopt;
  const std::optional<NoteOnVelocityContext> context =
      resolve_note_on_velocity_context(voice, event_id_value, project_default);
  if (!context.has_value())
    return std::nullopt;
  return event_note_on_velocity(voice.events()[*event_index],
                                context->governing_dynamic, context->hairpin);
}

std::optional<MidiVelocity> event_note_on_velocity(
    const Project& project, const VoiceContent& voice,
    NotationEntityId event_id_value) {
  return event_note_on_velocity(voice, event_id_value,
                                project.default_dynamic());
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
