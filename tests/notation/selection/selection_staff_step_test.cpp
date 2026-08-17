// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- selection_after_staff_step: keyboard staff step (M5-phase-24) -------

[[nodiscard]] Duration whole_note() {
  return *Duration::create(NoteValue::kWhole, 0);
}

[[nodiscard]] SpelledPitch treble_pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

[[nodiscard]] Voice voice_one() {
  return *Voice::create(1);
}

// A single-item NoteheadSet naming `entity` on the fixture's (track, stave).
[[nodiscard]] Selection notehead_selection(const Fixture&   fixture,
                                           std::size_t      track_index,
                                           std::size_t      stave_index,
                                           NotationEntityId entity) {
  return Selection{*NoteheadSet::create({NoteheadItem{
      fixture.node_id, fixture.track_ids[track_index],
      fixture.stave_id(track_index, stave_index), voice_one(), entity}})};
}

[[nodiscard]] Selection caret_selection(const Fixture& fixture,
                                        std::size_t    track_index,
                                        std::size_t    stave_index,
                                        Rational       position) {
  return Selection{*InsertionCaretSet::create({InsertionCaretItem{
      fixture.node_id, fixture.track_ids[track_index],
      fixture.stave_id(track_index, stave_index), voice_one(), position}})};
}

// Layout options that fit exactly two 4/4 measures per system, so a
// four-measure node breaks into two systems and a tie spanning measures 2
// and 3 crosses that break.
[[nodiscard]] NotationLayoutOptions two_measure_system_options() {
  NotationLayoutOptions options;
  options.system_width          = 340.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 120.0;
  return options;
}

TEST(StaffStepTest, NoteheadSourceStepsToTheNextStaffInTheSameVoice) {
  Fixture    fixture({StaffLayout::single_staff(Clef::kTreble),
                      StaffLayout::single_staff(Clef::kTreble)},
                     1);
  const Note upper = make_note(treble_pitch(Letter::kC), whole_note());
  const Note lower = make_note(treble_pitch(Letter::kE), whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(upper).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(lower).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto stepped = selection_after_staff_step(
      fixture.project, layout, notehead_selection(fixture, 0, 0, upper.id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, lower.id);
  EXPECT_EQ(set->items()[0].track, fixture.track_ids[1]);
  EXPECT_EQ(set->items()[0].stave, fixture.stave_id(1, 0));
  EXPECT_EQ(set->items()[0].voice, voice_one());
  EXPECT_TRUE(validate_selection(fixture.project, *stepped).empty());
}

TEST(StaffStepTest, ChordSourceStepsToTheNextStaff) {
  Fixture     fixture({StaffLayout::single_staff(Clef::kTreble),
                       StaffLayout::single_staff(Clef::kTreble)},
                      1);
  const Chord source = make_chord(
      whole_note(),
      {{NotationEntityId::generate(), treble_pitch(Letter::kC), false},
       {NotationEntityId::generate(), treble_pitch(Letter::kG), false}});
  const Note target = make_note(treble_pitch(Letter::kE), whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(source).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(target).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const Selection selection{*ChordSet::create(
      {ChordItem{fixture.node_id, fixture.track_ids[0], fixture.stave_id(0, 0),
                 voice_one(), source.id}})};
  const auto      stepped = selection_after_staff_step(
      fixture.project, layout, selection, StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, target.id);
}

TEST(StaffStepTest, RestSourceStepsToTheNextStaff) {
  Fixture    fixture({StaffLayout::single_staff(Clef::kTreble),
                      StaffLayout::single_staff(Clef::kTreble)},
                     1);
  const Rest source = make_rest(whole_note());
  const Note target = make_note(treble_pitch(Letter::kE), whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(source).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(target).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const Selection selection{*RestSet::create(
      {RestItem{fixture.node_id, fixture.track_ids[0], fixture.stave_id(0, 0),
                voice_one(), source.id}})};
  const auto      stepped = selection_after_staff_step(
      fixture.project, layout, selection, StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, target.id);
}

TEST(StaffStepTest, InsertionCaretSourceStepsToTheNextStaff) {
  Fixture    fixture({StaffLayout::single_staff(Clef::kTreble),
                      StaffLayout::single_staff(Clef::kTreble)},
                     1);
  const Note target = make_note(treble_pitch(Letter::kE), whole_note());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(target).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto stepped = selection_after_staff_step(
      fixture.project, layout, caret_selection(fixture, 0, 0, Rational(0)),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, target.id);
}

TEST(StaffStepTest, IneligibleSelectionArmsAreNoOps) {
  Fixture    fixture({StaffLayout::single_staff(Clef::kTreble),
                      StaffLayout::single_staff(Clef::kTreble)},
                     1);
  const Note upper = make_note(treble_pitch(Letter::kC), whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(upper).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0)
                  .append(make_note(treble_pitch(Letter::kE), whole_note()))
                  .ok());
  ASSERT_TRUE(fixture.voice(1, 0, 0)
                  .add_dynamic(make_dynamic_marking(upper.id, Dynamic::kMf))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const std::vector<Selection> ineligible = {
      Selection{*MarkingSet::create({MarkingItem{
          fixture.node_id, fixture.track_ids[0], fixture.stave_id(0, 0),
          voice_one(), MarkingKind::kDynamic, upper.id, std::nullopt}})},
      Selection{*FullMeasureSet::create({FullMeasureItem{
          fixture.node_id, fixture.track_ids[0], fixture.stave_id(0, 0), 0}})},
      Selection{*ArbitraryRangeSet::create({ArbitraryRangeItem{
          fixture.node_id, fixture.track_ids[0], fixture.stave_id(0, 0),
          voice_one(), MusicalSpan{Rational(0), Rational(1)}}})},
      Selection{*NodeSet::create({NodeItem{fixture.node_id}})},
      // ConnectorSet is rejected by the arm dispatch; the id needs no
      // matching connector for that, and none exists on this node.
      Selection{*ConnectorSet::create(
          {ConnectorItem{fixture.node_id, ConnectorId::generate()}})},
  };

  for (const Selection& selection : ineligible) {
    EXPECT_EQ(selection_after_staff_step(fixture.project, layout, selection,
                                         StaffStepDirection::kNext),
              std::nullopt);
    EXPECT_EQ(selection_after_staff_step(fixture.project, layout, selection,
                                         StaffStepDirection::kPrevious),
              std::nullopt);
  }
}

TEST(StaffStepTest, MultiItemSelectionIsANoOp) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration half   = *Duration::create(NoteValue::kHalf, 0);
  const Note     first  = make_note(treble_pitch(Letter::kC), half);
  const Note     second = make_note(treble_pitch(Letter::kD), half);
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(first).ok());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(second).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0)
                  .append(make_note(treble_pitch(Letter::kE), whole_note()))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const Selection selection{*NoteheadSet::create(
      {NoteheadItem{fixture.node_id, fixture.track_ids[0],
                    fixture.stave_id(0, 0), voice_one(), first.id},
       NoteheadItem{fixture.node_id, fixture.track_ids[0],
                    fixture.stave_id(0, 0), voice_one(), second.id}})};
  EXPECT_EQ(selection_after_staff_step(fixture.project, layout, selection,
                                       StaffStepDirection::kNext),
            std::nullopt);
}

TEST(StaffStepTest, PreviousAndNextWrapAtBothEndsOfTheNodeStaffList) {
  Fixture           fixture({StaffLayout::single_staff(Clef::kTreble),
                             StaffLayout::single_staff(Clef::kTreble),
                             StaffLayout::single_staff(Clef::kTreble)},
                            1);
  std::vector<Note> notes;
  for (std::size_t track = 0; track < 3; ++track) {
    const Note note = make_note(treble_pitch(Letter::kC), whole_note());
    ASSERT_TRUE(fixture.voice(1, track, 0).append(note).ok());
    notes.push_back(note);
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto landing = [&](std::size_t from, StaffStepDirection direction) {
    const auto stepped = selection_after_staff_step(
        fixture.project, layout,
        notehead_selection(fixture, from, 0, notes[from].id), direction);
    EXPECT_TRUE(stepped.has_value());
    if (!stepped.has_value()) {
      return NotationEntityId::generate();
    }
    const auto* set = std::get_if<NoteheadSet>(&*stepped);
    EXPECT_NE(set, nullptr);
    if (set == nullptr || set->items().size() != 1u) {
      return NotationEntityId::generate();
    }
    return set->items()[0].entity;
  };

  EXPECT_EQ(landing(0, StaffStepDirection::kNext), notes[1].id);
  EXPECT_EQ(landing(1, StaffStepDirection::kNext), notes[2].id);
  // Wrap at the bottom: the last staff's kNext is the first staff.
  EXPECT_EQ(landing(2, StaffStepDirection::kNext), notes[0].id);
  EXPECT_EQ(landing(2, StaffStepDirection::kPrevious), notes[1].id);
  EXPECT_EQ(landing(1, StaffStepDirection::kPrevious), notes[0].id);
  // Wrap at the top: the first staff's kPrevious is the last staff.
  EXPECT_EQ(landing(0, StaffStepDirection::kPrevious), notes[2].id);
}

TEST(StaffStepTest, TrackAddedAfterNodeCreationBecomesAStepTarget) {
  Fixture    fixture({StaffLayout::single_staff(Clef::kTreble),
                      StaffLayout::single_staff(Clef::kTreble)},
                     1);
  const Note upper = make_note(treble_pitch(Letter::kC), whole_note());
  const Note lower = make_note(treble_pitch(Letter::kE), whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(upper).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(lower).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // A track added after the node exists is aligned into the node immediately,
  // including its empty StaveVoices.
  const auto late = fixture.project.add_track(
      "Late", StaffLayout::single_staff(Clef::kTreble),
      *MidiChannel::create(7));
  ASSERT_TRUE(late.has_value());
  ASSERT_EQ(score_ordered_staves(fixture.project).size(), 3u);

  // kNext from the formerly last staff reaches the newly aligned empty staff.
  const auto stepped = selection_after_staff_step(
      fixture.project, layout, notehead_selection(fixture, 1, 0, lower.id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<InsertionCaretSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].position, Rational(0));
  EXPECT_EQ(set->items()[0].track, *late);
}

TEST(StaffStepTest, SingleStaffNodeIsANoOpRatherThanAWrapOntoItself) {
  Fixture    fixture(1);
  const Note only = make_note(treble_pitch(Letter::kC), whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(only).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const Selection selection = notehead_selection(fixture, 0, 0, only.id);
  EXPECT_EQ(selection_after_staff_step(fixture.project, layout, selection,
                                       StaffStepDirection::kNext),
            std::nullopt);
  EXPECT_EQ(selection_after_staff_step(fixture.project, layout, selection,
                                       StaffStepDirection::kPrevious),
            std::nullopt);
}

TEST(StaffStepTest, NearestOnsetInTheTargetVoiceWins) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration eighth  = *Duration::create(NoteValue::kEighth, 0);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t i = 0; i < 8; ++i) {
    ASSERT_TRUE(fixture.voice(1, 0, 0)
                    .append(make_note(treble_pitch(Letter::kC), eighth))
                    .ok());
  }
  std::vector<Note> targets;
  for (std::size_t i = 0; i < 4; ++i) {
    const Note note = make_note(treble_pitch(Letter::kE), quarter);
    ASSERT_TRUE(fixture.voice(1, 1, 0).append(note).ok());
    targets.push_back(note);
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // The source eighth note at onset 1/2 lands on the quarter at onset 1/2.
  const NotationEntityId source_id =
      graphscore::event_id(fixture.voice(1, 0, 0).events()[4]);
  const auto stepped = selection_after_staff_step(
      fixture.project, layout, notehead_selection(fixture, 0, 0, source_id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, targets[2].id);
}

TEST(StaffStepTest, EquidistantOnsetsResolveToTheEarlierOnset) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration eighth  = *Duration::create(NoteValue::kEighth, 0);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t i = 0; i < 8; ++i) {
    ASSERT_TRUE(fixture.voice(1, 0, 0)
                    .append(make_note(treble_pitch(Letter::kC), eighth))
                    .ok());
  }
  std::vector<Note> targets;
  for (std::size_t i = 0; i < 4; ++i) {
    const Note note = make_note(treble_pitch(Letter::kE), quarter);
    ASSERT_TRUE(fixture.voice(1, 1, 0).append(note).ok());
    targets.push_back(note);
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // A caret at 3/8 is exactly 1/8 from the quarters at 1/4 and at 1/2.
  const auto stepped = selection_after_staff_step(
      fixture.project, layout,
      caret_selection(fixture, 0, 0, *Rational::create(3, 8)),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, targets[1].id);
}

TEST(StaffStepTest, ChordTargetProducesAChordSetAndNoteTargetANoteheadSet) {
  Fixture     fixture({StaffLayout::single_staff(Clef::kTreble),
                       StaffLayout::single_staff(Clef::kTreble)},
                      1);
  const Note  source = make_note(treble_pitch(Letter::kC), whole_note());
  const Chord target = make_chord(
      whole_note(),
      {{NotationEntityId::generate(), treble_pitch(Letter::kE), false},
       {NotationEntityId::generate(), treble_pitch(Letter::kG), false}});
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(source).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(target).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto down = selection_after_staff_step(
      fixture.project, layout, notehead_selection(fixture, 0, 0, source.id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(down.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*down);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items()[0].entity, target.id);
  EXPECT_TRUE(validate_selection(fixture.project, *down).empty());

  // And back: stepping from the chord onto the single Note produces the
  // NoteheadSet arm, not a ChordSet.
  const Selection chord_selection{*ChordSet::create(
      {ChordItem{fixture.node_id, fixture.track_ids[1], fixture.stave_id(1, 0),
                 voice_one(), target.id}})};
  const auto      up = selection_after_staff_step(
      fixture.project, layout, chord_selection, StaffStepDirection::kPrevious);
  ASSERT_TRUE(up.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*up);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items()[0].entity, source.id);
}

TEST(StaffStepTest, RestOnlyTargetVoiceYieldsACaretRatherThanSelectingARest) {
  Fixture fixture({StaffLayout::single_staff(Clef::kTreble),
                   StaffLayout::single_staff(Clef::kTreble)},
                  2);
  ASSERT_TRUE(fixture.voice(1, 0, 0)
                  .append(make_note(treble_pitch(Letter::kC), whole_note()))
                  .ok());
  const Note source = make_note(treble_pitch(Letter::kD), whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(source).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto stepped = selection_after_staff_step(
      fixture.project, layout, notehead_selection(fixture, 0, 0, source.id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<InsertionCaretSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].position, Rational(1));
  EXPECT_EQ(set->items()[0].track, fixture.track_ids[1]);
  EXPECT_TRUE(validate_selection(fixture.project, *stepped).empty());
}

TEST(StaffStepTest, EmptyTargetVoiceYieldsACaretAtZero) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         2);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t i = 0; i < 8; ++i) {
    ASSERT_TRUE(fixture.voice(1, 0, 0)
                    .append(make_note(treble_pitch(Letter::kC), quarter))
                    .ok());
  }
  // Voice 2 of the target staff carries the whole node; voice 1 -- the one
  // that carries over -- is empty, and the step never falls through to
  // another voice.
  ASSERT_TRUE(fixture.voice(2, 1, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(2, 1, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).events().empty());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Position 3/2 is nearer the target lane's own total_length (2) than 0,
  // so a plain nearest-boundary snap would land on 2; an empty voice
  // resolves to 0 regardless.
  const NotationEntityId source_id =
      graphscore::event_id(fixture.voice(1, 0, 0).events()[6]);
  const auto stepped = selection_after_staff_step(
      fixture.project, layout, notehead_selection(fixture, 0, 0, source_id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<InsertionCaretSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].position, Rational(0));
  EXPECT_EQ(set->items()[0].voice, voice_one());
  EXPECT_TRUE(validate_selection(fixture.project, *stepped).empty());
}

TEST(StaffStepTest, CaretSnapsAnUnalignedCarriedPositionToTheNearestBoundary) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         2);
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  for (std::size_t i = 0; i < 16; ++i) {
    ASSERT_TRUE(fixture.voice(1, 0, 0)
                    .append(make_note(treble_pitch(Letter::kC), eighth))
                    .ok());
  }
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // 5/8 is not a boundary in the target voice (boundaries: 0, 1, and the
  // lane extent 2); it is 3/8 from 1 and 5/8 from 0.
  const auto stepped = selection_after_staff_step(
      fixture.project, layout,
      caret_selection(fixture, 0, 0, *Rational::create(5, 8)),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<InsertionCaretSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].position, Rational(1));
  EXPECT_TRUE(validate_selection(fixture.project, *stepped).empty());
}

TEST(StaffStepTest, CaretPastTheTargetLaneEndSnapsToItsTotalLength) {
  Fixture fixture({StaffLayout::single_staff(Clef::kTreble),
                   StaffLayout::single_staff(Clef::kTreble)},
                  2);
  ASSERT_TRUE(fixture.voice(1, 0, 0)
                  .append(make_note(treble_pitch(Letter::kC), whole_note()))
                  .ok());
  ASSERT_TRUE(fixture.voice(1, 0, 0)
                  .append(make_note(treble_pitch(Letter::kD), whole_note()))
                  .ok());
  // The target lane holds one measure only, so its total_length is 1 while
  // the source caret sits at 2.
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto stepped = selection_after_staff_step(
      fixture.project, layout, caret_selection(fixture, 0, 0, Rational(2)),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<InsertionCaretSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const TrackLane* target_lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_ids[1]);
  ASSERT_NE(target_lane, nullptr);
  EXPECT_EQ(set->items()[0].position, target_lane->total_length());
  EXPECT_EQ(set->items()[0].position, Rational(1));
  EXPECT_TRUE(validate_selection(fixture.project, *stepped).empty());
}

// The tie clause is a literal VISUAL rule, and this is the case that proves
// it: a tie crossing a system break puts the musically-nearest chain member
// (the tie's origin, one measure away) at the far left of the following
// system, while the chain's later member is drawn close to the source's own
// horizontal position. The step must take the visually nearer one.
TEST(StaffStepTest, TieBreakTakesTheVisuallyNearestChainMemberAcrossASystem) {
  Fixture           fixture({StaffLayout::single_staff(Clef::kTreble),
                             StaffLayout::single_staff(Clef::kTreble)},
                            4);
  std::vector<Note> source_notes;
  for (std::size_t i = 0; i < 4; ++i) {
    const Note note = make_note(treble_pitch(Letter::kC), whole_note());
    ASSERT_TRUE(fixture.voice(1, 0, 0).append(note).ok());
    source_notes.push_back(note);
  }
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());
  const Note chain_first =
      make_note(treble_pitch(Letter::kE), whole_note(), true);
  const Note chain_second = make_note(treble_pitch(Letter::kE), whole_note());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(chain_first).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(chain_second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(layout_notation(
      fixture.project, fixture.node_id, metrics, two_measure_system_options()));
  ASSERT_GE(layout.systems.size(), 2u);

  // The discriminating geometry, read out of the real layout: the chain's
  // LATER member is drawn horizontally nearer the source notehead than the
  // chain's earlier (musically nearer) member is.
  const double source_x = notehead_origin(layout, source_notes[1].id).x;
  const double first_x  = notehead_origin(layout, chain_first.id).x;
  const double second_x = notehead_origin(layout, chain_second.id).x;
  ASSERT_LT(std::abs(second_x - source_x), std::abs(first_x - source_x));

  const auto stepped = selection_after_staff_step(
      fixture.project, layout,
      notehead_selection(fixture, 0, 0, source_notes[1].id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, chain_second.id);
  EXPECT_TRUE(validate_selection(fixture.project, *stepped).empty());
}

// The same geometry with a source that has no notehead of its own: the
// visual rule degrades to the musical one and the tie's origin -- the
// nearest chain member by onset -- is selected instead.
TEST(StaffStepTest, TieBreakDegradesToOnsetWhenTheSourceHasNoNotehead) {
  Fixture fixture({StaffLayout::single_staff(Clef::kTreble),
                   StaffLayout::single_staff(Clef::kTreble)},
                  4);
  ASSERT_TRUE(fixture.voice(1, 0, 0)
                  .append(make_note(treble_pitch(Letter::kC), whole_note()))
                  .ok());
  const Rest source_rest = make_rest(whole_note());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(source_rest).ok());
  for (std::size_t i = 0; i < 2; ++i) {
    ASSERT_TRUE(fixture.voice(1, 0, 0)
                    .append(make_note(treble_pitch(Letter::kC), whole_note()))
                    .ok());
  }
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());
  const Note chain_first =
      make_note(treble_pitch(Letter::kE), whole_note(), true);
  const Note chain_second = make_note(treble_pitch(Letter::kE), whole_note());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(chain_first).ok());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(chain_second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(layout_notation(
      fixture.project, fixture.node_id, metrics, two_measure_system_options()));
  ASSERT_GE(layout.systems.size(), 2u);

  const Selection selection{*RestSet::create(
      {RestItem{fixture.node_id, fixture.track_ids[0], fixture.stave_id(0, 0),
                voice_one(), source_rest.id}})};
  const auto      stepped = selection_after_staff_step(
      fixture.project, layout, selection, StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, chain_first.id);
}

// The chain of a chord candidate is the union of its noteheads' own chains,
// and a chord whose noteheads tie in OPPOSITE directions produces that union
// out of musical order: the notehead that ties forward contributes the
// following event before the notehead that ties backward contributes the
// preceding one. The earlier-onset rule below is a tie-break over the whole
// chain, not over whichever member happened to be produced first, so the
// union has to be ordered before it is scanned.
//
// The geometry that makes the difference observable: the source measure is
// wide enough (a half rest, a quarter rest and the source quarter) to take a
// system of its own, so the tie's origin and the chord each open a system and
// are drawn at exactly the same x -- an exact visual tie between the chord
// and its own earlier chain member, which must resolve to the earlier onset.
TEST(StaffStepTest, ChordChainUnionResolvesAnExactVisualTieToTheEarlierOnset) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         4);
  const Duration half    = *Duration::create(NoteValue::kHalf, 0);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);

  // Source staff: a notehead at 7/4, nearer by onset to the chord at 2 than
  // to either of the chord's own chain neighbours at 1 and at 3.
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(make_rest(half)).ok());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(make_rest(quarter)).ok());
  const Note source = make_note(treble_pitch(Letter::kD), quarter);
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(source).ok());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(make_rest(whole_note())).ok());
  ASSERT_TRUE(fixture.voice(1, 0, 0).append(make_rest(whole_note())).ok());

  // Target staff: chord notehead 0 (C) ties FORWARD into `tied_to`, chord
  // notehead 1 (G) is tied BACKWARD from `tied_from`. Walking the chord's
  // noteheads in index order therefore reaches event 3 before event 1.
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(make_rest(whole_note())).ok());
  const Note tied_from =
      make_note(treble_pitch(Letter::kG), whole_note(), true);
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(tied_from).ok());
  const Chord chord = make_chord(
      whole_note(),
      {{NotationEntityId::generate(), treble_pitch(Letter::kC), true},
       {NotationEntityId::generate(), treble_pitch(Letter::kG), false}});
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(chord).ok());
  const Note tied_to = make_note(treble_pitch(Letter::kC), whole_note());
  ASSERT_TRUE(fixture.voice(1, 1, 0).append(tied_to).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(layout_notation(
      fixture.project, fixture.node_id, metrics, two_measure_system_options()));
  ASSERT_GE(layout.systems.size(), 3u);

  // The discriminating geometry, read out of the real layout: the chord's
  // two noteheads are drawn at exactly the distance from the source that its
  // earlier chain member is, and its later chain member is strictly farther
  // away and so can never win on distance alone.
  const double source_x = notehead_origin(layout, source.id).x;
  const double from_x   = notehead_origin(layout, tied_from.id).x;
  const double to_x     = notehead_origin(layout, tied_to.id).x;
  ASSERT_EQ(notehead_origin(layout, chord.notes[0].id).x, from_x);
  ASSERT_EQ(notehead_origin(layout, chord.notes[1].id).x, from_x);
  ASSERT_LT(std::abs(from_x - source_x), std::abs(to_x - source_x));

  const auto stepped = selection_after_staff_step(
      fixture.project, layout, notehead_selection(fixture, 0, 0, source.id),
      StaffStepDirection::kNext);
  ASSERT_TRUE(stepped.has_value());
  // Named rather than left to the NoteheadSet check below: an unordered
  // union makes the chord itself -- the member the forward-tying notehead
  // contributes first -- win the exact tie, and a ChordSet is exactly what
  // that regression looks like.
  ASSERT_EQ(std::get_if<ChordSet>(&*stepped), nullptr);
  const auto* set = std::get_if<NoteheadSet>(&*stepped);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].entity, tied_from.id);
  EXPECT_TRUE(validate_selection(fixture.project, *stepped).empty());
}
}  // namespace
