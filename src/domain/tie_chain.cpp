// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/tie_chain.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

namespace {

// The notehead's tie state for a note/chordnote address. A grace note has
// no tied_to_next field, so it never participates in a tie chain.
[[nodiscard]] bool notehead_tied_to_next(const VoiceEvent& event,
                                         std::size_t       note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note_index == 0 && note->tied_to_next;
  if (const auto* chord = std::get_if<Chord>(&event))
    return note_index < chord->notes.size() &&
           chord->notes[note_index].tied_to_next;
  return false;
}

// The pitch of notehead `note_index` in `event`. `note_index` must be in
// range (callers guarantee it through notehead_count, which is 0 for a
// Rest, so this is never reached for one).
[[nodiscard]] SpelledPitch notehead_pitch(const VoiceEvent& event,
                                          std::size_t       note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note->pitch;
  if (const auto* chord = std::get_if<Chord>(&event))
    return chord->notes[note_index].pitch;
  return SpelledPitch{};
}

// One exact address of a note/chordnote notehead: the event it lives in
// and the notehead index within that event. Ties are traversed over these
// addresses rather than over a pitch's first occurrence, so
// duplicate-pitch chords resolve the notehead that actually carries the
// tie.
struct NoteheadAddress {
  std::size_t event_index = 0;
  std::size_t note_index  = 0;
};

}  // namespace

std::size_t notehead_count(const VoiceEvent& event) {
  if (std::get_if<Note>(&event) != nullptr)
    return 1;
  if (const auto* chord = std::get_if<Chord>(&event))
    return chord->notes.size();
  return 0;
}

std::optional<NotationEntityId> notehead_id_at(const VoiceEvent& event,
                                               std::size_t       note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note_index == 0 ? std::optional<NotationEntityId>(note->id)
                           : std::nullopt;
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (note_index < chord->notes.size())
      return chord->notes[note_index].id;
  }
  return std::nullopt;
}

std::vector<ChainNotehead> build_tie_chain(const VoiceContent& voice,
                                           std::size_t         event_index,
                                           std::size_t         note_index) {
  const std::vector<VoiceEvent>& events = voice.events();
  std::vector<ChainNotehead>     chain;
  if (event_index >= events.size() ||
      note_index >= notehead_count(events[event_index]))
    return chain;

  const SpelledPitch pitch = notehead_pitch(events[event_index], note_index);

  std::vector<NoteheadAddress> visited;

  const auto add_member = [&](std::size_t ei, std::size_t ni) {
    for (const NoteheadAddress& address : visited) {
      if (address.event_index == ei && address.note_index == ni)
        return false;
    }
    visited.push_back(NoteheadAddress{ei, ni});
    chain.push_back(ChainNotehead{ei, ni, pitch});
    return true;
  };

  std::vector<NoteheadAddress> frontier;
  add_member(event_index, note_index);
  frontier.push_back(NoteheadAddress{event_index, note_index});

  while (!frontier.empty()) {
    const NoteheadAddress current = frontier.back();
    frontier.pop_back();

    // Backward: any previous notehead whose tie flag is set at the same
    // pitch ties into the current notehead -- not merely the first previous
    // notehead at that pitch, so a duplicate carrying the tie is found even
    // when an earlier duplicate does not.
    if (current.event_index > 0) {
      const std::size_t previous = current.event_index - 1;
      for (std::size_t j = 0; j < notehead_count(events[previous]); ++j) {
        if (notehead_tied_to_next(events[previous], j) &&
            notehead_pitch(events[previous], j) == pitch) {
          if (add_member(previous, j))
            frontier.push_back(NoteheadAddress{previous, j});
        }
      }
    }

    // Forward: the current notehead's tie continues into every following
    // notehead at the same pitch.
    if (notehead_tied_to_next(events[current.event_index],
                              current.note_index) &&
        current.event_index + 1 < events.size()) {
      const std::size_t next = current.event_index + 1;
      for (std::size_t j = 0; j < notehead_count(events[next]); ++j) {
        if (notehead_pitch(events[next], j) == pitch) {
          if (add_member(next, j))
            frontier.push_back(NoteheadAddress{next, j});
        }
      }
    }
  }

  std::sort(chain.begin(), chain.end(),
            [](const ChainNotehead& a, const ChainNotehead& b) {
              if (a.event_index != b.event_index)
                return a.event_index < b.event_index;
              return a.note_index < b.note_index;
            });
  return chain;
}

}  // namespace graphscore
