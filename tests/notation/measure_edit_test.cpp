// SPDX-License-Identifier: Apache-2.0

#include "selection/selection_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

using graphscore::Command;
using graphscore::CommandHistory;
using graphscore::event_duration;
using graphscore::make_delete_measure_command;
using graphscore::make_insert_measure_command;
using graphscore::MeasureInsertMode;
using graphscore::selection_after_delete_measure;
using graphscore::selection_after_insert_measure;
using graphscore::VoiceEvent;

namespace {

Duration whole_duration() {
  return *Duration::create(NoteValue::kWhole, 0);
}

// Fills `voice` with one whole note per letter in `markers`, at octave 4.
// A default 4/4 measure() is exactly one whole note long, so this places
// one distinguishable marker note per measure.
void fill_measures_with_markers(VoiceContent&       voice,
                                std::vector<Letter> markers) {
  for (const Letter letter : markers) {
    const SpelledPitch pitch = *SpelledPitch::create(letter, 4);
    ASSERT_TRUE(voice.append(make_note(pitch, whole_duration())).ok());
  }
}

// Scans `voice` for the onset of the first top-level Note at `pitch`, or
// nullopt when none is found.
std::optional<Rational> onset_of_pitch(const VoiceContent& voice,
                                       SpelledPitch        pitch) {
  Rational onset(0);
  for (const VoiceEvent& event : voice.events()) {
    if (const auto* note = std::get_if<Note>(&event);
        note != nullptr && note->pitch == pitch) {
      return onset;
    }
    onset = onset + event_duration(event).resolved();
  }
  return std::nullopt;
}

Selection single_full_measure(NodeId node_id, TrackId track_id,
                              StaveId stave_id, std::size_t measure_index) {
  const auto set = FullMeasureSet::create(
      {FullMeasureItem{node_id, track_id, stave_id, measure_index}});
  return Selection{*set};
}

// Asserts every one of the four API functions rejects `selection` against
// `project`: both insert modes' command and selection-after query, and the
// delete command and its selection-after query.  Used for rejection cases
// that are equally invalid for insert and delete (arm mismatch,
// misalignment, an invalid track/stave/timeline/measure_index) --
// everything except the sole-measure delete guard, which is delete-only.
void expect_shared_rejection(const Project&   project,
                             const Selection& selection) {
  EXPECT_EQ(make_insert_measure_command(project, selection,
                                        MeasureInsertMode::kBefore),
            nullptr);
  EXPECT_EQ(make_insert_measure_command(project, selection,
                                        MeasureInsertMode::kAppend),
            nullptr);
  EXPECT_FALSE(selection_after_insert_measure(project, selection,
                                              MeasureInsertMode::kBefore)
                   .has_value());
  EXPECT_FALSE(selection_after_insert_measure(project, selection,
                                              MeasureInsertMode::kAppend)
                   .has_value());
  EXPECT_EQ(make_delete_measure_command(project, selection), nullptr);
  EXPECT_FALSE(selection_after_delete_measure(project, selection).has_value());
}

// ---- execute through CommandHistory: measure count and surrounding music
// ----

TEST(MeasureEditTest, InsertBeforeFirstMeasureShiftsAllMarkersRight) {
  Fixture fixture(3);
  fill_measures_with_markers(fixture.voice(),
                             {Letter::kC, Letter::kD, Letter::kE});
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 0);

  const std::optional<Selection> predicted = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_TRUE(predicted.has_value());

  const Node               before = *fixture.project.find_node(fixture.node_id);
  std::unique_ptr<Command> command = make_insert_measure_command(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            4u);
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4)),
      Rational(1));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4)),
      Rational(2));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kE, 4)),
      Rational(3));

  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 1u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  const Node inserted = *fixture.project.find_node(fixture.node_id);
  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), before);
  ASSERT_TRUE(history.redo(fixture.project).ok());
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), inserted);
}

TEST(MeasureEditTest, InsertBeforeMiddleMeasureShiftsOnlyLaterMarkers) {
  Fixture fixture(3);
  fill_measures_with_markers(fixture.voice(),
                             {Letter::kC, Letter::kD, Letter::kE});
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 1);

  const std::optional<Selection> predicted = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_TRUE(predicted.has_value());

  std::unique_ptr<Command> command = make_insert_measure_command(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            4u);
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4)),
      Rational(0));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4)),
      Rational(2));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kE, 4)),
      Rational(3));

  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 2u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
}

TEST(MeasureEditTest, InsertBeforeLastMeasureShiftsOnlyThatMarker) {
  Fixture fixture(3);
  fill_measures_with_markers(fixture.voice(),
                             {Letter::kC, Letter::kD, Letter::kE});
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 2);

  const std::optional<Selection> predicted = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_TRUE(predicted.has_value());

  std::unique_ptr<Command> command = make_insert_measure_command(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            4u);
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4)),
      Rational(0));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4)),
      Rational(1));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kE, 4)),
      Rational(3));

  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 3u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
}

TEST(MeasureEditTest, AppendAddsFinalMeasureWithoutMovingExistingMarkers) {
  Fixture fixture(3);
  fill_measures_with_markers(fixture.voice(),
                             {Letter::kC, Letter::kD, Letter::kE});
  // The source selection names measure 1 (the middle measure) -- append
  // targets node end regardless.
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 1);

  const std::optional<Selection> predicted = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kAppend);
  ASSERT_TRUE(predicted.has_value());

  std::unique_ptr<Command> command = make_insert_measure_command(
      fixture.project, selection, MeasureInsertMode::kAppend);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            4u);
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4)),
      Rational(0));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4)),
      Rational(1));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kE, 4)),
      Rational(2));

  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 3u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
}

TEST(MeasureEditTest, DeleteFirstMeasureRemovesItsMarkerAndShiftsRestLeft) {
  Fixture fixture(3);
  fill_measures_with_markers(fixture.voice(),
                             {Letter::kC, Letter::kD, Letter::kE});
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 0);

  const std::optional<Selection> predicted =
      selection_after_delete_measure(fixture.project, selection);
  ASSERT_TRUE(predicted.has_value());

  const Node               before = *fixture.project.find_node(fixture.node_id);
  std::unique_ptr<Command> command =
      make_delete_measure_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            2u);
  EXPECT_FALSE(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4))
          .has_value());
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4)),
      Rational(0));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kE, 4)),
      Rational(1));

  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 0u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), before);
}

TEST(MeasureEditTest, DeleteMiddleMeasureRemovesItsMarkerOnly) {
  Fixture fixture(3);
  fill_measures_with_markers(fixture.voice(),
                             {Letter::kC, Letter::kD, Letter::kE});
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 1);

  const std::optional<Selection> predicted =
      selection_after_delete_measure(fixture.project, selection);
  ASSERT_TRUE(predicted.has_value());

  std::unique_ptr<Command> command =
      make_delete_measure_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            2u);
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4)),
      Rational(0));
  EXPECT_FALSE(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4))
          .has_value());
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kE, 4)),
      Rational(1));

  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 1u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
}

TEST(MeasureEditTest, DeleteLastMeasureClampsSelectionToNewLastIndex) {
  Fixture fixture(3);
  fill_measures_with_markers(fixture.voice(),
                             {Letter::kC, Letter::kD, Letter::kE});
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 2);

  const std::optional<Selection> predicted =
      selection_after_delete_measure(fixture.project, selection);
  ASSERT_TRUE(predicted.has_value());

  std::unique_ptr<Command> command =
      make_delete_measure_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            2u);
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4)),
      Rational(0));
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4)),
      Rational(1));
  EXPECT_FALSE(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kE, 4))
          .has_value());

  // The deleted measure_index (2) is no longer the last index (now 1), so
  // the clamp fires: new_count - 1 == 1.
  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 1u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
}

// ---- multi-track aligned FullMeasureSet: item scopes and score order ----

TEST(MeasureEditTest, InsertBeforeMultiTrackSelectionPreservesScopesAndOrder) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        3);
  const TrackId track0 = fixture.track_ids[0];
  const TrackId track1 = fixture.track_ids[1];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId stave1 = fixture.stave_id(1);

  const auto set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track1, stave1, 1},
       FullMeasureItem{fixture.node_id, track0, stave0, 1}});
  ASSERT_TRUE(set.has_value());
  const Selection selection{*set};

  const std::optional<Selection> predicted = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_TRUE(predicted.has_value());
  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  ASSERT_EQ(predicted_set->items().size(), 2u);
  // Score order: track0 before track1, regardless of caller input order.
  EXPECT_EQ(predicted_set->items()[0].track, track0);
  EXPECT_EQ(predicted_set->items()[0].stave, stave0);
  EXPECT_EQ(predicted_set->items()[1].track, track1);
  EXPECT_EQ(predicted_set->items()[1].stave, stave1);
  for (const FullMeasureItem& item : predicted_set->items()) {
    EXPECT_EQ(item.measure_index, 2u);
  }

  std::unique_ptr<Command> command = make_insert_measure_command(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());
}

TEST(MeasureEditTest, DeleteMultiTrackSelectionPreservesScopesAndOrder) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        3);
  const TrackId track0 = fixture.track_ids[0];
  const TrackId track1 = fixture.track_ids[1];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId stave1 = fixture.stave_id(1);

  const auto set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track1, stave1, 2},
       FullMeasureItem{fixture.node_id, track0, stave0, 2}});
  ASSERT_TRUE(set.has_value());
  const Selection selection{*set};

  const std::optional<Selection> predicted =
      selection_after_delete_measure(fixture.project, selection);
  ASSERT_TRUE(predicted.has_value());
  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  ASSERT_EQ(predicted_set->items().size(), 2u);
  EXPECT_EQ(predicted_set->items()[0].track, track0);
  EXPECT_EQ(predicted_set->items()[0].stave, stave0);
  EXPECT_EQ(predicted_set->items()[1].track, track1);
  EXPECT_EQ(predicted_set->items()[1].stave, stave1);
  for (const FullMeasureItem& item : predicted_set->items()) {
    EXPECT_EQ(item.measure_index, 1u);  // clamped: was the last measure
  }

  std::unique_ptr<Command> command =
      make_delete_measure_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());
}

// ---- selection_after_* never mutates the project ----

TEST(MeasureEditTest, SelectionAfterInsertDoesNotMutateProject) {
  Fixture         fixture(3);
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 1);

  const Node before = *fixture.project.find_node(fixture.node_id);
  const std::optional<Selection> result = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), before);
}

TEST(MeasureEditTest, SelectionAfterDeleteDoesNotMutateProject) {
  Fixture         fixture(3);
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 1);

  const Node before = *fixture.project.find_node(fixture.node_id);
  const std::optional<Selection> result =
      selection_after_delete_measure(fixture.project, selection);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), before);
}

// ---- paired-rejection agreement ----

TEST(MeasureEditTest, NoteheadSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = NoteheadSet::create(
      {NoteheadItem{fixture.node_id, fixture.track_ids[0], fixture.stave_id(),
                    *Voice::create(1), NotationEntityId::generate()}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, ChordSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = ChordSet::create(
      {ChordItem{fixture.node_id, fixture.track_ids[0], fixture.stave_id(),
                 *Voice::create(1), NotationEntityId::generate()}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, RestSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = RestSet::create(
      {RestItem{fixture.node_id, fixture.track_ids[0], fixture.stave_id(),
                *Voice::create(1), NotationEntityId::generate()}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, MarkingSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = MarkingSet::create(
      {MarkingItem{fixture.node_id, fixture.track_ids[0], fixture.stave_id(),
                   *Voice::create(1), MarkingKind::kDynamic,
                   NotationEntityId::generate(), std::nullopt}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, ArbitraryRangeSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = ArbitraryRangeSet::create({ArbitraryRangeItem{
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(),
      *Voice::create(1), MusicalSpan{Rational(0), Rational(1)}}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, InsertionCaretSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = InsertionCaretSet::create(
      {InsertionCaretItem{fixture.node_id, fixture.track_ids[0],
                          fixture.stave_id(), *Voice::create(1), Rational(0)}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, NodeSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = NodeSet::create({NodeItem{fixture.node_id}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, ConnectorSetArmIsRejected) {
  Fixture    fixture(1);
  const auto set = ConnectorSet::create(
      {ConnectorItem{fixture.node_id, ConnectorId::generate()}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, MisalignedDifferentNodeIsRejected) {
  Fixture               fixture(1);
  const NodeId          other_node = fixture.project.add_node("Other");
  const FullMeasureItem item_a{fixture.node_id, fixture.track_ids[0],
                               fixture.stave_id(), 0};
  const FullMeasureItem item_b{other_node, fixture.track_ids[0],
                               fixture.stave_id(), 0};
  const auto            set = FullMeasureSet::create({item_a, item_b});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, MisalignedDifferentMeasureIndexIsRejected) {
  Fixture    fixture(2);
  const auto set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, fixture.track_ids[0],
                       fixture.stave_id(), 0},
       FullMeasureItem{fixture.node_id, fixture.track_ids[0],
                       fixture.stave_id(), 1}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, UnknownTrackIsRejected) {
  Fixture       fixture(1);
  const TrackId unknown = TrackId::generate();
  const auto    set     = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, unknown, fixture.stave_id(), 0}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, ArchivedTrackIsRejected) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const TrackId archived       = fixture.track_ids[1];
  const StaveId archived_stave = fixture.stave_id(1);
  ASSERT_TRUE(fixture.project.archive_track(archived).ok());

  const auto set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, archived, archived_stave, 0}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, AbsentStaveIsRejected) {
  Fixture fixture({StaffLayout::single_staff(Clef::kTreble),
                   StaffLayout::single_staff(Clef::kBass)},
                  1);
  // stave1 belongs to track 1's layout, not track 0's.
  const auto set = FullMeasureSet::create({FullMeasureItem{
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(1), 0}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, OutOfRangeMeasureIndexIsRejected) {
  Fixture    fixture(1);
  const auto set = FullMeasureSet::create({FullMeasureItem{
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 99}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(fixture.project, Selection{*set});
}

TEST(MeasureEditTest, NodeWithoutTimelineIsRejected) {
  Project    project{ProjectId::generate(), "NoTimeline"};
  const auto track_id =
      project.add_track("Track", StaffLayout::single_staff(Clef::kTreble),
                        *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  const StaveId stave_id =
      project.find_active_track(*track_id)->layout().staves()[0].id;
  const NodeId node_id = project.add_node("Bare");
  Node*        node    = project.find_node(node_id);
  node->ensure_lane(*track_id);
  node->lane(*track_id)->ensure_stave(stave_id);
  ASSERT_FALSE(node->has_timeline());

  const auto set = FullMeasureSet::create(
      {FullMeasureItem{node_id, *track_id, stave_id, 0}});
  ASSERT_TRUE(set.has_value());
  expect_shared_rejection(project, Selection{*set});
}

TEST(MeasureEditTest, SoleMeasureDeleteIsRejectedButInsertIsNot) {
  Fixture         fixture(1);
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 0);

  EXPECT_EQ(make_delete_measure_command(fixture.project, selection), nullptr);
  EXPECT_FALSE(
      selection_after_delete_measure(fixture.project, selection).has_value());

  // Insert is unaffected by the sole-measure guard: a one-measure node can
  // still accept an inserted measure.
  EXPECT_NE(make_insert_measure_command(fixture.project, selection,
                                        MeasureInsertMode::kBefore),
            nullptr);
  EXPECT_TRUE(selection_after_insert_measure(fixture.project, selection,
                                             MeasureInsertMode::kBefore)
                  .has_value());
}

TEST(MeasureEditTest, AppendOnSoleMeasureNodeGrowsToTwoMeasures) {
  Fixture         fixture(1);
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 0);

  const std::optional<Selection> predicted = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kAppend);
  ASSERT_TRUE(predicted.has_value());
  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 1u);

  std::unique_ptr<Command> command = make_insert_measure_command(
      fixture.project, selection, MeasureInsertMode::kAppend);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            2u);
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
}

TEST(MeasureEditTest, DeleteLastMeasureOnTwoMeasureNodeClampsToFirstSurvivor) {
  Fixture fixture(2);
  fill_measures_with_markers(fixture.voice(), {Letter::kC, Letter::kD});
  const Selection selection = single_full_measure(
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 1);

  const std::optional<Selection> predicted =
      selection_after_delete_measure(fixture.project, selection);
  ASSERT_TRUE(predicted.has_value());
  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  EXPECT_EQ(predicted_set->items().front().measure_index, 0u);

  const Node               before = *fixture.project.find_node(fixture.node_id);
  std::unique_ptr<Command> command =
      make_delete_measure_command(fixture.project, selection);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->timeline()
                ->measures()
                .measure_count(),
            1u);
  EXPECT_EQ(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kC, 4)),
      Rational(0));
  EXPECT_FALSE(
      onset_of_pitch(fixture.voice(), *SpelledPitch::create(Letter::kD, 4))
          .has_value());
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, selection).empty());
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), before);
}

TEST(MeasureEditTest, InsertMultiStaveSelectionOrdersItemsByStaffLayout) {
  Fixture       fixture({StaffLayout::grand_staff()}, 3);
  const TrackId track0 = fixture.track_ids[0];
  const StaveId treble = fixture.stave_id(0, 0);
  const StaveId bass   = fixture.stave_id(0, 1);

  // Items supplied in reverse stave order: bass first, treble second. The
  // remapped selection must come back in StaffLayout::staves() order
  // (treble over bass), not caller input order.
  const auto set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, bass, 1},
       FullMeasureItem{fixture.node_id, track0, treble, 1}});
  ASSERT_TRUE(set.has_value());
  const Selection selection{*set};

  const std::optional<Selection> predicted = selection_after_insert_measure(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_TRUE(predicted.has_value());
  const auto* predicted_set = std::get_if<FullMeasureSet>(&*predicted);
  ASSERT_NE(predicted_set, nullptr);
  ASSERT_EQ(predicted_set->items().size(), 2u);
  EXPECT_EQ(predicted_set->items()[0].stave, treble);
  EXPECT_EQ(predicted_set->items()[1].stave, bass);
  for (const FullMeasureItem& item : predicted_set->items()) {
    EXPECT_EQ(item.measure_index, 2u);
  }

  std::unique_ptr<Command> command = make_insert_measure_command(
      fixture.project, selection, MeasureInsertMode::kBefore);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());
  EXPECT_TRUE(validate_selection(fixture.project, *predicted).empty());
}

}  // namespace
