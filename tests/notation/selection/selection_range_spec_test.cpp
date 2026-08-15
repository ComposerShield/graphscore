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

// ---- resolve_range_selection_spec: musical-coordinate equivalent of a
//      pointer-drag range selection -----------------------------------

[[nodiscard]] std::vector<std::pair<TrackId, StaveId>> test_score_order(
    const Project& project) {
  std::vector<std::pair<TrackId, StaveId>> order;
  for (const auto& track : project.active_tracks()) {
    for (const auto& stave : track.layout().staves()) {
      order.emplace_back(track.id(), stave.id);
    }
  }
  return order;
}

struct RangeExtent {
  MusicalSpan  span;
  MeasureScope first_staff;
  MeasureScope last_staff;
};

// Derives the span and score-order staff endpoints from a resolved
// ArbitraryRangeSet the way a real caller would: read the result of a
// pointer drag back out, then feed it into resolve_range_selection_spec.
// This is the equivalence property's own test machinery, not production
// code -- it deliberately does not call any private notation helper.
[[nodiscard]] RangeExtent range_extent(const Project&           project,
                                       const ArbitraryRangeSet& set) {
  const auto                 order = test_score_order(project);
  std::optional<std::size_t> lower;
  std::optional<std::size_t> upper;
  for (const auto& item : set.items()) {
    const auto position =
        std::ranges::find(order, std::pair{item.track, item.stave});
    EXPECT_NE(position, order.end());
    const auto index = static_cast<std::size_t>(position - order.begin());
    if (!lower.has_value() || index < *lower) {
      lower = index;
    }
    if (!upper.has_value() || index > *upper) {
      upper = index;
    }
  }
  return RangeExtent{set.items().front().span,
                     MeasureScope{order[*lower].first, order[*lower].second},
                     MeasureScope{order[*upper].first, order[*upper].second}};
}

TEST(RangeSpecTest, EquivalenceWithSingleStaffPointerDrag) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto drag_selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(drag_selection.has_value());
  const auto* drag_set = std::get_if<ArbitraryRangeSet>(&*drag_selection);
  ASSERT_NE(drag_set, nullptr);

  const RangeExtent extent         = range_extent(fixture.project, *drag_set);
  const auto        spec_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, extent.span, extent.first_staff,
                         extent.last_staff});
  ASSERT_TRUE(spec_selection.has_value());
  EXPECT_EQ(*drag_selection, *spec_selection);
}

TEST(RangeSpecTest, EquivalenceWithMultiStaffMultiTrackPointerDrag) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 5), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 1, 0);

  const auto drag_selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(drag_selection.has_value());
  const auto* drag_set = std::get_if<ArbitraryRangeSet>(&*drag_selection);
  ASSERT_NE(drag_set, nullptr);

  const RangeExtent extent         = range_extent(fixture.project, *drag_set);
  const auto        spec_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, extent.span, extent.first_staff,
                         extent.last_staff});
  ASSERT_TRUE(spec_selection.has_value());
  EXPECT_EQ(*drag_selection, *spec_selection);
}

TEST(RangeSpecTest, EquivalenceWithASystemBreakSpanningPointerDrag) {
  Fixture        fixture(2);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 8; ++index) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_EQ(layout.systems.size(), 2u);
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 1, 0, 0);

  const auto drag_selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(drag_selection.has_value());
  const auto* drag_set = std::get_if<ArbitraryRangeSet>(&*drag_selection);
  ASSERT_NE(drag_set, nullptr);

  const RangeExtent extent         = range_extent(fixture.project, *drag_set);
  const auto        spec_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, extent.span, extent.first_staff,
                         extent.last_staff});
  ASSERT_TRUE(spec_selection.has_value());
  EXPECT_EQ(*drag_selection, *spec_selection);
}

TEST(RangeSpecTest, EquivalenceWithABackwardDragUsingSwappedStaffOrder) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kE, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint staff2_left  = measure_left_edge(layout, 0, 2, 0);
  const NotationPoint staff0_right = measure_right_edge(layout, 0, 0, 0);

  const auto drag_selection = resolve_range_selection(
      fixture.project, layout, staff2_left, staff0_right);
  ASSERT_TRUE(drag_selection.has_value());
  const auto* drag_set = std::get_if<ArbitraryRangeSet>(&*drag_selection);
  ASSERT_NE(drag_set, nullptr);

  const RangeExtent extent = range_extent(fixture.project, *drag_set);
  // Deliberately swap first_staff/last_staff relative to `extent` --
  // order-insensitivity means this still yields the identical selection.
  const auto swapped_spec_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, extent.span, extent.last_staff,
                         extent.first_staff});
  ASSERT_TRUE(swapped_spec_selection.has_value());
  EXPECT_EQ(*drag_selection, *swapped_spec_selection);
}

TEST(RangeSpecTest, ZeroLengthSpanYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(0)},
                         staff, staff});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, NegativeSpanStartYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id,
                         MusicalSpan{*Rational::create(-1, 4), Rational(1)},
                         staff, staff});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, SpanBeyondTheTimelinesTotalLengthYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(2)},
                         staff, staff});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, UnknownNodeYieldsNoSelection) {
  Fixture            fixture(1);
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{NodeId::generate(),
                         MusicalSpan{Rational(0), Rational(1)}, staff, staff});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, NodeWithoutATimelineYieldsNoSelection) {
  Fixture            fixture(1);
  const NodeId       bare_node = fixture.project.add_node("Bare");
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{bare_node, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, StaffEndpointNotInScoreOrderYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope unknown{TrackId::generate(), StaveId::generate()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         unknown, unknown});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, ArchivedTrackStaffEndpointYieldsNoSelection) {
  Fixture fixture({StaffLayout::single_staff(Clef::kTreble),
                   StaffLayout::single_staff(Clef::kBass)},
                  1);
  ASSERT_TRUE(fixture.voice(1, 0)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  // Read both staves before archiving: Fixture::stave_id indexes into
  // Project::active_tracks(), which archive_track shrinks.
  const MeasureScope active{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope archived{fixture.track_ids[1], fixture.stave_id(1)};
  ASSERT_TRUE(fixture.project.archive_track(fixture.track_ids[1]).ok());
  const auto selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         active, archived});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, StaveBelongingToADifferentTrackYieldsNoSelection) {
  Fixture fixture({StaffLayout::single_staff(Clef::kTreble),
                   StaffLayout::single_staff(Clef::kBass)},
                  1);
  ASSERT_TRUE(fixture.voice(1, 0)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  // stave1 belongs to track 1's own layout, but the endpoint pairs it with
  // track 0.
  const MeasureScope mismatched{fixture.track_ids[0], fixture.stave_id(1)};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         mismatched, mismatched});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, NoVoiceOverlappingTheSpanAnywhereYieldsNoSelection) {
  Fixture fixture(2);
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff, staff});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, SingleVoiceScoreNeverProducesTheOtherThreeVoicesItems) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().voice, *Voice::create(1));
}

// Mirrors resolve_range_selection's own
// VoiceContentOutsideTheDraggedSpanIsExcludedEvenThoughNonEmpty: a voice
// whose only content ends exactly at the query span's own start is
// excluded, per the half-open [onset, onset+duration) overlap rule.
//
// The mirror case -- a voice whose content *starts* exactly at the query
// span's own end -- is not independently constructible as a distinct test
// here: VoiceContent packs events contiguously from onset 0 (see
// voice_overlaps_span, notation_range_selection.cpp), so an event with onset >
// 0 always implies some earlier event in the *same* voice already occupies [0,
// onset), and that earlier event necessarily overlaps any span touching that
// region too. The two symmetric halves of the half-open rule therefore collapse
// into this one observable test for any span that (like every span this or
// resolve_range_selection can produce) begins within already-filled voice
// content.
TEST(RangeSpecTest, EventEndingExactlyAtSpanStartIsExcluded) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff, staff});
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSpecTest, ItemOrderIsScoreOrderRegardlessOfCallerEndpointOrder) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  for (std::size_t track = 0; track < 3; ++track) {
    ASSERT_TRUE(
        fixture.voice(1, track)
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
            .ok());
  }
  const MeasureScope first{fixture.track_ids[0], fixture.stave_id(0)};
  const MeasureScope last{fixture.track_ids[2], fixture.stave_id(2)};

  const auto forward = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         first, last});
  const auto reversed = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         last, first});
  ASSERT_TRUE(forward.has_value());
  ASSERT_TRUE(reversed.has_value());
  EXPECT_EQ(*forward, *reversed);

  const auto* set = std::get_if<ArbitraryRangeSet>(&*forward);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 3u);
  EXPECT_EQ(set->items()[0].track, fixture.track_ids[0]);
  EXPECT_EQ(set->items()[1].track, fixture.track_ids[1]);
  EXPECT_EQ(set->items()[2].track, fixture.track_ids[2]);
}

// ---- score_ordered_staves -----------------------------------------------

TEST(ScoreOrderedStavesTest,
     MultiTrackMultiStaveOrderMatchesActiveTracksThenStaves) {
  Fixture fixture(
      {StaffLayout::grand_staff(), StaffLayout::single_staff(Clef::kTreble)},
      1);

  const std::vector<MeasureScope> order = score_ordered_staves(fixture.project);

  const std::vector<MeasureScope> expected{
      MeasureScope{fixture.track_ids[0], fixture.stave_id(0, 0)},
      MeasureScope{fixture.track_ids[0], fixture.stave_id(0, 1)},
      MeasureScope{fixture.track_ids[1], fixture.stave_id(1, 0)},
  };
  EXPECT_EQ(order, expected);
}

TEST(ScoreOrderedStavesTest, ProjectWithNoActiveTracksReturnsAnEmptyVector) {
  Project project{ProjectId::generate(), "Empty"};
  EXPECT_TRUE(score_ordered_staves(project).empty());
}

// Proves score_ordered_staves' own order agrees with the order
// resolve_range_selection_spec actually applies: feeding its front() and
// back() straight back in as first_staff/last_staff must select every
// staff in between, in that same order, matching the two-copies-drift
// concern the design forbids.
TEST(ScoreOrderedStavesTest,
     FrontAndBackFeedDirectlyIntoRangeSelectionSpecEndpoints) {
  Fixture fixture(
      {StaffLayout::grand_staff(), StaffLayout::single_staff(Clef::kTreble)},
      1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 0, 1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 3), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const std::vector<MeasureScope> order = score_ordered_staves(fixture.project);
  ASSERT_EQ(order.size(), 3u);

  const auto selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         order.front(), order.back()});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 3u);
  for (std::size_t index = 0; index < order.size(); ++index) {
    EXPECT_EQ(set->items()[index].track, order[index].track_id);
    EXPECT_EQ(set->items()[index].stave, order[index].stave_id);
  }
}
}  // namespace
