// SPDX-License-Identifier: Apache-2.0

#include "selection/selection_test_support.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

namespace {

constexpr Voice kVoice = *Voice::create(1);

VoiceContent& voice(Fixture& fixture) {
  return fixture.project.find_node(fixture.node_id)
      ->lane(fixture.track_ids.front())
      ->stave(fixture.stave_id())
      ->voice(kVoice);
}

Selection note_selection(Fixture& fixture, NotationEntityId id) {
  return Selection{*NoteheadSet::create(
      {NoteheadItem{fixture.node_id, fixture.track_ids.front(),
                    fixture.stave_id(), kVoice, id}})};
}

Selection chord_selection(Fixture& fixture, NotationEntityId id) {
  return Selection{
      *ChordSet::create({ChordItem{fixture.node_id, fixture.track_ids.front(),
                                   fixture.stave_id(), kVoice, id}})};
}

Selection articulation_selection(Fixture& fixture, NotationEntityId id,
                                 Articulation articulation) {
  return Selection{*MarkingSet::create({MarkingItem{
      fixture.node_id, fixture.track_ids.front(), fixture.stave_id(), kVoice,
      MarkingKind::kArticulation, id, articulation}})};
}

NotationEntityId append_note(Fixture&                  fixture,
                             std::vector<Articulation> articulations = {}) {
  Note       note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0), false,
                              std::move(articulations));
  const auto id   = note.id;
  EXPECT_TRUE(voice(fixture).append(std::move(note)).ok());
  EXPECT_TRUE(voice(fixture)
                  .normalize(fixture.project.find_node(fixture.node_id)
                                 ->timeline()
                                 ->node_end())
                  .ok());
  return id;
}

NotationEntityId append_chord(Fixture&                  fixture,
                              std::vector<Articulation> articulations = {}) {
  const auto c        = *SpelledPitch::create(Letter::kC, 4);
  const auto e        = *SpelledPitch::create(Letter::kE, 4);
  Chord      chord    = make_chord(*Duration::create(NoteValue::kQuarter, 0),
                                   {{NotationEntityId::generate(), c, false},
                                    {NotationEntityId::generate(), e, false}});
  chord.articulations = std::move(articulations);
  const auto id       = chord.id;
  EXPECT_TRUE(voice(fixture).append(std::move(chord)).ok());
  EXPECT_TRUE(voice(fixture)
                  .normalize(fixture.project.find_node(fixture.node_id)
                                 ->timeline()
                                 ->node_end())
                  .ok());
  return id;
}

const graphscore::VoiceEvent& first_event(const Fixture& fixture) {
  return fixture.project.find_node(fixture.node_id)
      ->lane(fixture.track_ids.front())
      ->stave(fixture.stave_id())
      ->voice(kVoice)
      .events()
      .front();
}

}  // namespace

TEST(EventStyleEditTest, AppliesEveryArticulationToNotesAndChords) {
  for (const Articulation articulation : graphscore::kAllArticulations) {
    Fixture    note_fixture(1);
    const auto note_id     = append_note(note_fixture);
    auto       note_result = graphscore::make_articulation_edit_command(
        note_fixture.project, note_selection(note_fixture, note_id),
        graphscore::ArticulationEdit::kApply, articulation);
    ASSERT_TRUE(note_result.available()) << note_result.unavailable_reason;
    EXPECT_TRUE(note_result.command->execute(note_fixture.project).ok());

    Fixture    chord_fixture(1);
    const auto chord_id     = append_chord(chord_fixture);
    auto       chord_result = graphscore::make_articulation_edit_command(
        chord_fixture.project, chord_selection(chord_fixture, chord_id),
        graphscore::ArticulationEdit::kApply, articulation);
    ASSERT_TRUE(chord_result.available()) << chord_result.unavailable_reason;
    EXPECT_TRUE(chord_result.command->execute(chord_fixture.project).ok());
  }
}

TEST(EventStyleEditTest, ChangeRemoveDuplicateAndConflictHavePreciseReasons) {
  Fixture         fixture(1);
  const auto      id        = append_note(fixture, {Articulation::kStaccato});
  const Selection event     = note_selection(fixture, id);
  auto            duplicate = graphscore::make_articulation_edit_command(
      fixture.project, event, graphscore::ArticulationEdit::kApply,
      Articulation::kStaccato);
  EXPECT_FALSE(duplicate.available());
  EXPECT_EQ(duplicate.unavailable_reason, "articulation is already present");
  auto conflict = graphscore::make_articulation_edit_command(
      fixture.project, event, graphscore::ArticulationEdit::kApply,
      Articulation::kTenuto);
  EXPECT_FALSE(conflict.available());
  EXPECT_EQ(conflict.unavailable_reason,
            "conflicts with the existing duration articulation");

  const Selection marking =
      articulation_selection(fixture, id, Articulation::kStaccato);
  auto change = graphscore::make_articulation_edit_command(
      fixture.project, marking, graphscore::ArticulationEdit::kChange,
      Articulation::kTenuto);
  ASSERT_TRUE(change.available()) << change.unavailable_reason;
  ASSERT_TRUE(change.command->execute(fixture.project).ok());
  const Selection changed =
      articulation_selection(fixture, id, Articulation::kTenuto);
  auto remove = graphscore::make_articulation_edit_command(
      fixture.project, changed, graphscore::ArticulationEdit::kRemove,
      Articulation::kAccent);
  ASSERT_TRUE(remove.available()) << remove.unavailable_reason;
  EXPECT_TRUE(remove.command->execute(fixture.project).ok());
}

TEST(EventStyleEditTest, ChangePreservesArticulationOrderForNotesAndChords) {
  const std::vector initial = {Articulation::kAccent, Articulation::kStaccato,
                               Articulation::kMarcato};
  for (const bool chord : {false, true}) {
    Fixture    fixture(1);
    const auto id =
        chord ? append_chord(fixture, initial) : append_note(fixture, initial);
    auto change = graphscore::make_articulation_edit_command(
        fixture.project,
        articulation_selection(fixture, id, Articulation::kStaccato),
        graphscore::ArticulationEdit::kChange, Articulation::kTenuto);
    ASSERT_TRUE(change.available()) << change.unavailable_reason;
    ASSERT_TRUE(change.command->execute(fixture.project).ok());
    EXPECT_EQ(*graphscore::event_articulations(first_event(fixture)),
              (std::vector{Articulation::kAccent, Articulation::kTenuto,
                           Articulation::kMarcato}));
  }
}

TEST(EventStyleEditTest,
     GraceNoteRejectsEveryEventStyleFactoryWithoutMutation) {
  Fixture               fixture(1);
  const auto            principal = append_note(fixture);
  graphscore::GraceNote grace{NotationEntityId::generate(),
                              *SpelledPitch::create(Letter::kD, 4),
                              *Duration::create(NoteValue::kEighth, 0)};
  const auto            grace_id = grace.id;
  ASSERT_TRUE(voice(fixture)
                  .add_grace_group(make_grace_group(principal, {grace}))
                  .ok());
  const VoiceContent before   = voice(fixture);
  const Selection    selected = note_selection(fixture, grace_id);
  for (const auto edit : {graphscore::ArticulationEdit::kApply,
                          graphscore::ArticulationEdit::kChange,
                          graphscore::ArticulationEdit::kRemove}) {
    auto result = graphscore::make_articulation_edit_command(
        fixture.project, selected, edit, Articulation::kAccent);
    EXPECT_FALSE(result.available());
    EXPECT_EQ(result.unavailable_reason,
              "grace notes do not support event style editing");
    EXPECT_EQ(voice(fixture).events(), before.events());
  }
  for (const auto stem :
       {graphscore::StemDirection::kAuto, graphscore::StemDirection::kUp,
        graphscore::StemDirection::kDown}) {
    auto result =
        graphscore::make_stem_edit_command(fixture.project, selected, stem);
    EXPECT_FALSE(result.available());
    EXPECT_EQ(result.unavailable_reason,
              "grace notes do not support event style editing");
    EXPECT_EQ(voice(fixture).events(), before.events());
  }

  const Selection stale = note_selection(fixture, NotationEntityId::generate());
  auto            stale_result = graphscore::make_articulation_edit_command(
      fixture.project, stale, graphscore::ArticulationEdit::kApply,
      Articulation::kAccent);
  EXPECT_FALSE(stale_result.available());
  EXPECT_EQ(voice(fixture).events(), before.events());

  Fixture            malformed_fixture(1);
  const auto         chord_id  = append_chord(malformed_fixture);
  const auto         malformed = note_selection(malformed_fixture, chord_id);
  const VoiceContent malformed_before = voice(malformed_fixture);
  auto malformed_articulation = graphscore::make_articulation_edit_command(
      malformed_fixture.project, malformed,
      graphscore::ArticulationEdit::kApply, Articulation::kAccent);
  auto malformed_stem = graphscore::make_stem_edit_command(
      malformed_fixture.project, malformed, graphscore::StemDirection::kUp);
  EXPECT_FALSE(malformed_articulation.available());
  EXPECT_FALSE(malformed_stem.available());
  EXPECT_EQ(voice(malformed_fixture).events(), malformed_before.events());
}

TEST(EventStyleEditTest, StemFactoriesSetAllDirectionsAndRejectInvalidTargets) {
  Fixture         fixture(1);
  const auto      id       = append_note(fixture);
  const Selection selected = note_selection(fixture, id);
  auto up = graphscore::make_stem_edit_command(fixture.project, selected,
                                               graphscore::StemDirection::kUp);
  ASSERT_TRUE(up.available());
  ASSERT_TRUE(up.command->execute(fixture.project).ok());
  auto automatic = graphscore::make_stem_edit_command(
      fixture.project, selected, graphscore::StemDirection::kAuto);
  ASSERT_TRUE(automatic.available());
  ASSERT_TRUE(automatic.command->execute(fixture.project).ok());
  auto down = graphscore::make_stem_edit_command(
      fixture.project, selected, graphscore::StemDirection::kDown);
  ASSERT_TRUE(down.available());

  const Selection stale = note_selection(fixture, NotationEntityId::generate());
  auto            stale_result = graphscore::make_stem_edit_command(
      fixture.project, stale, graphscore::StemDirection::kUp);
  EXPECT_FALSE(stale_result.available());
  EXPECT_EQ(stale_result.unavailable_reason,
            "requires one live note or chord event");
  auto wrong = graphscore::make_stem_edit_command(
      fixture.project,
      articulation_selection(fixture, id, Articulation::kAccent),
      graphscore::StemDirection::kUp);
  EXPECT_FALSE(wrong.available());
}
