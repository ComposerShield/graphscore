// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/notation_event.hpp>

#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

Note make_note(SpelledPitch pitch, Duration duration, bool tied_to_next) {
  return Note{NotationEntityId::generate(), pitch, duration, tied_to_next};
}

Chord make_chord(Duration duration, std::vector<ChordNote> notes) {
  return Chord{NotationEntityId::generate(), duration, std::move(notes)};
}

Rest make_rest(Duration duration) {
  return Rest{NotationEntityId::generate(), duration};
}

const Duration& event_duration(const VoiceEvent& event) {
  return std::visit([](const auto& e) -> const Duration& { return e.duration; },
                    event);
}

NotationEntityId event_id(const VoiceEvent& event) {
  return std::visit([](const auto& e) { return e.id; }, event);
}

bool event_sounds_pitch(const VoiceEvent& event, const SpelledPitch& pitch) {
  if (const auto* note = std::get_if<Note>(&event))
    return note->pitch == pitch;

  if (const auto* chord = std::get_if<Chord>(&event)) {
    for (const ChordNote& notehead : chord->notes) {
      if (notehead.pitch == pitch)
        return true;
    }
    return false;
  }

  return false;
}

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

}  // namespace

Result validate_ties(const std::vector<VoiceEvent>& events) {
  for (std::size_t i = 0; i < events.size(); ++i) {
    const std::vector<SpelledPitch> ties = tied_pitches(events[i]);
    if (ties.empty())
      continue;

    if (i + 1 >= events.size())
      return Result(ResultCode::kInvalidArgument);

    for (const SpelledPitch& pitch : ties) {
      if (!event_sounds_pitch(events[i + 1], pitch))
        return Result(ResultCode::kInvalidArgument);
    }
  }

  return Result();
}

}  // namespace graphscore
