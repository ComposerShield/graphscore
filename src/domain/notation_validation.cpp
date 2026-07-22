// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/notation_validation.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

std::vector<SpelledPitch> tied_pitches(const VoiceEvent& event) {
  std::vector<SpelledPitch> pitches;

  if (const auto* note = std::get_if<Note>(&event)) {
    if (note->tied_to_next)
      pitches.push_back(note->pitch);
    return pitches;
  }

  if (const auto* chord = std::get_if<Chord>(&event)) {
    for (const ChordNote& notehead : chord->notes) {
      if (notehead.tied_to_next)
        pitches.push_back(notehead.pitch);
    }
  }

  return pitches;
}

void append_tie_diagnostics(const std::vector<VoiceEvent>&   events,
                            std::vector<NotationDiagnostic>& diagnostics) {
  for (std::size_t i = 0; i < events.size(); ++i) {
    const std::vector<SpelledPitch> ties = tied_pitches(events[i]);
    if (ties.empty())
      continue;

    if (i + 1 >= events.size()) {
      diagnostics.push_back({event_id(events[i]),
                             NotationDiagnosticCode::kDanglingTie,
                             "tied note has no following event"});
      continue;
    }

    for (const SpelledPitch& pitch : ties) {
      if (!event_sounds_pitch(events[i + 1], pitch)) {
        diagnostics.push_back(
            {event_id(events[i]), NotationDiagnosticCode::kTiePitchMismatch,
             "tie target pitch not found in the following event"});
        break;
      }
    }
  }
}

void append_articulation_diagnostics(
    const std::vector<VoiceEvent>&   events,
    std::vector<NotationDiagnostic>& diagnostics) {
  for (const VoiceEvent& event : events) {
    const std::vector<Articulation>* articulations = event_articulations(event);
    if (articulations == nullptr)
      continue;

    int duration_articulation_count = 0;
    for (const Articulation articulation : *articulations) {
      if (is_duration_articulation(articulation))
        ++duration_articulation_count;
    }
    if (duration_articulation_count > 1) {
      diagnostics.push_back(
          {event_id(event),
           NotationDiagnosticCode::kConflictingDurationArticulation,
           "staccato/staccatissimo/tenuto are mutually exclusive on one "
           "event"});
    }
  }
}

std::unordered_map<NotationEntityId, std::size_t> index_events_by_id(
    const std::vector<VoiceEvent>& events) {
  std::unordered_map<NotationEntityId, std::size_t> positions;
  positions.reserve(events.size());
  for (std::size_t i = 0; i < events.size(); ++i)
    positions.emplace(event_id(events[i]), i);
  return positions;
}

void append_span_diagnostics(
    NotationEntityId span_id, NotationEntityId start_event,
    NotationEntityId                                         end_event,
    const std::unordered_map<NotationEntityId, std::size_t>& positions,
    NotationDiagnosticCode dangling_code, NotationDiagnosticCode order_code,
    std::vector<NotationDiagnostic>& diagnostics) {
  const auto start_it = positions.find(start_event);
  const auto end_it   = positions.find(end_event);
  if (start_it == positions.end() || end_it == positions.end()) {
    diagnostics.push_back({span_id, dangling_code,
                           "span endpoint is not an event in this voice"});
    return;
  }
  if (!(start_it->second < end_it->second)) {
    diagnostics.push_back(
        {span_id, order_code, "span start does not strictly precede its end"});
  }
}

void append_hairpin_diagnostics(
    const std::vector<Hairpin>&                              hairpins,
    const std::unordered_map<NotationEntityId, std::size_t>& positions,
    std::vector<NotationDiagnostic>&                         diagnostics) {
  for (const Hairpin& hairpin : hairpins) {
    append_span_diagnostics(
        hairpin.id, hairpin.start_event, hairpin.end_event, positions,
        NotationDiagnosticCode::kHairpinDanglingEndpoint,
        NotationDiagnosticCode::kHairpinNotOrdered, diagnostics);
  }
}

void append_slur_diagnostics(
    const std::vector<Slur>&                                 slurs,
    const std::unordered_map<NotationEntityId, std::size_t>& positions,
    std::vector<NotationDiagnostic>&                         diagnostics) {
  for (const Slur& slur : slurs) {
    append_span_diagnostics(
        slur.id, slur.start_event, slur.end_event, positions,
        NotationDiagnosticCode::kSlurDanglingEndpoint,
        NotationDiagnosticCode::kSlurNotOrdered, diagnostics);
  }
}

void append_beam_override_diagnostics(
    const std::vector<BeamOverride>&                         overrides,
    const std::vector<VoiceEvent>&                           events,
    const std::unordered_map<NotationEntityId, std::size_t>& positions,
    std::vector<NotationDiagnostic>&                         diagnostics) {
  for (const BeamOverride& beam_override : overrides) {
    if (beam_override.events.empty()) {
      diagnostics.push_back({beam_override.id,
                             NotationDiagnosticCode::kInvalidBeamOverride,
                             "beam override references no events"});
      continue;
    }

    std::vector<std::size_t> event_positions;
    event_positions.reserve(beam_override.events.size());
    bool valid = true;
    for (const NotationEntityId& referenced_id : beam_override.events) {
      const auto it = positions.find(referenced_id);
      if (it == positions.end() || !event_is_beamable(events[it->second])) {
        valid = false;
        break;
      }
      event_positions.push_back(it->second);
    }

    if (valid && event_positions.size() > 1) {
      std::sort(event_positions.begin(), event_positions.end());
      for (std::size_t i = 1; i < event_positions.size(); ++i) {
        if (event_positions[i] != event_positions[i - 1] + 1) {
          valid = false;
          break;
        }
      }
    }

    if (!valid) {
      diagnostics.push_back(
          {beam_override.id, NotationDiagnosticCode::kInvalidBeamOverride,
           "beam override references a non-existent, non-beamable, or "
           "non-adjacent event"});
    }
  }
}

void append_tuplet_diagnostics(const std::vector<VoiceEvent>&   events,
                               std::vector<NotationDiagnostic>& diagnostics) {
  std::size_t i = 0;
  while (i < events.size()) {
    const std::optional<TupletRatio>& ratio =
        event_duration(events[i]).tuplet();
    if (!ratio.has_value()) {
      ++i;
      continue;
    }

    const std::size_t run_start = i;
    Rational          resolved_sum(0);
    while (i < events.size()) {
      const std::optional<TupletRatio>& next_ratio =
          event_duration(events[i]).tuplet();
      if (!next_ratio.has_value() || !(*next_ratio == *ratio))
        break;
      resolved_sum = resolved_sum + event_duration(events[i]).resolved();
      ++i;
    }

    const Duration base_unit_duration =
        *Duration::create(event_duration(events[run_start]).base(), 0);
    const Rational multiple = resolved_sum / base_unit_duration.resolved();
    if (multiple.denominator() != 1 || multiple.numerator() <= 0) {
      diagnostics.push_back(
          {event_id(events[run_start]),
           NotationDiagnosticCode::kIncompleteTupletGroup,
           "tuplet run's summed length is not a whole multiple of its "
           "un-tupleted base unit"});
    }
  }
}

}  // namespace

std::vector<NotationDiagnostic> validate_voice_references(
    const VoiceContent& voice) {
  std::vector<NotationDiagnostic> diagnostics;
  const std::vector<VoiceEvent>&  events = voice.events();

  append_tie_diagnostics(events, diagnostics);
  append_articulation_diagnostics(events, diagnostics);

  const std::unordered_map<NotationEntityId, std::size_t> positions =
      index_events_by_id(events);
  append_hairpin_diagnostics(voice.hairpins(), positions, diagnostics);
  append_slur_diagnostics(voice.slurs(), positions, diagnostics);
  append_beam_override_diagnostics(voice.beam_overrides(), events, positions,
                                   diagnostics);
  append_tuplet_diagnostics(events, diagnostics);

  return diagnostics;
}

std::vector<NotationDiagnostic> validate_pedal_spans(
    const std::vector<PedalSpan>& spans, Rational node_end) {
  std::vector<NotationDiagnostic> diagnostics;
  for (const PedalSpan& span : spans) {
    if (!(span.start < span.end)) {
      diagnostics.push_back(
          {span.id, NotationDiagnosticCode::kPedalSpanNotOrdered,
           "pedal span start does not strictly precede its end"});
      continue;
    }
    if (span.start < Rational(0) || span.end > node_end) {
      diagnostics.push_back(
          {span.id, NotationDiagnosticCode::kPedalSpanOutOfRange,
           "pedal span falls outside the stave's node timeline range"});
    }
  }
  return diagnostics;
}

std::vector<NotationDiagnostic> validate_lane_references(const TrackLane& lane,
                                                         Rational node_end) {
  std::vector<NotationDiagnostic> diagnostics;

  for (const StaveId stave_id : lane.stave_ids()) {
    const StaveVoices* voices = lane.stave(stave_id);
    if (voices == nullptr)
      continue;

    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
      const std::vector<NotationDiagnostic> voice_diagnostics =
          validate_voice_references(voices->voice(*Voice::create(v)));
      diagnostics.insert(diagnostics.end(), voice_diagnostics.begin(),
                         voice_diagnostics.end());
    }

    if (const std::vector<PedalSpan>* spans = lane.pedal_spans(stave_id)) {
      const std::vector<NotationDiagnostic> pedal_diagnostics =
          validate_pedal_spans(*spans, node_end);
      diagnostics.insert(diagnostics.end(), pedal_diagnostics.begin(),
                         pedal_diagnostics.end());
    }
  }

  return diagnostics;
}

}  // namespace graphscore
