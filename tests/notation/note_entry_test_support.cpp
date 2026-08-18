// SPDX-License-Identifier: Apache-2.0

#include "note_entry_test_support.hpp"

namespace note_entry_test {

Measure measure(std::uint8_t numerator, std::uint16_t denominator) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 KeySignature{}};
}

Rest append_whole_rest(Fixture& fixture, std::uint8_t voice_index) {
  Rest rest = make_rest(*Duration::create(NoteValue::kWhole, 0));
  EXPECT_TRUE(fixture.voice(voice_index).append(rest).ok());
  return rest;
}

Note append_quarter_note(Fixture& fixture, const SpelledPitch& pitch,
                         std::uint8_t voice_index) {
  Note note = make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  EXPECT_TRUE(fixture.voice(voice_index).append(note).ok());
  return note;
}

NotePaletteEntrySpec armed(NoteValue            note_value,
                           NotePaletteEntryKind entry_kind,
                           std::uint8_t         voice_index) {
  const NotePaletteState state = *NotePaletteState::create(
      note_value, 0, entry_kind, *Voice::create(voice_index));
  return state.next_entry_spec();
}

GraceNote grace_note(const SpelledPitch& pitch) {
  return GraceNote{NotationEntityId::generate(), pitch,
                   *Duration::create(NoteValue::kEighth, 0),
                   GraceNoteType::kAcciaccatura, true};
}

}  // namespace note_entry_test
