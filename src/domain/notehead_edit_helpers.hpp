// SPDX-License-Identifier: Apache-2.0
//
// Internal helper shared by the notehead-editing commands: notehead
// resolution by persistent id. Both the diatonic step (MoveNoteheadCommand,
// M5-phase-20) and the accidental step (StepAccidentalCommand,
// M5-phase-21) resolve one selected NotationEntityId to an exact
// (event, notehead) address before editing its pitch in place.
//
// The tie-chain traversal those same commands then apply is NOT here: it is
// a cross-target public header (graphscore/domain/tie_chain.hpp), because
// the notation layer's keyboard staff step needs the identical walk. See
// that header for why.
//
// Not a public header; included only by src/domain/* command .cpp files.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/tie_chain.hpp>
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

}  // namespace internal
}  // namespace graphscore
