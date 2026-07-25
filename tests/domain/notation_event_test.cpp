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
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteValue;
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
  const Chord chord = make_chord(
      quarter(), {ChordNote{.pitch = pitch(Letter::kC), .tied_to_next = false},
                  ChordNote{.pitch = pitch(Letter::kE), .tied_to_next = true}});
  ASSERT_EQ(chord.notes.size(), 2u);
  EXPECT_FALSE(chord.notes[0].tied_to_next);
  EXPECT_TRUE(chord.notes[1].tied_to_next);
}

TEST(NotationEventTest, EventSoundsPitchMatchesNoteAndChordNotehead) {
  const VoiceEvent note_event = make_note(pitch(Letter::kC), quarter());
  EXPECT_TRUE(event_sounds_pitch(note_event, pitch(Letter::kC)));
  EXPECT_FALSE(event_sounds_pitch(note_event, pitch(Letter::kD)));

  const VoiceEvent chord_event =
      make_chord(quarter(), {ChordNote{.pitch = pitch(Letter::kC)},
                             ChordNote{.pitch = pitch(Letter::kG)}});
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
      make_chord(
          quarter(),
          {ChordNote{.pitch = pitch(Letter::kC), .tied_to_next = true},
           ChordNote{.pitch = pitch(Letter::kE), .tied_to_next = false}}),
      make_chord(
          quarter(),
          {ChordNote{.pitch = pitch(Letter::kC), .tied_to_next = false},
           ChordNote{.pitch = pitch(Letter::kG), .tied_to_next = false}}),
  };
  EXPECT_TRUE(validate_ties(events).ok());
}

TEST(TieValidationTest, ChordNoteheadTieToMissingPitchIsFlagged) {
  const std::vector<VoiceEvent> events = {
      make_chord(
          quarter(),
          {ChordNote{.pitch = pitch(Letter::kC), .tied_to_next = true},
           ChordNote{.pitch = pitch(Letter::kE), .tied_to_next = false}}),
      make_chord(
          quarter(),
          {ChordNote{.pitch = pitch(Letter::kD), .tied_to_next = false},
           ChordNote{.pitch = pitch(Letter::kG), .tied_to_next = false}}),
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

// -- Phase 8f-i: ChordNote identity --

TEST(ChordNoteIdentityTest, EachNoteheadReceivesDistinctFreshId) {
  const Chord chord =
      make_chord(quarter(), {ChordNote{.pitch = pitch(Letter::kC)},
                             ChordNote{.pitch = pitch(Letter::kE)},
                             ChordNote{.pitch = pitch(Letter::kG)}});
  ASSERT_EQ(chord.notes.size(), 3u);
  EXPECT_NE(chord.notes[0].id, NotationEntityId{});
  EXPECT_NE(chord.notes[1].id, NotationEntityId{});
  EXPECT_NE(chord.notes[2].id, NotationEntityId{});
  EXPECT_NE(chord.notes[0].id, chord.notes[1].id);
  EXPECT_NE(chord.notes[1].id, chord.notes[2].id);
  EXPECT_NE(chord.notes[0].id, chord.notes[2].id);
}

TEST(ChordNoteIdentityTest, SeparateChordsDoNotReuseIds) {
  const Chord c1 =
      make_chord(quarter(), {ChordNote{.pitch = pitch(Letter::kC)},
                             ChordNote{.pitch = pitch(Letter::kE)}});
  const Chord c2 =
      make_chord(quarter(), {ChordNote{.pitch = pitch(Letter::kG)},
                             ChordNote{.pitch = pitch(Letter::kB)}});
  // Every notehead across both chords must have a distinct, non-nil id.
  for (std::size_t i = 0; i < c1.notes.size(); ++i) {
    EXPECT_NE(c1.notes[i].id, NotationEntityId{});
    for (std::size_t j = 0; j < c2.notes.size(); ++j) {
      EXPECT_NE(c1.notes[i].id, c2.notes[j].id);
    }
  }
}

TEST(ChordNoteIdentityTest, IdsSurviveCopy) {
  const Chord original = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch(Letter::kC), .tied_to_next = true},
       ChordNote{.pitch = pitch(Letter::kE), .tied_to_next = false}});
  const Chord copy = original;
  ASSERT_EQ(copy.notes.size(), 2u);
  EXPECT_EQ(copy.notes[0].id, original.notes[0].id);
  EXPECT_EQ(copy.notes[1].id, original.notes[1].id);
  EXPECT_EQ(copy.notes[0].tied_to_next, true);
}

TEST(ChordNoteIdentityTest, ExplicitIdPreservedWhenNonNil) {
  const NotationEntityId explicit_id = NotationEntityId::generate();
  const ChordNote        cn{explicit_id, pitch(Letter::kC), false};
  Chord chord = Chord{NotationEntityId::generate(), quarter(), {cn}, {}, {}};
  // Constructing a chord with explicit ChordNote ids via aggregate init
  // preserves the id because it is non-nil and the factory is bypassed.
  ASSERT_EQ(chord.notes.size(), 1u);
  EXPECT_EQ(chord.notes[0].id, explicit_id);
}

TEST(ChordNoteIdentityTest, FactoryPreservesNonNilEmbeddedId) {
  const NotationEntityId explicit_id = NotationEntityId::generate();
  const Chord            chord =
      make_chord(quarter(), {ChordNote{explicit_id, pitch(Letter::kC), false},
                             ChordNote{{}, pitch(Letter::kE), false}});
  ASSERT_EQ(chord.notes.size(), 2u);
  // Non-nil id preserved.
  EXPECT_EQ(chord.notes[0].id, explicit_id);
  // Nil id replaced with fresh id.
  EXPECT_NE(chord.notes[1].id, NotationEntityId{});
  EXPECT_NE(chord.notes[1].id, explicit_id);
}

TEST(ChordNoteIdentityTest, StructuralEqualityIncludesAllFields) {
  const NotationEntityId shared_id = NotationEntityId::generate();
  const ChordNote        a{shared_id, pitch(Letter::kC), false};
  const ChordNote        b{shared_id, pitch(Letter::kC), false};
  const ChordNote        c{shared_id, pitch(Letter::kD), false};
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}
