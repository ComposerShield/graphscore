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

// ---- build_range_highlight_rects -------------------------------------------

TEST(HighlightRectsTest, EmptyOnNonRangeSelection) {
  Fixture              fixture(1);
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto caret_set = InsertionCaretSet::create({{InsertionCaretItem{
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(),
      *Voice::create(1), Rational(0)}}});
  ASSERT_TRUE(caret_set.has_value());
  const Selection caret = *caret_set;
  EXPECT_TRUE(
      build_range_highlight_rects(caret, fixture.project, layout).empty());
}

TEST(HighlightRectsTest, FullMeasureDragProducesOneRectPerSelectedStaff) {
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

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // One rect per measure in the span.  The fixture has exactly one measure.
  EXPECT_EQ(rects.size(), 1u);

  // Exact NotationRect equality: for a full-measure drag across measure 0
  // of the default fixture (C major, 4/4, staff_space=10), the highlight
  // rect spans the rhythmic area of the measure horizontally and the
  // staff vertically.  The x starts after the leading area (clef, key,
  // time signature) and width equals the rhythmic width.
  {
    const auto& staff   = layout.systems[0].staves[0];
    const auto& measure = layout.systems[0].measures[0];
    // measure_leading_width for C major, 4/4, measure 0, staff_space=10:
    //   min(measure_width - 20, 80)
    constexpr double kStaffSpace = 10.0;
    const double     leading =
        std::min(measure.bounds.width - kStaffSpace * 2.0, kStaffSpace * 8.0);
    const double rhythmic_width =
        std::max(kStaffSpace, measure.bounds.width - leading - kStaffSpace);
    const NotationRect expected{measure.bounds.x + leading, staff.bounds.y,
                                rhythmic_width, staff.bounds.height};
    EXPECT_EQ(rects.front(), expected);
  }
}

TEST(HighlightRectsTest, FullMeasureSetShowsEveryContiguousMeasure) {
  Fixture              fixture(2);
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto set = FullMeasureSet::create({FullMeasureItem{
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(), 0, 2}});
  ASSERT_TRUE(set.has_value());

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(Selection{*set}, fixture.project, layout);
  EXPECT_EQ(rects.size(), 2u);
}

TEST(HighlightRectsTest, NullProjectIsEmpty) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);
  const auto          selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());

  const Project empty_project{ProjectId::generate(), "Empty"};
  EXPECT_TRUE(
      build_range_highlight_rects(*selection, empty_project, layout).empty());
}

TEST(HighlightRectsTest, MultiTrackProducesRectsForEachSelectedStaff) {
  // Two tracks, each with one staff. Both are selected; each should produce
  // its own rect.
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t track = 0; track < 2; ++track) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(
          fixture.voice(1, track)
              .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
              .ok());
    }
  }

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
  EXPECT_GE(set->items().size(), 2u);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // One rect per measure per staff (2 staves × 1 measure).
  EXPECT_EQ(rects.size(), 2u);
}

TEST(HighlightRectsTest, RepeatedStaveIdAcrossTracksIsDisambiguated) {
  // Two tracks, each with one staff. StaveIds are created independently per
  // track and may collide. The highlight projection must use (TrackId,
  // StaveId) keys so staves from different tracks are not conflated.
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t track = 0; track < 2; ++track) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(
          fixture.voice(1, track)
              .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
              .ok());
    }
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Select only the first track by anchoring and focusing within its
  // staff bounds.
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // Only the first track should produce rects, even if StaveIds happen to
  // collide.
  EXPECT_EQ(rects.size(), 1u);
  EXPECT_DOUBLE_EQ(rects[0].y, layout.systems[0].staves[0].bounds.y);
}

TEST(HighlightRectsTest, MixedSpanItemsEachProjectedIndependently) {
  // Construct two items with different musical spans on the same staff.
  // build_range_highlight_rects must project each item's span independently.
  Fixture        fixture(2);  // 2 measures
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Build a range set manually with two items: one spanning measure 0,
  // the other spanning measure 1.
  const Voice voice1 = *Voice::create(1);
  const auto  r1     = Rational::create(1, 4);
  const auto  r2     = Rational::create(1, 2);
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  std::vector<ArbitraryRangeItem> items;
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{Rational(0), *r1}});
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{*r1, *r2}});
  const auto range_set = ArbitraryRangeSet::create(std::move(items));
  ASSERT_TRUE(range_set.has_value());
  const Selection sel = *range_set;

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(sel, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // Items [0, ¼) and [¼, ½) are touching on the same measure; the
  // interval-coalescing logic merges touching intervals into one rect.
  EXPECT_EQ(rects.size(), 1u);

  // All rects should be on the same staff.
  for (const NotationRect& r : rects) {
    EXPECT_DOUBLE_EQ(r.y, layout.systems[0].staves[0].bounds.y);
    EXPECT_GT(r.width, 0.0);
  }
}

// Two items with disjoint spans on the same staff and measure produce
// separate highlight rectangles — the coalescing logic must not fill the
// gap between them.
TEST(HighlightRectsTest, DisjointSpanProducesSeparateRects) {
  Fixture        fixture(2);  // 2 measures
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const Voice voice1 = *Voice::create(1);
  const auto  r1     = Rational::create(1, 8);
  const auto  r2     = Rational::create(3, 8);
  const auto  r3     = Rational::create(1, 2);
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  ASSERT_TRUE(r3.has_value());

  // [0, ⅛) and [⅜, ½): a disjoint pair with a ¼ gap on the same measure.
  std::vector<ArbitraryRangeItem> items;
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{Rational(0), *r1}});
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{*r2, *r3}});
  const auto range_set = ArbitraryRangeSet::create(std::move(items));
  ASSERT_TRUE(range_set.has_value());
  const Selection sel = *range_set;

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(sel, fixture.project, layout);
  // Both intervals fall within measure 0.  They are disjoint (gap from ⅛
  // to ⅜) so must produce two separate rects.
  ASSERT_EQ(rects.size(), 2u);

  // Both rects should be on the same staff, non-zero width, and sorted
  // left to right.
  EXPECT_DOUBLE_EQ(rects[0].y, layout.systems[0].staves[0].bounds.y);
  EXPECT_DOUBLE_EQ(rects[1].y, layout.systems[0].staves[0].bounds.y);
  EXPECT_GT(rects[0].width, 0.0);
  EXPECT_GT(rects[1].width, 0.0);
  EXPECT_LT(rects[0].x + rects[0].width, rects[1].x)
      << "disjoint rects must not overlap";
}

TEST(HighlightRectsTest, ItemFromDifferentNodeIsRejected) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Build a range set where the item's node does not match the layout's
  // node. The highlight projection must reject it.
  const Voice  voice1     = *Voice::create(1);
  const NodeId other_node = fixture.project.add_node("Other");
  std::vector<ArbitraryRangeItem> items;
  items.push_back(ArbitraryRangeItem{other_node, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{Rational(0), Rational(1)}});
  const auto range_set = ArbitraryRangeSet::create(std::move(items));
  ASSERT_TRUE(range_set.has_value());

  EXPECT_TRUE(
      build_range_highlight_rects(*range_set, fixture.project, layout).empty());
}

// Two voices on the same staff, both selected in a single range drag,
// produce duplicate items in the ArbitraryRangeSet (one per voice).  The
// highlight projection must de-duplicate by (system, staff, measure) so
// the translucent highlight overlay is drawn once, not stacked twice.
TEST(HighlightRectsTest, TwoVoiceSameStaffProducesOneRect) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);

  // Fill both voices so the range resolver includes both.
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(
        fixture.voice(1)
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
    ASSERT_TRUE(
        fixture.voice(2)
            .append(make_note(*SpelledPitch::create(Letter::kE, 4), quarter))
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
  // Both voices appear in the range set.
  ASSERT_EQ(set->items().size(), 2u);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  // One rect per (system, staff, measure) — de-duplicated even though
  // two voices produced two items spanning the exact same measure.
  ASSERT_EQ(rects.size(), 1u);
  EXPECT_DOUBLE_EQ(rects[0].y, layout.systems[0].staves[0].bounds.y);
  EXPECT_DOUBLE_EQ(rects[0].height, layout.systems[0].staves[0].bounds.height);
  EXPECT_GT(rects[0].width, 0.0);
}

// Multi-track with mixed spans: two tracks produce rects on their respective
// staves, and the per-staff de-duplication does not collapse across tracks.
TEST(HighlightRectsTest, MultiTrackMixedSpanPreservesPerStaffDeDuplication) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t track = 0; track < 2; ++track) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(
          fixture.voice(1, track)
              .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
              .ok());
      ASSERT_TRUE(
          fixture.voice(2, track)
              .append(make_note(*SpelledPitch::create(Letter::kE, 4), quarter))
              .ok());
    }
  }

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
  // 2 voices × 2 tracks = 4 items in the range set.
  ASSERT_EQ(set->items().size(), 4u);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  // One rect per staff (each staff gets its own, de-duplicated across its
  // two voices).  2 staves × 1 measure = 2 rects.
  ASSERT_EQ(rects.size(), 2u);

  // The two rects belong to different staves.
  std::vector<double> y_positions = {rects[0].y, rects[1].y};
  std::ranges::sort(y_positions);
  EXPECT_DOUBLE_EQ(y_positions[0], layout.systems[0].staves[0].bounds.y);
  EXPECT_DOUBLE_EQ(y_positions[1], layout.systems[0].staves[1].bounds.y);
}
}  // namespace
