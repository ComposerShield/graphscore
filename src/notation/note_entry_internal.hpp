// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/voice.hpp>
#include <graphscore/domain/notation_event.hpp>

namespace graphscore {
class Project;
class VoiceContent;
struct NotePaletteEntrySpec;
enum class NoteEntryBranch : std::uint8_t {
  kCreateStreamRest,
  kCreateStreamNote,
  kRestDurationOnly,
  kEventToRest,
  kRestToNote,
  kNoteDurationOnly,
  kNoteToChord,
  kChordDurationOnly,
  kChordExtension
};

struct NoteEntryResolution {
  NoteEntryBranch           branch   = NoteEntryBranch::kCreateStreamRest;
  const VoiceEvent*         existing = nullptr;
  std::vector<SpelledPitch> sounding_pitches;

  [[nodiscard]] const SpelledPitch& inserted_pitch() const {
    assert(!sounding_pitches.empty());
    return sounding_pitches.front();
  }
};

[[nodiscard]] std::optional<NoteEntryResolution> resolve_note_entry(
    const Project&, NodeId, TrackId, StaveId, Rational,
    const NotePaletteEntrySpec&, const std::optional<SpelledPitch>&);
[[nodiscard]] const VoiceContent* resolve_voice_content(const Project&, NodeId,
                                                        TrackId, StaveId,
                                                        Voice);
}  // namespace graphscore
