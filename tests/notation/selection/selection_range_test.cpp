// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- resolve_range_selection: dedicated selection-tool pointer drag ----

TEST(RangeSelectionTest,
     SingleStaffSingleVoiceDragProducesOneItemWithTheExpectedSpan) {
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

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const ArbitraryRangeItem& item = set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  EXPECT_EQ(item.span, (MusicalSpan{Rational(0), Rational(1)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     DragAcrossTwoVoicesOnOneStaveProducesTwoItemsWithIdenticalSpans) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  const MusicalSpan  expected_span{Rational(0), Rational(1)};
  std::vector<Voice> voices;
  for (const ArbitraryRangeItem& item : set->items()) {
    EXPECT_EQ(item.span, expected_span);
    EXPECT_EQ(item.track, fixture.track_ids[0]);
    EXPECT_EQ(item.stave, fixture.stave_id());
    voices.push_back(item.voice);
  }
  std::ranges::sort(voices);
  EXPECT_EQ(voices, (std::vector<Voice>{*Voice::create(1), *Voice::create(2)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     DragAcrossMultipleStavesAndTracksProducesOneItemPerContentfulVoice) {
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

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  const MusicalSpan    expected_span{Rational(0), Rational(1)};
  std::vector<TrackId> tracks;
  for (const ArbitraryRangeItem& item : set->items()) {
    EXPECT_EQ(item.span, expected_span);
    EXPECT_EQ(item.voice, *Voice::create(1));
    tracks.push_back(item.track);
  }
  EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[0]), tracks.end());
  EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[1]), tracks.end());
  EXPECT_NE(fixture.track_ids[0], fixture.track_ids[1]);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     VoiceContentOutsideTheDraggedSpanIsExcludedEvenThoughNonEmpty) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  // Voice 1 spans both measures, so its second whole note occupies
  // measure 1's own span.
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  // Voice 2 is non-empty but stops exactly at the end of measure 0 -- its
  // one event's own extent, [0, 1), ends exactly at measure 1's own span
  // start, so it does not overlap a drag confined to measure 1.
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 1);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 1);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().voice, *Voice::create(1));
  EXPECT_EQ(set->items().front().span, (MusicalSpan{Rational(1), Rational(2)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     NoVoiceOverlappingTheDraggedSpanAnywhereYieldsNoSelection) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  // Voice 1's only content is measure 0; measure 1 has no content in any
  // voice at all, so a drag confined to measure 1 reaches the voice scan
  // (staff and measure both resolve, the span is non-degenerate) but finds
  // nothing overlapping anywhere in the resolved staff range.
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout empty_layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(empty_layout, 0, 0, 1);
  const NotationPoint focus  = measure_right_edge(empty_layout, 0, 0, 1);

  EXPECT_FALSE(
      resolve_range_selection(fixture.project, empty_layout, anchor, focus)
          .has_value());

  // Positive control: identical fixture and identical (staff, measure)
  // drag geometry, except measure 1 now has content -- proves the nullopt
  // above comes from the empty voice scan, not from the drag geometry
  // itself failing to resolve a staff/measure/span.
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  const NotationLayout filled_layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint filled_anchor = measure_left_edge(filled_layout, 0, 0, 1);
  const NotationPoint filled_focus = measure_right_edge(filled_layout, 0, 0, 1);

  const auto selection = resolve_range_selection(fixture.project, filled_layout,
                                                 filled_anchor, filled_focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     AnInteriorStaffIsIncludedAndAnOutOfRangeStaffIsExcluded) {
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
  const NotationPoint staff0_left  = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint staff0_right = measure_right_edge(layout, 0, 0, 0);
  const NotationPoint staff1_right = measure_right_edge(layout, 0, 1, 0);
  const NotationPoint staff2_left  = measure_left_edge(layout, 0, 2, 0);
  const NotationPoint staff2_right = measure_right_edge(layout, 0, 2, 0);

  // Case 1: staff 0 -> staff 2 includes the interior staff 1's own content.
  const auto downward_selection = resolve_range_selection(
      fixture.project, layout, staff0_left, staff2_right);
  ASSERT_TRUE(downward_selection.has_value());
  const auto* downward_set =
      std::get_if<ArbitraryRangeSet>(&*downward_selection);
  ASSERT_NE(downward_set, nullptr);
  ASSERT_EQ(downward_set->items().size(), 3u);
  {
    std::vector<TrackId> tracks;
    for (const ArbitraryRangeItem& item : downward_set->items()) {
      tracks.push_back(item.track);
    }
    EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[0]), tracks.end());
    EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[1]), tracks.end());
    EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[2]), tracks.end());
  }

  // Case 2: staff 0 -> staff 1 excludes the out-of-range staff 2.
  const auto narrow_selection = resolve_range_selection(
      fixture.project, layout, staff0_left, staff1_right);
  ASSERT_TRUE(narrow_selection.has_value());
  const auto* narrow_set = std::get_if<ArbitraryRangeSet>(&*narrow_selection);
  ASSERT_NE(narrow_set, nullptr);
  ASSERT_EQ(narrow_set->items().size(), 2u);
  std::vector<TrackId> narrow_tracks;
  for (const ArbitraryRangeItem& item : narrow_set->items()) {
    narrow_tracks.push_back(item.track);
  }
  EXPECT_NE(std::ranges::find(narrow_tracks, fixture.track_ids[0]),
            narrow_tracks.end());
  EXPECT_NE(std::ranges::find(narrow_tracks, fixture.track_ids[1]),
            narrow_tracks.end());
  EXPECT_EQ(std::ranges::find(narrow_tracks, fixture.track_ids[2]),
            narrow_tracks.end());

  // Case 3: the upward drag staff 2 -> staff 0 yields the identical set of
  // items as the downward drag in case 1 -- also the only coverage of
  // std::minmax over the two score_order iterators.
  const auto upward_selection = resolve_range_selection(
      fixture.project, layout, staff2_left, staff0_right);
  ASSERT_TRUE(upward_selection.has_value());
  EXPECT_EQ(*downward_selection, *upward_selection);
}

TEST(RangeSelectionTest, ABackwardsOrUpwardDragProducesTheSameSpanAsForward) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint forward_anchor    = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint forward_focus     = measure_right_edge(layout, 0, 0, 0);
  const auto          forward_selection = resolve_range_selection(
      fixture.project, layout, forward_anchor, forward_focus);
  ASSERT_TRUE(forward_selection.has_value());

  // The backward drag's anchor is both to the right of and (trivially,
  // same staff) not below the focus, and its focus is to the left of the
  // anchor -- the "focus left of / above anchor" case.
  const NotationPoint backward_anchor    = measure_right_edge(layout, 0, 0, 0);
  const NotationPoint backward_focus     = measure_left_edge(layout, 0, 0, 0);
  const auto          backward_selection = resolve_range_selection(
      fixture.project, layout, backward_anchor, backward_focus);
  ASSERT_TRUE(backward_selection.has_value());

  EXPECT_EQ(*forward_selection, *backward_selection);
  EXPECT_TRUE(validate_selection(fixture.project, *backward_selection).empty());
}

TEST(RangeSelectionTest, ADragSpanningASystemBreakProducesOneContiguousSpan) {
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
  ASSERT_EQ(layout.systems[1].first_measure, 1u);

  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 1, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().span, (MusicalSpan{Rational(0), Rational(2)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest, ADegenerateZeroLengthDragYieldsNoSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = measure_left_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, point, point);
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSelectionTest, EitherEndpointOffAnyStaveYieldsNoSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint on_staff = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint off_staff{-10'000.0, -10'000.0};

  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, on_staff, off_staff)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, off_staff, on_staff)
          .has_value());
}

TEST(RangeSelectionTest, EitherEndpointBeingNonFiniteYieldsNoSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint on_staff = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint nan_point{std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::quiet_NaN()};
  const NotationPoint infinite_point{std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity()};

  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, on_staff, nan_point)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, nan_point, on_staff)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, on_staff, infinite_point)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, infinite_point, on_staff)
          .has_value());
}

TEST(RangeSelectionTest,
     AMultiItemRangeSelectionRoundTripsThroughExtractArbitraryRange) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());

  const FragmentExtraction extraction =
      extract_fragment(fixture.project, *selection);
  EXPECT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  std::vector<Voice> part_voices;
  for (const auto& part : extraction.fragment->parts()) {
    part_voices.push_back(part.voice);
  }
  std::ranges::sort(part_voices);
  EXPECT_EQ(part_voices,
            (std::vector<Voice>{*Voice::create(1), *Voice::create(2)}));
}
}  // namespace
