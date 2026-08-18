// SPDX-License-Identifier: Apache-2.0

#include "note_entry_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {
using note_entry_test::append_quarter_note;
using note_entry_test::append_whole_rest;
using note_entry_test::armed;
using note_entry_test::Fixture;
using note_entry_test::grace_note;
using note_entry_test::measure;

// ---- make_convert_event_to_rest_command (M5-phase-23) ----------------------

TEST(NoteEntryTest, ConvertNoteSelectionReplacesEventWithEqualDurationRest) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  const Selection    selection{*NoteheadSet::create({item})};
  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
  const Rest& converted = std::get<Rest>(fixture.voice().events().front());
  EXPECT_EQ(converted.id, original.id);
  EXPECT_EQ(converted.duration, original.duration);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()), original);
  ASSERT_TRUE(command->redo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
}

TEST(NoteEntryTest, ConvertChordNoteSelectionConvertsWholeContainingChord) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g4 = *SpelledPitch::create(Letter::kG, 4);
  append_whole_rest(fixture);
  const Chord original =
      make_chord(*Duration::create(NoteValue::kQuarter, 0),
                 {{NotationEntityId::generate(), c4, false},
                  {NotationEntityId::generate(), e4, false},
                  {NotationEntityId::generate(), g4, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  // Selecting only the middle pitch still converts the WHOLE chord -- this
  // deliberately differs from make_delete_notehead_command's per-pitch
  // semantics.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.notes[1].id};
  const Selection    selection{*NoteheadSet::create({item})};
  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events()[1]));
  const Rest& converted = std::get<Rest>(fixture.voice().events()[1]);
  EXPECT_EQ(converted.id, original.id);
  EXPECT_EQ(converted.duration, original.duration);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events()[1]), original);
  ASSERT_TRUE(command->redo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(fixture.voice().events()[1]));
}

TEST(NoteEntryTest, ConvertChordSetSelectionConvertsTheChord) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  const Chord        original =
      make_chord(*Duration::create(NoteValue::kQuarter, 0),
                 {{NotationEntityId::generate(), c4, false},
                  {NotationEntityId::generate(), e4, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  const ChordItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                       *Voice::create(1), original.id};
  const Selection selection{*ChordSet::create({item})};
  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
  EXPECT_EQ(std::get<Rest>(fixture.voice().events().front()).id, original.id);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events().front()), original);
}

TEST(NoteEntryTest, ConvertNoteSelectionPreservesDottedDuration) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Duration     dotted   = *Duration::create(NoteValue::kHalf, 1);
  const Note         original = make_note(c4, dotted);
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  const Selection    selection{*NoteheadSet::create({item})};
  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  const Rest* converted = std::get_if<Rest>(&fixture.voice().events().front());
  ASSERT_NE(converted, nullptr);
  EXPECT_EQ(converted->duration, dotted);
}

TEST(NoteEntryTest, ConvertNoteSelectionPreservesTupletDuration) {
  Fixture            fixture;
  const SpelledPitch c4      = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4      = *SpelledPitch::create(Letter::kD, 4);
  const SpelledPitch e4      = *SpelledPitch::create(Letter::kE, 4);
  const auto         ratio   = *TupletRatio::create(3, 2);
  const Duration     triplet = *Duration::create(NoteValue::kEighth, 0, ratio);
  const Note         first   = make_note(c4, triplet);
  const Note         second  = make_note(d4, triplet);
  const Note         third   = make_note(e4, triplet);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());
  ASSERT_TRUE(fixture.voice().append(third).ok());
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), second.id};
  const Selection    selection{*NoteheadSet::create({item})};
  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  const Rest* converted = std::get_if<Rest>(&fixture.voice().events()[1]);
  ASSERT_NE(converted, nullptr);
  EXPECT_EQ(converted->duration, triplet);
}

TEST(NoteEntryTest, ConvertNoteheadOnlyAffectsSelectedVoiceInMultiVoiceStaff) {
  Fixture            fixture;
  const SpelledPitch c4          = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4          = *SpelledPitch::create(Letter::kE, 4);
  const Note         voice1_note = append_quarter_note(fixture, c4, 1);
  const Note         voice2_note = append_quarter_note(fixture, e4, 2);
  fixture.normalize_voice(1);
  fixture.normalize_voice(2);

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), voice1_note.id};
  const Selection    selection{*NoteheadSet::create({item})};
  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(fixture.voice(1).events().front()));
  // Voice 2 is untouched.
  EXPECT_EQ(std::get<Note>(fixture.voice(2).events().front()), voice2_note);
}

TEST(NoteEntryTest, SelectionAfterConvertIsRestSetOnPreservedNoteId) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  const Selection    selection{*NoteheadSet::create({item})};
  const auto next = selection_after_convert_to_rest(fixture.project, selection);
  ASSERT_TRUE(next.has_value());
  const auto* rest_set = std::get_if<RestSet>(&*next);
  ASSERT_NE(rest_set, nullptr);
  ASSERT_EQ(rest_set->items().size(), 1u);
  EXPECT_EQ(rest_set->items().front().entity, original.id);
  EXPECT_EQ(rest_set->items().front().node, fixture.node_id);
  EXPECT_EQ(rest_set->items().front().track, fixture.track());
  EXPECT_EQ(rest_set->items().front().stave, fixture.stave_id());
  EXPECT_EQ(rest_set->items().front().voice, *Voice::create(1));

  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  EXPECT_EQ(std::get<Rest>(fixture.voice().events().front()).id, original.id);
}

TEST(NoteEntryTest, SelectionAfterConvertForChordNoteUsesTheChordId) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  append_whole_rest(fixture);
  const Chord original =
      make_chord(*Duration::create(NoteValue::kQuarter, 0),
                 {{NotationEntityId::generate(), c4, false},
                  {NotationEntityId::generate(), e4, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  // Selecting one pitch (a ChordNote) of the chord: the preserved id after
  // conversion is the CHORD's own id, not the clicked ChordNote's.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.notes[0].id};
  const Selection    selection{*NoteheadSet::create({item})};
  const auto next = selection_after_convert_to_rest(fixture.project, selection);
  ASSERT_TRUE(next.has_value());
  const auto* rest_set = std::get_if<RestSet>(&*next);
  ASSERT_NE(rest_set, nullptr);
  ASSERT_EQ(rest_set->items().size(), 1u);
  EXPECT_EQ(rest_set->items().front().entity, original.id);

  auto command = make_convert_event_to_rest_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  EXPECT_EQ(std::get<Rest>(fixture.voice().events()[1]).id, original.id);
}

TEST(NoteEntryTest, ConvertAlreadyRestSelectionReturnsNull) {
  Fixture            fixture;
  const Rest         original = append_whole_rest(fixture);
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  const Selection    selection{*NoteheadSet::create({item})};
  EXPECT_EQ(make_convert_event_to_rest_command(fixture.project, selection),
            nullptr);
  EXPECT_EQ(selection_after_convert_to_rest(fixture.project, selection),
            std::nullopt);
}

TEST(NoteEntryTest, ConvertGraceNoteSelectionReturnsNull) {
  Fixture            fixture;
  const SpelledPitch c4        = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4        = *SpelledPitch::create(Letter::kD, 4);
  const Note         principal = append_quarter_note(fixture, c4);
  append_quarter_note(fixture, d4);
  fixture.normalize_voice();
  const GraceGroup group = make_grace_group(principal.id, {grace_note(c4)});
  ASSERT_TRUE(fixture.voice().add_grace_group(group).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), group.notes[0].id};
  const Selection    selection{*NoteheadSet::create({item})};
  EXPECT_EQ(make_convert_event_to_rest_command(fixture.project, selection),
            nullptr);
  EXPECT_EQ(selection_after_convert_to_rest(fixture.project, selection),
            std::nullopt);
}

TEST(NoteEntryTest, ConvertMultiItemNoteheadSetReturnsNull) {
  Fixture            fixture;
  const SpelledPitch c4     = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4     = *SpelledPitch::create(Letter::kD, 4);
  const Note         first  = append_quarter_note(fixture, c4);
  const Note         second = append_quarter_note(fixture, d4);
  fixture.normalize_voice();

  const Selection selection{*NoteheadSet::create(
      {NoteheadItem{fixture.node_id, fixture.track(), fixture.stave_id(),
                    *Voice::create(1), first.id},
       NoteheadItem{fixture.node_id, fixture.track(), fixture.stave_id(),
                    *Voice::create(1), second.id}})};
  EXPECT_EQ(make_convert_event_to_rest_command(fixture.project, selection),
            nullptr);
  EXPECT_EQ(selection_after_convert_to_rest(fixture.project, selection),
            std::nullopt);
}

TEST(NoteEntryTest, ConvertMultiItemChordSetReturnsNull) {
  Fixture            fixture;
  const SpelledPitch c4      = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4      = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g4      = *SpelledPitch::create(Letter::kG, 4);
  const SpelledPitch b4      = *SpelledPitch::create(Letter::kB, 4);
  const Duration     quarter = *Duration::create(NoteValue::kQuarter, 0);
  const Chord        first =
      make_chord(quarter, {{NotationEntityId::generate(), c4, false},
                           {NotationEntityId::generate(), e4, false}});
  const Chord second =
      make_chord(quarter, {{NotationEntityId::generate(), g4, false},
                           {NotationEntityId::generate(), b4, false}});
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());
  fixture.normalize_voice();

  const Selection selection{*ChordSet::create(
      {ChordItem{fixture.node_id, fixture.track(), fixture.stave_id(),
                 *Voice::create(1), first.id},
       ChordItem{fixture.node_id, fixture.track(), fixture.stave_id(),
                 *Voice::create(1), second.id}})};
  EXPECT_EQ(make_convert_event_to_rest_command(fixture.project, selection),
            nullptr);
  EXPECT_EQ(selection_after_convert_to_rest(fixture.project, selection),
            std::nullopt);
}

TEST(NoteEntryTest, ConvertStaleNoteheadSelectionReturnsNull) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), NotationEntityId::generate()};
  const Selection    selection{*NoteheadSet::create({item})};
  EXPECT_EQ(make_convert_event_to_rest_command(fixture.project, selection),
            nullptr);
  EXPECT_EQ(selection_after_convert_to_rest(fixture.project, selection),
            std::nullopt);
}

TEST(NoteEntryTest, ConvertNonNoteheadOrChordSelectionArmsReturnNull) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();
  ASSERT_TRUE(fixture.voice()
                  .add_dynamic(make_dynamic_marking(original.id, Dynamic::kMf))
                  .ok());

  const std::vector<Selection> non_applicable = {
      Selection{*RestSet::create(
          {RestItem{fixture.node_id, fixture.track(), fixture.stave_id(),
                    *Voice::create(1), NotationEntityId::generate()}})},
      Selection{*MarkingSet::create(
          {MarkingItem{fixture.node_id, fixture.track(), fixture.stave_id(),
                       *Voice::create(1), MarkingKind::kDynamic, original.id,
                       std::nullopt}})},
      Selection{*FullMeasureSet::create({FullMeasureItem{
          fixture.node_id, fixture.track(), fixture.stave_id(), 0}})},
      Selection{*ArbitraryRangeSet::create({ArbitraryRangeItem{
          fixture.node_id, fixture.track(), fixture.stave_id(),
          *Voice::create(1),
          MusicalSpan{Rational(0), *Rational::create(1, 4)}}})},
      Selection{*InsertionCaretSet::create({InsertionCaretItem{
          fixture.node_id, fixture.track(), fixture.stave_id(),
          *Voice::create(1), Rational(0)}})},
      Selection{*NodeSet::create({NodeItem{fixture.node_id}})},
      Selection{*ConnectorSet::create(
          {ConnectorItem{fixture.node_id, ConnectorId::generate()}})},
  };

  for (const Selection& selection : non_applicable) {
    EXPECT_EQ(make_convert_event_to_rest_command(fixture.project, selection),
              nullptr);
    EXPECT_EQ(selection_after_convert_to_rest(fixture.project, selection),
              std::nullopt);
  }
}

}  // namespace
