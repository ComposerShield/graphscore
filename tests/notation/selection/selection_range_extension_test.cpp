// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- extend_range_selection: Shift/keyboard time-edge extension --------

TEST(RangeExtensionTest, MovingKStartWidensTheSpanKeepingEndFixed) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  const auto extended = extend_range_selection(fixture.project, *existing_set,
                                               RangeEdge::kStart, Rational(0));
  ASSERT_TRUE(extended.has_value());
  const auto* extended_set = std::get_if<ArbitraryRangeSet>(&*extended);
  ASSERT_NE(extended_set, nullptr);
  ASSERT_EQ(extended_set->items().size(), 1u);
  EXPECT_EQ(extended_set->items().front().span,
            (MusicalSpan{Rational(0), Rational(2)}));
}

TEST(RangeExtensionTest, MovingKEndWidensTheSpanKeepingStartFixed) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  const auto extended = extend_range_selection(fixture.project, *existing_set,
                                               RangeEdge::kEnd, Rational(2));
  ASSERT_TRUE(extended.has_value());
  const auto* extended_set = std::get_if<ArbitraryRangeSet>(&*extended);
  ASSERT_NE(extended_set, nullptr);
  ASSERT_EQ(extended_set->items().size(), 1u);
  EXPECT_EQ(extended_set->items().front().span,
            (MusicalSpan{Rational(0), Rational(2)}));
}

TEST(RangeExtensionTest, MovingTheStartPastTheCurrentEndSwaps) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  // kStart moves to 2, past the current end (1) -- the span swaps to
  // [1, 2) rather than being rejected.
  const auto extended = extend_range_selection(fixture.project, *existing_set,
                                               RangeEdge::kStart, Rational(2));
  ASSERT_TRUE(extended.has_value());
  const auto* extended_set = std::get_if<ArbitraryRangeSet>(&*extended);
  ASSERT_NE(extended_set, nullptr);
  EXPECT_EQ(extended_set->items().front().span,
            (MusicalSpan{Rational(1), Rational(2)}));
}

TEST(RangeExtensionTest, MovingTheEndBeforeTheCurrentStartSwaps) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  const auto extended = extend_range_selection(fixture.project, *existing_set,
                                               RangeEdge::kEnd, Rational(0));
  ASSERT_TRUE(extended.has_value());
  const auto* extended_set = std::get_if<ArbitraryRangeSet>(&*extended);
  ASSERT_NE(extended_set, nullptr);
  EXPECT_EQ(extended_set->items().front().span,
            (MusicalSpan{Rational(0), Rational(1)}));
}

// Because VoiceContent packs events contiguously from onset 0 with no
// gaps (see voice_overlaps_span, notation_range_selection.cpp), a fully-packed
// voice overlaps a query span [S, E) exactly when S is less than that voice's
// own total content length -- E plays no part once S already reaches
// it. Consequently a newly *end*-widened span can never newly capture a
// voice a narrower span already missed (only a start-narrowed span can
// newly *exclude* one, and only a start-widened, i.e. earlier, span can
// newly *include* one): this test moves kStart earlier to demonstrate the
// inclusion side.
TEST(RangeExtensionTest, WideningTheSpanAddsANewlyOverlappingVoice) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  // Voice 2's only content is measure 0's own span, [0, 1) -- it ends
  // exactly where the existing span begins, so it starts out excluded.
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);
  ASSERT_EQ(existing_set->items().size(), 1u);  // voice 1 only

  const auto extended = extend_range_selection(fixture.project, *existing_set,
                                               RangeEdge::kStart, Rational(0));
  ASSERT_TRUE(extended.has_value());
  const auto* extended_set = std::get_if<ArbitraryRangeSet>(&*extended);
  ASSERT_NE(extended_set, nullptr);
  ASSERT_EQ(extended_set->items().size(), 2u);
  std::vector<Voice> voices;
  for (const auto& item : extended_set->items()) {
    voices.push_back(item.voice);
  }
  std::ranges::sort(voices);
  EXPECT_EQ(voices, (std::vector<Voice>{*Voice::create(1), *Voice::create(2)}));
}

TEST(RangeExtensionTest, NarrowingTheSpanDropsAVoiceThatNoLongerOverlaps) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(fixture.voice(2).append(make_rest(whole)).ok());
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(2)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);
  ASSERT_EQ(existing_set->items().size(), 2u);

  const auto narrowed = extend_range_selection(fixture.project, *existing_set,
                                               RangeEdge::kStart, Rational(1));
  ASSERT_TRUE(narrowed.has_value());
  const auto* narrowed_set = std::get_if<ArbitraryRangeSet>(&*narrowed);
  ASSERT_NE(narrowed_set, nullptr);
  ASSERT_EQ(narrowed_set->items().size(), 1u);
  EXPECT_EQ(narrowed_set->items().front().voice, *Voice::create(2));
}

TEST(RangeExtensionTest, NarrowingUntilNoVoiceOverlapsYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kQuarter, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  EXPECT_FALSE(extend_range_selection(fixture.project, *existing_set,
                                      RangeEdge::kStart,
                                      *Rational::create(1, 2))
                   .has_value());
}

TEST(RangeExtensionTest, MovingAnEdgeOntoTheFixedEdgeYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  EXPECT_FALSE(extend_range_selection(fixture.project, *existing_set,
                                      RangeEdge::kStart, Rational(1))
                   .has_value());
}

TEST(RangeExtensionTest,
     MovingAnEdgeBeyondTheTimelinesTotalLengthYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  EXPECT_FALSE(extend_range_selection(fixture.project, *existing_set,
                                      RangeEdge::kEnd, Rational(2))
                   .has_value());
}

TEST(RangeExtensionTest, MovingAnEdgeBelowZeroYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  EXPECT_FALSE(extend_range_selection(fixture.project, *existing_set,
                                      RangeEdge::kStart,
                                      *Rational::create(-1, 4))
                   .has_value());
}

// Pins a documented limitation (extend_range_selection's own contract
// comment, graphscore_notation.hpp): the staff range extend_range_selection
// holds fixed is reconstructed from `existing`'s own items, so a staff at
// the extreme of the originally resolved range is recoverable through an
// edge-only extension only if it happened to contribute an item at initial
// resolution -- regardless of whether its content overlaps the widened
// span. Track 2 here is built to make that discriminating: its own whole
// note occupies [0, 1), and the extension below widens the span to
// [0, 2), which *does* overlap it. An implementation that instead held the
// full, originally resolved [staff0, staff2] range fixed (rather than
// reconstructing it from items) would therefore include a track 2 item in
// the extended result. Track 2's absence below is attributable to the
// reconstruction discarding staff 2 at initial resolution -- it
// contributed no item to the original span [1, 2), which ends exactly
// where track 2's own note ends -- not to track 2 lacking overlapping
// content once the span widens. This pins that intentional, lossy behavior
// exactly as documented, not a desirable property: a caller that must
// preserve such a staff has to track the staff endpoints alongside the
// Selection itself rather than reconstructing them from items, per that
// same comment.
TEST(RangeExtensionTest,
     AStaffWithNoOverlappingVoiceIsNotPreservedThroughEdgeExtension) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  // Track 2's only content is measure 0's own span, [0, 1) -- it ends
  // exactly where the existing span begins, so it starts out excluded,
  // exactly like voice 2 in WideningTheSpanAddsANewlyOverlappingVoice above.
  ASSERT_TRUE(
      fixture.voice(1, 2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());
  const MeasureScope staff0{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope staff2{fixture.track_ids[2], fixture.stave_id(2)};

  const auto existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff0, staff2});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);
  // Staff 2 already contributes no item at initial resolution -- the loss
  // this test pins starts here, before extend_range_selection is involved.
  ASSERT_EQ(existing_set->items().size(), 2u);
  for (const auto& item : existing_set->items()) {
    EXPECT_NE(item.track, fixture.track_ids[2]);
  }

  // Widening the span's start reconstructs the "fixed" staff range as
  // [staff0, staff1] -- the score-order extent of the *items*, not the
  // originally resolved [staff0, staff2] range -- so staff 2 stays
  // unreachable even though the new span [0, 2) does overlap its content.
  const auto extended = extend_range_selection(fixture.project, *existing_set,
                                               RangeEdge::kStart, Rational(0));
  ASSERT_TRUE(extended.has_value());
  const auto* extended_set = std::get_if<ArbitraryRangeSet>(&*extended);
  ASSERT_NE(extended_set, nullptr);
  for (const auto& item : extended_set->items()) {
    EXPECT_NE(item.track, fixture.track_ids[2]);
  }
}

TEST(RangeExtensionTest, MisalignedExistingSetIsRejected) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice(2)
                  .append(make_note(*SpelledPitch::create(Letter::kG, 3),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  // Hand-built set whose two items disagree on span -- never produced by
  // resolve_range_selection or resolve_range_selection_spec.
  const auto misaligned = ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                          fixture.stave_id(), *Voice::create(1),
                          MusicalSpan{Rational(0), Rational(1)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                          fixture.stave_id(), *Voice::create(2),
                          MusicalSpan{*Rational::create(1, 4), Rational(1)}}});
  ASSERT_TRUE(misaligned.has_value());

  EXPECT_FALSE(extend_range_selection(fixture.project, *misaligned,
                                      RangeEdge::kStart, Rational(0))
                   .has_value());
}

TEST(RangeExtensionTest, ExistingSetNamingAnUnknownNodeIsRejected) {
  Fixture    fixture(1);
  const auto fabricated = ArbitraryRangeSet::create({ArbitraryRangeItem{
      NodeId::generate(), fixture.track_ids[0], fixture.stave_id(),
      *Voice::create(1), MusicalSpan{Rational(0), Rational(1)}}});
  ASSERT_TRUE(fabricated.has_value());
  EXPECT_FALSE(extend_range_selection(fixture.project, *fabricated,
                                      RangeEdge::kStart, Rational(0))
                   .has_value());
}

TEST(RangeExtensionTest, ExistingSetNamingANodeWithoutATimelineIsRejected) {
  Fixture      fixture(1);
  const NodeId bare_node  = fixture.project.add_node("Bare");
  const auto   fabricated = ArbitraryRangeSet::create({ArbitraryRangeItem{
      bare_node, fixture.track_ids[0], fixture.stave_id(), *Voice::create(1),
      MusicalSpan{Rational(0), Rational(1)}}});
  ASSERT_TRUE(fabricated.has_value());
  EXPECT_FALSE(extend_range_selection(fixture.project, *fabricated,
                                      RangeEdge::kStart, Rational(0))
                   .has_value());
}

// ---- extend_range_selection_staff_scope: Shift/keyboard staff-axis
//      extension --------------------------------------------------------

TEST(RangeStaffScopeExtensionTest, WideningAcrossTracksAddsFurtherStaves) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  for (std::size_t track = 0; track < 3; ++track) {
    ASSERT_TRUE(
        fixture.voice(1, track)
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
            .ok());
  }
  const MeasureScope staff0{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope staff2{fixture.track_ids[2], fixture.stave_id(2)};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff0, staff0});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);
  ASSERT_EQ(existing_set->items().size(), 1u);

  const auto widened = extend_range_selection_staff_scope(
      fixture.project, *existing_set, staff0, staff2);
  ASSERT_TRUE(widened.has_value());
  const auto* widened_set = std::get_if<ArbitraryRangeSet>(&*widened);
  ASSERT_NE(widened_set, nullptr);
  ASSERT_EQ(widened_set->items().size(), 3u);
  std::vector<TrackId> tracks;
  for (const auto& item : widened_set->items()) {
    tracks.push_back(item.track);
  }
  EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[0]), tracks.end());
  EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[1]), tracks.end());
  EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[2]), tracks.end());
}

TEST(RangeStaffScopeExtensionTest, ContractingRemovesTrailingStaves) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  for (std::size_t track = 0; track < 3; ++track) {
    ASSERT_TRUE(
        fixture.voice(1, track)
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
            .ok());
  }
  const MeasureScope staff0{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope staff1{fixture.track_ids[1], fixture.stave_id(1)};
  const MeasureScope staff2{fixture.track_ids[2], fixture.stave_id(2)};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff0, staff2});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);
  ASSERT_EQ(existing_set->items().size(), 3u);

  const auto contracted = extend_range_selection_staff_scope(
      fixture.project, *existing_set, staff0, staff1);
  ASSERT_TRUE(contracted.has_value());
  const auto* contracted_set = std::get_if<ArbitraryRangeSet>(&*contracted);
  ASSERT_NE(contracted_set, nullptr);
  ASSERT_EQ(contracted_set->items().size(), 2u);
  std::vector<TrackId> tracks;
  for (const auto& item : contracted_set->items()) {
    tracks.push_back(item.track);
  }
  EXPECT_EQ(std::ranges::find(tracks, fixture.track_ids[2]), tracks.end());
}

TEST(RangeStaffScopeExtensionTest, OrderInsensitivityOfTheTwoEndpoints) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 3), whole))
          .ok());
  const MeasureScope staff0{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope staff1{fixture.track_ids[1], fixture.stave_id(1)};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff0, staff0});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  const auto forward = extend_range_selection_staff_scope(
      fixture.project, *existing_set, staff0, staff1);
  const auto reversed = extend_range_selection_staff_scope(
      fixture.project, *existing_set, staff1, staff0);
  ASSERT_TRUE(forward.has_value());
  ASSERT_TRUE(reversed.has_value());
  EXPECT_EQ(*forward, *reversed);
}

TEST(RangeStaffScopeExtensionTest, IdempotenceOnAnUnchangedEndpoint) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 3), whole))
          .ok());
  const MeasureScope staff0{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope staff1{fixture.track_ids[1], fixture.stave_id(1)};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff0, staff1});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  const auto repeated = extend_range_selection_staff_scope(
      fixture.project, *existing_set, staff0, staff1);
  ASSERT_TRUE(repeated.has_value());
  EXPECT_EQ(*existing_selection, *repeated);
}

TEST(RangeStaffScopeExtensionTest, MisalignedExistingSetIsRejected) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice(2)
                  .append(make_note(*SpelledPitch::create(Letter::kG, 3),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const auto misaligned = ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                          fixture.stave_id(), *Voice::create(1),
                          MusicalSpan{Rational(0), Rational(1)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                          fixture.stave_id(), *Voice::create(2),
                          MusicalSpan{*Rational::create(1, 4), Rational(1)}}});
  ASSERT_TRUE(misaligned.has_value());

  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  EXPECT_FALSE(extend_range_selection_staff_scope(fixture.project, *misaligned,
                                                  staff, staff)
                   .has_value());
}

TEST(RangeStaffScopeExtensionTest, UnknownStaffEndpointIsRejected) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  const MeasureScope unknown{TrackId::generate(), StaveId::generate()};
  EXPECT_FALSE(extend_range_selection_staff_scope(fixture.project,
                                                  *existing_set, staff, unknown)
                   .has_value());
}

TEST(RangeStaffScopeExtensionTest,
     NoOverlapInTheNewStaffRangeYieldsNoSelection) {
  Fixture fixture({StaffLayout::single_staff(Clef::kTreble),
                   StaffLayout::single_staff(Clef::kBass)},
                  1);
  ASSERT_TRUE(fixture.voice(1, 0)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  // Track 1's stave carries no content at all.
  const MeasureScope staff0{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope staff1{fixture.track_ids[1], fixture.stave_id(1)};
  const auto         existing_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff0, staff0});
  ASSERT_TRUE(existing_selection.has_value());
  const auto* existing_set =
      std::get_if<ArbitraryRangeSet>(&*existing_selection);
  ASSERT_NE(existing_set, nullptr);

  EXPECT_FALSE(extend_range_selection_staff_scope(fixture.project,
                                                  *existing_set, staff1, staff1)
                   .has_value());
}

TEST(RangeStaffScopeExtensionTest,
     ExistingSetNamingANodeWithoutATimelineIsRejected) {
  Fixture      fixture(1);
  const NodeId bare_node  = fixture.project.add_node("Bare");
  const auto   fabricated = ArbitraryRangeSet::create({ArbitraryRangeItem{
      bare_node, fixture.track_ids[0], fixture.stave_id(), *Voice::create(1),
      MusicalSpan{Rational(0), Rational(1)}}});
  ASSERT_TRUE(fabricated.has_value());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  EXPECT_FALSE(extend_range_selection_staff_scope(fixture.project, *fabricated,
                                                  staff, staff)
                   .has_value());
}
}  // namespace
