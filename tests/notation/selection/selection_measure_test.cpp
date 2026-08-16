// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- resolve_measure_selection_at: HitRole::kStaffMeasure -> FullMeasureSet
// ----

TEST(MeasureSelectionTest, BlankMeasureClickSelectsOneFullMeasure) {
  Fixture fixture(1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto selection =
      resolve_measure_selection_at(fixture.project, layout, point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const FullMeasureItem& item = set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.measure_index, 0u);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureSelectionTest,
     MeasureAlignedDragSelectsContiguousCompleteMeasures) {
  Fixture              fixture(3);
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto selection = resolve_measure_range_selection(
      fixture.project, layout, staff_center(layout, 0, 2),
      staff_center(layout, 0, 0));
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().measure_index, 0u);
  EXPECT_EQ(set->items().front().measure_count, 3u);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureSelectionTest,
     GrandStaffClicksNameTheSameMeasureOnDifferentStaves) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint upper_point = staff_center(layout, 0, 0);
  const NotationPoint lower_point = staff_center(layout, 1, 0);

  const auto upper_selection =
      resolve_measure_selection_at(fixture.project, layout, upper_point);
  const auto lower_selection =
      resolve_measure_selection_at(fixture.project, layout, lower_point);
  ASSERT_TRUE(upper_selection.has_value());
  ASSERT_TRUE(lower_selection.has_value());
  const auto* upper_set = std::get_if<FullMeasureSet>(&*upper_selection);
  const auto* lower_set = std::get_if<FullMeasureSet>(&*lower_selection);
  ASSERT_NE(upper_set, nullptr);
  ASSERT_NE(lower_set, nullptr);
  ASSERT_EQ(upper_set->items().size(), 1u);
  ASSERT_EQ(lower_set->items().size(), 1u);
  const FullMeasureItem& upper_item = upper_set->items().front();
  const FullMeasureItem& lower_item = lower_set->items().front();
  EXPECT_EQ(upper_item.track, lower_item.track);
  EXPECT_EQ(upper_item.measure_index, lower_item.measure_index);
  EXPECT_NE(upper_item.stave, lower_item.stave);
  EXPECT_TRUE(validate_selection(fixture.project, *upper_selection).empty());
  EXPECT_TRUE(validate_selection(fixture.project, *lower_selection).empty());
}

TEST(MeasureSelectionTest, MultiTrackClickNamesTheClickedTrackNotTheFirst) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff()},
                  1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 1, 0);

  const auto selection =
      resolve_measure_selection_at(fixture.project, layout, point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().track, fixture.track_ids[1]);
  EXPECT_NE(set->items().front().track, fixture.track_ids[0]);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureSelectionTest,
     SecondSystemClickNamesTheGlobalMeasureOrdinalNotASystemLocalIndex) {
  Fixture        fixture(3);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 12; ++index) {
    ASSERT_TRUE(fixture.voice().append(make_rest(quarter)).ok());
  }

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_GE(layout.systems.size(), 2u);
  const auto& second_system = layout.systems[1];
  ASSERT_FALSE(second_system.measures.empty());
  const std::size_t expected_ordinal = second_system.measures[0].ordinal;
  // A system-local index for the first measure of a non-first system would
  // be 0; the global ordinal must not be.
  ASSERT_GT(expected_ordinal, 0u);

  const auto&         staff = second_system.staves[0];
  const NotationPoint point{second_system.measures[0].bounds.x +
                                second_system.measures[0].bounds.width * 0.5,
                            staff.bounds.y + staff.bounds.height * 0.5};

  const auto selection =
      resolve_measure_selection_at(fixture.project, layout, point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().measure_index, expected_ordinal);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureSelectionTest, ClickingANoteheadYieldsNoMeasureSelection) {
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(pitch, *Duration::create(NoteValue::kQuarter, 0)))
          .ok());
  const Note note = std::get<Note>(fixture.voice().events().back());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kNotehead);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, ClickingARestYieldsNoMeasureSelection) {
  Fixture    fixture(1);
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(rest).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = rest_origin(layout, rest);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, ClickingAStemYieldsNoMeasureSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = stem_click_point(layout, note.id);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest,
     ClickingAStemlessChordsNoteheadColumnYieldsNoMeasureSelection) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      notehead_gap_point(layout, notes[0].id, notes[1].id);
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, PointOutsideEverySystemYieldsNoMeasureSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{-10'000.0, -10'000.0};

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, NonFinitePointYieldsNoMeasureSelection) {
  Fixture fixture(1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{std::numeric_limits<double>::quiet_NaN(), 0.0};

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

// layout_notation only ever emits a staff-measure region for a measure
// inside the node's own NodeTimeline main region (system.measures, built
// from timeline->measures(), which is exactly the main region --
// measure_map.hpp); it never lays out the pickdown at all. So a click past
// the last drawn measure's own right edge -- spatially where the
// pickdown's own material would sit -- never names a HitRole::kStaffMeasure
// region in the first place, and resolve_measure_selection_at rejects it
// through that same role check every other non-staff-measure hit is
// rejected through, before validate_selection is even consulted.
//
// This is as close as this resolver can come to exercising
// validate_full_measure_set's own TimelineRegion::kPickdown check
// (src/domain/selection.cpp): that check can only fire for a
// FullMeasureItem::measure_index that is simultaneously < measure_count()
// (so it passes the preceding range check) and whose own measure_start is
// >= boundary_position() -- and measure_start is strictly increasing while
// bounded above by boundary_position() for every ordinal < measure_count(),
// so no such ordinal exists. The domain's own test coverage
// (SelectionTest.ValidateFullMeasureRejectsPickdownOrdinal,
// tests/domain/selection_test.cpp) hits the identical situation and
// likewise only ever observes kMeasureIndexOutOfRange, never
// kMeasureIndexInPickdown, corroborating that the pickdown-specific branch
// is unreachable from any project state today -- a pre-existing domain
// property, not something this resolver can be exercised against.
TEST(MeasureSelectionTest,
     ClickPastTheLastDrawnMeasureWherePickdownMaterialWouldSitYieldsNothing) {
  Project    project{ProjectId::generate(), "Pickdown"};
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  const StaveId stave_id =
      project.find_active_track(*track_id)->layout().staves()[0].id;
  const NodeId node_id = project.add_node("Node");

  std::vector<Measure> measures{measure()};
  auto                 timeline = NodeTimeline::create(
      measures, {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  const_cast<Node*>(project.find_node(node_id))
      ->set_timeline(std::move(*timeline));
  const_cast<TrackLane*>(project.find_node(node_id)->lane(*track_id))
      ->ensure_stave(stave_id);

  const FixedMetrics   metrics;
  const NotationLayout layout =
      require_layout(layout_notation(project, node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  ASSERT_EQ(layout.systems[0].measures.size(), 1u);
  const auto& staff = layout.systems[0].staves[0];
  ASSERT_EQ(staff.measure_bounds.size(), 1u);

  const NotationPoint point{
      staff.measure_bounds[0].x + staff.measure_bounds[0].width + 5.0,
      staff.bounds.y + staff.bounds.height * 0.5};
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NE(hit->role, HitRole::kStaffMeasure);

  EXPECT_FALSE(
      resolve_measure_selection_at(project, layout, point).has_value());
}

// resolve_staff_at's own generous ledger/marking lane (6 staff-spaces above
// or below a staff's own five lines) is what preview_note_entry and
// resolve_selection_at's insertion-caret arm use to attribute an
// off-stave click to the nearest staff. hit_test's own per-region bounds
// carry no such padding for the two staff-tight container roles:
// HitRole::kStaffMeasure's own bounds are exactly staff.measure_bounds'
// tight extent, and HitRole::kStaff's are staff.bounds' -- neither reaches
// into the ledger lane. kMeasure and kSystem are not staff-tight, though:
// both are built from the full system height (see layout_internal), so the
// ledger lane sits inside them, and a point there resolves to kMeasure (it
// outranks kSystem). That is still never a HitRole::kStaffMeasure region --
// resolve_measure_selection_at deliberately does not fall back to the
// nearest staff the way the insertion-caret path does: naming a whole
// measure is a more deliberate act than placing a note, and a point this
// far outside a staff's own drawn region should not silently select a
// measure on a staff the composer did not visibly click.
TEST(MeasureSelectionTest,
     LedgerLineLaneAboveTheStaffAttributesToTheStaffButNamesNoMeasure) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto&         staff = layout.systems[0].staves[0];
  const double        space = staff.bounds.height / 4.0;
  const double        x     = layout.systems[0].measures[0].bounds.x + 10.0;
  const NotationPoint point{x, staff.bounds.y - space * 2.0};

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->stave_id, fixture.stave_id());

  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NE(hit->role, HitRole::kStaffMeasure);
  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

// The staff-measure rank must win by rank, not by hit_test's smaller-area or
// semantic_id tie-break, on a real layout the ordinary engraver produces --
// not on hand-built regions. A single-measure system is the case where the
// tie-break would otherwise decide: there, a staff's one staff-measure
// region and its containing kVoice region share exactly the same bounds
// (staff.bounds), so an equal or lower rank would fall through to the
// smaller-area comparison (a tie, since the areas are equal) and then to
// the ascending semantic_id comparison, which happens to still pick the
// staff-measure region today only because "staff-measure" sorts before
// "voice" lexically. This test bypasses both tie-breaks entirely by
// asserting the rank comparison directly: EXPECT_GT fails outright at
// kHitPriorityStaffMeasure == kHitPriorityVoice (3), regardless of area or
// id, so it fails specifically when the rank stops being its own.
TEST(MeasureSelectionTest,
     StaffMeasureRegionOutranksVoiceRegionOnTheSameStaffBySeparateRank) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  const auto& system = layout.systems[0];
  ASSERT_EQ(system.measures.size(), 1u);
  ASSERT_EQ(system.staves.size(), 2u);

  for (const auto& staff : system.staves) {
    ASSERT_EQ(staff.measure_bounds.size(), 1u);
    ASSERT_FALSE(staff.voices.empty());

    const HitRegion* staff_measure_region = nullptr;
    for (const HitRegion& region : layout.hit_regions) {
      if (region.role == HitRole::kStaffMeasure &&
          region.bounds == staff.measure_bounds[0]) {
        staff_measure_region = &region;
        break;
      }
    }
    ASSERT_NE(staff_measure_region, nullptr);

    const HitRegion* voice_region = nullptr;
    for (const HitRegion& region : layout.hit_regions) {
      if (region.role == HitRole::kVoice &&
          region.semantic_id.value == staff.voices[0].id.value) {
        voice_region = &region;
        break;
      }
    }
    ASSERT_NE(voice_region, nullptr);

    // The precondition the tie-break case depends on: equal bounds, so an
    // equal rank would indeed reach hit_test's tie-break rather than being
    // decided by area.
    ASSERT_EQ(staff_measure_region->bounds, voice_region->bounds);
    EXPECT_GT(staff_measure_region->priority, voice_region->priority);
  }
}

// Isolation test: exercises hit_test's own priority/tie-break mechanics
// directly on two hand-built regions of exactly equal bounds and area, with
// hand-chosen ids -- it never touches the production ladder
// (kHitPriorityStaffMeasure et al.) or a real engraver-produced layout, so
// it does not by itself establish that any particular numeric rank is
// necessary there. What it does establish is the "loses to every engraved
// object" direction of the ladder: a region ranked strictly below another
// always loses the hit, regardless of area or id, while two regions of
// equal rank and area fall through to the id tie-break, where the outcome
// depends on how the ids happen to be spelled -- exactly the dependence
// the header comment on HitRegion::priority (staff-measure rank) explains
// giving the staff-measure region a rank of its own removes. The rank's
// own necessity on a real layout is established separately, by the
// production-layout test above and by the pre-existing sweep at
// TheNoteheadColumnRanksAboveEveryContainerAndBelowEveryEngravedObject.
TEST(MeasureSelectionTest,
     HitTestsPriorityTieBreakIsolatedFromProductionLadder) {
  const NotationRect shared_bounds{0.0, 0.0, 4.0, 4.0};

  // Production ladder: staff-measure (4) strictly below notehead (8).
  // Equal-area, overlapping regions -- the notehead still wins outright,
  // regardless of area or id, because its rank is strictly higher.
  NotationLayout correct;
  correct.hit_regions = {
      HitRegion{NotationId{"sm/hit"}, NotationId{"sm"}, HitRole::kStaffMeasure,
                shared_bounds, 4, std::nullopt, std::nullopt},
      HitRegion{NotationId{"note/notehead/hit"}, NotationId{"note"},
                HitRole::kNotehead, shared_bounds, 8, std::nullopt,
                std::nullopt},
  };
  const auto correct_hit = correct.hit_test(NotationPoint{2.0, 2.0});
  ASSERT_TRUE(correct_hit.has_value());
  EXPECT_EQ(correct_hit->role, HitRole::kNotehead);

  // Misranked: give the staff-measure region the notehead's own rank
  // instead (the cheaper "let the area/id tie-break sort it out"
  // alternative HitRegion::priority's own comment rules out). With equal
  // priority and equal area, hit_test falls to the semantic_id tie-break,
  // ascending -- and this pair of ids is chosen so the staff-measure
  // region's own semantic_id sorts first ("a-staff-measure" <
  // "b-notehead"). The staff-measure region now wins: exactly the wrong
  // click outcome resolve_measure_selection_at depends on the production
  // rank (strictly below every engraved-object rank) making impossible.
  NotationLayout misranked;
  misranked.hit_regions = {
      HitRegion{NotationId{"a-staff-measure/hit"},
                NotationId{"a-staff-measure"}, HitRole::kStaffMeasure,
                shared_bounds, 8, std::nullopt, std::nullopt},
      HitRegion{NotationId{"b-notehead/hit"}, NotationId{"b-notehead"},
                HitRole::kNotehead, shared_bounds, 8, std::nullopt,
                std::nullopt},
  };
  const auto misranked_hit = misranked.hit_test(NotationPoint{2.0, 2.0});
  ASSERT_TRUE(misranked_hit.has_value());
  EXPECT_EQ(misranked_hit->role, HitRole::kStaffMeasure);
}
}  // namespace
