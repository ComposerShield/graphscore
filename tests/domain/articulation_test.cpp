// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Articulation;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Duration;
using graphscore::event_articulations;
using graphscore::event_stem;
using graphscore::is_duration_articulation;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_note;
using graphscore::make_rest;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::SpelledPitch;
using graphscore::StemDirection;
using graphscore::VoiceEvent;

namespace {

SpelledPitch pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

}  // namespace

TEST(ArticulationTest, IsDurationArticulationIdentifiesTheThreeConflicting) {
  EXPECT_TRUE(is_duration_articulation(Articulation::kStaccato));
  EXPECT_TRUE(is_duration_articulation(Articulation::kStaccatissimo));
  EXPECT_TRUE(is_duration_articulation(Articulation::kTenuto));
  EXPECT_FALSE(is_duration_articulation(Articulation::kAccent));
  EXPECT_FALSE(is_duration_articulation(Articulation::kMarcato));
}

TEST(ArticulationTest, NoteAttachesSingleArticulation) {
  const Note note = make_note(pitch(Letter::kC), quarter(),
                              /*tied_to_next=*/false, {Articulation::kAccent});
  ASSERT_EQ(note.articulations.size(), 1u);
  EXPECT_EQ(note.articulations[0], Articulation::kAccent);
}

TEST(ArticulationTest, NoteAttachesMultipleNonConflictingArticulations) {
  const Note note =
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/false,
                {Articulation::kAccent, Articulation::kStaccato});
  ASSERT_EQ(note.articulations.size(), 2u);
  EXPECT_EQ(note.articulations[0], Articulation::kAccent);
  EXPECT_EQ(note.articulations[1], Articulation::kStaccato);
}

TEST(ArticulationTest, ChordAttachesArticulationsToTheWholeChord) {
  const Chord chord = make_chord(
      quarter(), {ChordNote{pitch(Letter::kC)}, ChordNote{pitch(Letter::kE)}},
      {Articulation::kMarcato});
  ASSERT_EQ(chord.articulations.size(), 1u);
  EXPECT_EQ(chord.articulations[0], Articulation::kMarcato);
}

TEST(ArticulationTest, EventArticulationsAccessorFindsNoteAndChordButNotRest) {
  const VoiceEvent note_event =
      make_note(pitch(Letter::kC), quarter(), false, {Articulation::kTenuto});
  const std::vector<Articulation>* note_articulations =
      event_articulations(note_event);
  ASSERT_NE(note_articulations, nullptr);
  EXPECT_EQ(*note_articulations,
            std::vector<Articulation>{Articulation::kTenuto});

  const VoiceEvent rest_event = make_rest(quarter());
  EXPECT_EQ(event_articulations(rest_event), nullptr);
}

TEST(StemOverrideTest, DefaultsToAuto) {
  const Note note = make_note(pitch(Letter::kC), quarter());
  EXPECT_EQ(note.stem, StemDirection::kAuto);
}

TEST(StemOverrideTest, RoundTripsUpAndDown) {
  const Note up_note =
      make_note(pitch(Letter::kC), quarter(), false, {}, StemDirection::kUp);
  EXPECT_EQ(up_note.stem, StemDirection::kUp);

  const Note down_note =
      make_note(pitch(Letter::kC), quarter(), false, {}, StemDirection::kDown);
  EXPECT_EQ(down_note.stem, StemDirection::kDown);

  const VoiceEvent event = up_note;
  EXPECT_EQ(event_stem(event), StemDirection::kUp);
}

TEST(StemOverrideTest, ChordStemRoundTrips) {
  const Chord chord = make_chord(
      quarter(), {ChordNote{pitch(Letter::kC)}, ChordNote{pitch(Letter::kE)}},
      {}, StemDirection::kDown);
  EXPECT_EQ(chord.stem, StemDirection::kDown);
}

TEST(StemOverrideTest, RestHasNoStemOverride) {
  const VoiceEvent rest_event = make_rest(quarter());
  EXPECT_EQ(event_stem(rest_event), StemDirection::kAuto);
}
