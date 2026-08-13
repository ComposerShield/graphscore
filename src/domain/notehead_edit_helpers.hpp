// SPDX-License-Identifier: Apache-2.0
//
// Internal helpers shared by the notehead-editing commands: notehead
// resolution by persistent id and tie-chain traversal. Both the diatonic
// step (MoveNoteheadCommand, M5-phase-20) and the accidental step
// (StepAccidentalCommand, M5-phase-21) edit one notehead's pitch in place
// and must carry a connected tie chain with it, so the traversal lives here
// once rather than in each command.
// Not installed; included only by src/domain/* command .cpp files.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {
namespace internal {

enum class NoteheadKind : std::uint8_t { kNote, kChordNote, kGraceNote };

struct NoteheadLocation {
  NoteheadKind kind        = NoteheadKind::kNote;
  std::size_t  event_index = 0;  // kNote/kChordNote: index into events()
  std::size_t  note_index  = 0;  // kChordNote/kGraceNote: index into notes
  std::size_t  grace_group_index = 0;  // kGraceNote: index into grace_groups()
  SpelledPitch pitch;
};

[[nodiscard]] inline std::optional<NoteheadLocation> find_notehead(
    const VoiceContent& voice, const NotationEntityId id) {
  const std::vector<VoiceEvent>& events = voice.events();
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (const auto* note = std::get_if<Note>(&events[i])) {
      if (note->id == id)
        return NoteheadLocation{NoteheadKind::kNote, i, 0, 0, note->pitch};
    }
    if (const auto* chord = std::get_if<Chord>(&events[i])) {
      for (std::size_t j = 0; j < chord->notes.size(); ++j) {
        if (chord->notes[j].id == id)
          return NoteheadLocation{NoteheadKind::kChordNote, i, j, 0,
                                  chord->notes[j].pitch};
      }
    }
  }

  const std::vector<GraceGroup>& groups = voice.grace_groups();
  for (std::size_t g = 0; g < groups.size(); ++g) {
    for (std::size_t j = 0; j < groups[g].notes.size(); ++j) {
      if (groups[g].notes[j].id == id)
        return NoteheadLocation{NoteheadKind::kGraceNote, 0, j, g,
                                groups[g].notes[j].pitch};
    }
  }
  return std::nullopt;
}

// The notehead's tie state for a note/chordnote address. A grace note has no
// tied_to_next field, so it never participates in a tie chain.
[[nodiscard]] inline bool notehead_tied_to_next(const VoiceEvent& event,
                                                std::size_t       note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note_index == 0 && note->tied_to_next;
  if (const auto* chord = std::get_if<Chord>(&event))
    return note_index < chord->notes.size() &&
           chord->notes[note_index].tied_to_next;
  return false;
}

// The number of addressable noteheads in `event`: 1 for a Note, the note
// count for a Chord, 0 for a Rest.
[[nodiscard]] inline std::size_t notehead_count(const VoiceEvent& event) {
  if (std::get_if<Note>(&event) != nullptr)
    return 1;
  if (const auto* chord = std::get_if<Chord>(&event))
    return chord->notes.size();
  return 0;
}

// The pitch of notehead `note_index` in `event`. `note_index` must be in
// range (callers guarantee it through notehead_count, which is 0 for a Rest,
// so this is never reached for one).
[[nodiscard]] inline SpelledPitch notehead_pitch(const VoiceEvent& event,
                                                 std::size_t       note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note->pitch;
  if (const auto* chord = std::get_if<Chord>(&event))
    return chord->notes[note_index].pitch;
  return SpelledPitch{};
}

// The persistent id of the notehead at `note_index` in `event` (a Note's own
// id, or the addressed ChordNote's id), or std::nullopt when the address is
// out of range or the event is a Rest.
[[nodiscard]] inline std::optional<NotationEntityId> notehead_id_at(
    const VoiceEvent& event, std::size_t note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note_index == 0 ? std::optional<NotationEntityId>(note->id)
                           : std::nullopt;
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (note_index < chord->notes.size())
      return chord->notes[note_index].id;
  }
  return std::nullopt;
}

// One exact address of a note/chordnote notehead: the event it lives in and
// the notehead index within that event. Ties are traversed over these
// addresses rather than over a pitch's first occurrence, so duplicate-pitch
// chords resolve the notehead that actually carries the tie.
struct NoteheadAddress {
  std::size_t event_index = 0;
  std::size_t note_index  = 0;

  [[nodiscard]] bool operator==(const NoteheadAddress&) const = default;
};

// One addressable note/chordnote notehead in a tie chain: the event it lives
// in, the notehead index within that event, and its (chain-invariant) pitch.
struct ChainNotehead {
  std::size_t  event_index;
  std::size_t  note_index;
  SpelledPitch pitch;
};

// Builds the complete tie chain through the selected notehead. The selected
// notehead must be a Note or ChordNote (not a GraceNote, which never ties).
//
// A tie edge connects notehead (e, n) whose tied_to_next is set to every
// notehead (e+1, m) in the immediately following event at the same pitch;
// the same edge is traversed backward from (e+1, m) to (e, n). The chain is
// the connected component of the selected notehead under these edges,
// visited with exact-address deduplication so duplicate-pitch chords never
// re-visit a member or stop at the first equal pitch instead of the one that
// carries the tie. Returns the chain in ascending (event, notehead) order.
// A dangling tie (a tied_to_next flag with no matching following pitch)
// yields only the members a valid walk reaches; VoiceContent::validate later
// rejects that state.
[[nodiscard]] inline std::vector<ChainNotehead> build_tie_chain(
    const VoiceContent& voice, const NoteheadLocation& selected) {
  const std::vector<VoiceEvent>& events = voice.events();
  const SpelledPitch             pitch  = selected.pitch;

  std::vector<ChainNotehead>   chain;
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
  add_member(selected.event_index, selected.note_index);
  frontier.push_back(
      NoteheadAddress{selected.event_index, selected.note_index});

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

}  // namespace internal
}  // namespace graphscore
