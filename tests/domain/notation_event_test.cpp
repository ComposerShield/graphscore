// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Accidental;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Duration;
using graphscore::event_duration;
using graphscore::event_id;
using graphscore::event_sounds_pitch;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_note;
using graphscore::make_rest;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::Rest;
using graphscore::SpelledPitch;
using graphscore::validate_ties;
using graphscore::VoiceEvent;

namespace {

SpelledPitch pitch(Letter letter, std::int8_t octave = 4,
                   Accidental accidental = Accidental::kNatural) {
  return *SpelledPitch::create(letter, octave, accidental);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

}  // namespace

TEST(NotationEventTest, MakeNoteGeneratesStableId) {
  const Note note = make_note(pitch(Letter::kC), quarter());
  EXPECT_FALSE(note.tied_to_next);
  EXPECT_EQ(note.pitch, pitch(Letter::kC));
  const VoiceEvent event = note;
  EXPECT_EQ(event_id(event), note.id);
  EXPECT_EQ(event_duration(event), quarter());
}

TEST(NotationEventTest, MakeChordHoldsEachNoteheadIndependently) {
  const Chord chord =
      make_chord(quarter(), {ChordNote{pitch(Letter::kC), false},
                             ChordNote{pitch(Letter::kE), true}});
  ASSERT_EQ(chord.notes.size(), 2u);
  EXPECT_FALSE(chord.notes[0].tied_to_next);
  EXPECT_TRUE(chord.notes[1].tied_to_next);
}

TEST(NotationEventTest, EventSoundsPitchMatchesNoteAndChordNotehead) {
  const VoiceEvent note_event = make_note(pitch(Letter::kC), quarter());
  EXPECT_TRUE(event_sounds_pitch(note_event, pitch(Letter::kC)));
  EXPECT_FALSE(event_sounds_pitch(note_event, pitch(Letter::kD)));

  const VoiceEvent chord_event = make_chord(
      quarter(), {ChordNote{pitch(Letter::kC)}, ChordNote{pitch(Letter::kG)}});
  EXPECT_TRUE(event_sounds_pitch(chord_event, pitch(Letter::kG)));
  EXPECT_FALSE(event_sounds_pitch(chord_event, pitch(Letter::kA)));
}

TEST(NotationEventTest, RestNeverSoundsAPitch) {
  const VoiceEvent rest_event = make_rest(quarter());
  EXPECT_FALSE(event_sounds_pitch(rest_event, pitch(Letter::kC)));
}

TEST(TieValidationTest, NoTiesIsValid) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), quarter()),
      make_note(pitch(Letter::kD), quarter()),
  };
  EXPECT_TRUE(validate_ties(events).ok());
}

TEST(TieValidationTest, TieToMatchingPitchIsValid) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kC), quarter()),
  };
  EXPECT_TRUE(validate_ties(events).ok());
}

TEST(TieValidationTest, TieToNonMatchingPitchIsFlagged) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kD), quarter()),
  };
  EXPECT_FALSE(validate_ties(events).ok());
}

TEST(TieValidationTest, TieOnLastEventIsFlagged) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true),
  };
  EXPECT_FALSE(validate_ties(events).ok());
}

TEST(TieValidationTest, ChordNoteheadTiesIndependently) {
  const std::vector<VoiceEvent> events = {
      make_chord(quarter(), {ChordNote{pitch(Letter::kC), true},
                             ChordNote{pitch(Letter::kE), false}}),
      make_chord(quarter(), {ChordNote{pitch(Letter::kC), false},
                             ChordNote{pitch(Letter::kG), false}}),
  };
  EXPECT_TRUE(validate_ties(events).ok());
}

TEST(TieValidationTest, ChordNoteheadTieToMissingPitchIsFlagged) {
  const std::vector<VoiceEvent> events = {
      make_chord(quarter(), {ChordNote{pitch(Letter::kC), true},
                             ChordNote{pitch(Letter::kE), false}}),
      make_chord(quarter(), {ChordNote{pitch(Letter::kD), false},
                             ChordNote{pitch(Letter::kG), false}}),
  };
  EXPECT_FALSE(validate_ties(events).ok());
}

TEST(TieValidationTest, RestCannotBeTiedToButHasNoOwnTieFlag) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), quarter()),
      make_rest(quarter()),
  };
  EXPECT_TRUE(validate_ties(events).ok());
}
