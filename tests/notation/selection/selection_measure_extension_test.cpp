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

// ---- extend_measure_selection: aligned FullMeasureSet extension across
//      additional chosen tracks/staves ----

TEST(MeasureExtensionTest, CrossTrackExtensionPreservesAnchorNodeAndMeasure) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const auto    track0 = fixture.track_ids[0];
  const auto    track1 = fixture.track_ids[1];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId stave1 = fixture.stave_id(1);

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, stave0, 0}});
  ASSERT_TRUE(existing_set.has_value());

  const auto selection = extend_measure_selection(
      fixture.project, *existing_set, {MeasureScope{track1, stave1}});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);

  // Both items share the same anchor node and measure_index.
  for (const FullMeasureItem& item : set->items()) {
    EXPECT_EQ(item.node, fixture.node_id);
    EXPECT_EQ(item.measure_index, 0u);
  }

  // Track 0's stave is before track 1's stave in score order.
  EXPECT_EQ(set->items()[0].track, track0);
  EXPECT_EQ(set->items()[0].stave, stave0);
  EXPECT_EQ(set->items()[1].track, track1);
  EXPECT_EQ(set->items()[1].stave, stave1);

  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest, OneGrandStaffStaveToTheOtherWithinTheSameTrack) {
  Fixture       fixture({StaffLayout::grand_staff()}, 1);
  const auto    track = fixture.track_ids[0];
  const StaveId upper = fixture.stave_id(0, 0);
  const StaveId lower = fixture.stave_id(0, 1);

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, upper, 0}});
  ASSERT_TRUE(existing_set.has_value());

  const auto selection = extend_measure_selection(
      fixture.project, *existing_set, {MeasureScope{track, lower}});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);

  EXPECT_EQ(set->items()[0].track, track);
  EXPECT_EQ(set->items()[0].stave, upper);
  EXPECT_EQ(set->items()[1].track, track);
  EXPECT_EQ(set->items()[1].stave, lower);

  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest,
     ExplicitMixedSubsetAcrossTracksAndStavesSelectsOnlyTheNamedOnes) {
  // Three tracks: track 0 (single), track 1 (grand staff), track 2 (single).
  // Existing: track 0's stave.  Additional: only track 1's lower staff.
  // Track 1's upper staff and track 2's entire staff are neither in existing
  // nor in additional -- they must not appear in the result.
  Fixture fixture(
      {StaffLayout::single_staff(Clef::kTreble), StaffLayout::grand_staff(),
       StaffLayout::single_staff(Clef::kTreble)},
      1);
  const auto    track0 = fixture.track_ids[0];
  const auto    track1 = fixture.track_ids[1];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId lower  = fixture.stave_id(1, 1);

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, stave0, 0}});
  ASSERT_TRUE(existing_set.has_value());

  const auto selection = extend_measure_selection(
      fixture.project, *existing_set, {MeasureScope{track1, lower}});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);

  EXPECT_EQ(set->items()[0].track, track0);
  EXPECT_EQ(set->items()[0].stave, stave0);
  EXPECT_EQ(set->items()[1].track, track1);
  EXPECT_EQ(set->items()[1].stave, lower);

  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest, DuplicateAdditionIsIdempotent) {
  Fixture       fixture({StaffLayout::grand_staff()}, 1);
  const auto    track = fixture.track_ids[0];
  const StaveId upper = fixture.stave_id(0, 0);
  const StaveId lower = fixture.stave_id(0, 1);

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, upper, 0}});
  ASSERT_TRUE(existing_set.has_value());

  // Add the lower stave twice -- idempotent, still only one lower item.
  const auto selection = extend_measure_selection(
      fixture.project, *existing_set,
      {MeasureScope{track, lower}, MeasureScope{track, lower}});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  EXPECT_EQ(set->items()[0].stave, upper);
  EXPECT_EQ(set->items()[1].stave, lower);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest, ExistingDuplicateInAdditionalLeavesSetUnchanged) {
  Fixture       fixture({StaffLayout::grand_staff()}, 1);
  const auto    track = fixture.track_ids[0];
  const StaveId upper = fixture.stave_id(0, 0);

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, upper, 0}});
  ASSERT_TRUE(existing_set.has_value());

  // Adding a scope already in existing is idempotent and does not fail.
  const auto selection = extend_measure_selection(
      fixture.project, *existing_set, {MeasureScope{track, upper}});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].stave, upper);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest,
     CallerInputOrderDoesNotAffectDeterministicScoreOrder) {
  // Four tracks: tracks 0, 1, 2, 3. The existing set covers track 1.
  // Additional scopes are given in reverse score order (track 3 then track 0
  // then track 2).  The result must be sorted by score order: 0, 1, 2, 3.
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kTreble)},
                        1);
  const auto    t0 = fixture.track_ids[0];
  const auto    t1 = fixture.track_ids[1];
  const auto    t2 = fixture.track_ids[2];
  const auto    t3 = fixture.track_ids[3];
  const StaveId s0 = fixture.stave_id(0);
  const StaveId s1 = fixture.stave_id(1);
  const StaveId s2 = fixture.stave_id(2);
  const StaveId s3 = fixture.stave_id(3);

  const auto existing_set =
      FullMeasureSet::create({FullMeasureItem{fixture.node_id, t1, s1, 0}});
  ASSERT_TRUE(existing_set.has_value());

  // Additional given in reverse score order: t3, t0, t2.
  const auto selection = extend_measure_selection(
      fixture.project, *existing_set,
      {MeasureScope{t3, s3}, MeasureScope{t0, s0}, MeasureScope{t2, s2}});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 4u);

  EXPECT_EQ(set->items()[0].track, t0);
  EXPECT_EQ(set->items()[0].stave, s0);
  EXPECT_EQ(set->items()[1].track, t1);
  EXPECT_EQ(set->items()[1].stave, s1);
  EXPECT_EQ(set->items()[2].track, t2);
  EXPECT_EQ(set->items()[2].stave, s2);
  EXPECT_EQ(set->items()[3].track, t3);
  EXPECT_EQ(set->items()[3].stave, s3);

  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest,
     GrandStaffScoreOrderWithinATrackIsStavesOrderNotCallerOrder) {
  Fixture       fixture({StaffLayout::grand_staff()}, 1);
  const auto    track = fixture.track_ids[0];
  const StaveId upper = fixture.stave_id(0, 0);  // treble first
  const StaveId lower = fixture.stave_id(0, 1);  // bass second

  // Existing: lower stave.  Additional: upper stave (caller lists upper
  // first).  Result must be upper then lower (staves() order).
  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, lower, 0}});
  ASSERT_TRUE(existing_set.has_value());

  const auto selection = extend_measure_selection(
      fixture.project, *existing_set, {MeasureScope{track, upper}});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  EXPECT_EQ(set->items()[0].stave, upper);
  EXPECT_EQ(set->items()[1].stave, lower);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest, EmptyAdditionalReturnsExistingSetValidated) {
  Fixture       fixture(1);
  const auto    track = fixture.track_ids[0];
  const StaveId stave = fixture.stave_id();

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, stave, 0}});
  ASSERT_TRUE(existing_set.has_value());

  const auto selection =
      extend_measure_selection(fixture.project, *existing_set, {});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].track, track);
  EXPECT_EQ(set->items()[0].stave, stave);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest,
     SecondSystemPreservesGlobalOrdinalNotASystemLocalIndex) {
  Fixture       fixture(3);
  const auto    track        = fixture.track_ids[0];
  const StaveId stave        = fixture.stave_id();
  const auto    existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, stave, 2}});
  ASSERT_TRUE(existing_set.has_value());

  const auto selection =
      extend_measure_selection(fixture.project, *existing_set, {});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items()[0].measure_index, 2u);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureExtensionTest,
     ExtensionStartingFromResolveMeasureSelectionAtPreservesOrdinal) {
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

  // Use resolve_measure_selection_at on the second system's first measure.
  const auto&         second_system = layout.systems[1];
  const NotationPoint point{second_system.measures[0].bounds.x +
                                second_system.measures[0].bounds.width * 0.5,
                            second_system.staves[0].bounds.y +
                                second_system.staves[0].bounds.height * 0.5};
  const auto          existing =
      resolve_measure_selection_at(fixture.project, layout, point);
  ASSERT_TRUE(existing.has_value());
  const auto* existing_set = std::get_if<FullMeasureSet>(&*existing);
  ASSERT_NE(existing_set, nullptr);
  const std::size_t global_ordinal =
      existing_set->items().front().measure_index;
  ASSERT_GT(global_ordinal, 0u);

  // Extend with empty additional -- the global ordinal is preserved.
  const auto extended =
      extend_measure_selection(fixture.project, *existing_set, {});
  ASSERT_TRUE(extended.has_value());
  const auto* ext_set = std::get_if<FullMeasureSet>(&*extended);
  ASSERT_NE(ext_set, nullptr);
  ASSERT_EQ(ext_set->items().size(), 1u);
  EXPECT_EQ(ext_set->items().front().measure_index, global_ordinal);
  EXPECT_TRUE(validate_selection(fixture.project, *extended).empty());
}

TEST(MeasureExtensionTest, MisalignedExistingNodeIsRejected) {
  Fixture       fixture(1);
  const auto    track      = fixture.track_ids[0];
  const StaveId stave      = fixture.stave_id();
  const NodeId  other_node = fixture.project.add_node("Other");

  const auto misaligned =
      FullMeasureSet::create({FullMeasureItem{fixture.node_id, track, stave, 0},
                              FullMeasureItem{other_node, track, stave, 0}});
  ASSERT_TRUE(misaligned.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *misaligned, {}).has_value());
}

TEST(MeasureExtensionTest, MisalignedExistingMeasureIndexIsRejected) {
  Fixture       fixture(2);
  const auto    track = fixture.track_ids[0];
  const StaveId stave = fixture.stave_id();

  const auto misaligned = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, stave, 0},
       FullMeasureItem{fixture.node_id, track, stave, 1}});
  ASSERT_TRUE(misaligned.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *misaligned, {}).has_value());
}

TEST(MeasureExtensionTest,
     ExistingItemWithArchivedTrackIsRejectedEvenWithAnotherValidItem) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const auto    track0 = fixture.track_ids[0];
  const auto    track1 = fixture.track_ids[1];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId stave1 = fixture.stave_id(1);

  // Archive track 1 so it is neither active nor unknown.
  ASSERT_TRUE(fixture.project.archive_track(track1).ok());

  // Mixed set: one valid existing item (track 0) and one invalid (archived
  // track 1).  Must fail — not silently drop the archived item.
  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, stave0, 0},
       FullMeasureItem{fixture.node_id, track1, stave1, 0}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set, {}).has_value());
}

TEST(MeasureExtensionTest,
     ExistingItemWithUnknownTrackIsRejectedEvenWithAnotherValidItem) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const auto    track0  = fixture.track_ids[0];
  const StaveId stave0  = fixture.stave_id(0);
  const TrackId unknown = TrackId::generate();

  // Mixed set: one valid and one unknown — must fail.
  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, stave0, 0},
       FullMeasureItem{fixture.node_id, unknown, stave0, 0}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set, {}).has_value());
}

TEST(MeasureExtensionTest,
     ExistingItemWithWrongTrackStaveIsRejectedEvenWithAnotherValidItem) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const auto    track0 = fixture.track_ids[0];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId stave1 = fixture.stave_id(1);

  // stave1 belongs to track 1's layout, but the item pairs it with track 0.
  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, stave0, 0},
       FullMeasureItem{fixture.node_id, track0, stave1, 0}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set, {}).has_value());
}

TEST(
    MeasureExtensionTest,
    ExistingItemWithMissingLaneInAnchorNodeIsRejectedEvenWithAnotherValidItem) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const auto    track0 = fixture.track_ids[0];
  const auto    track1 = fixture.track_ids[1];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId stave1 = fixture.stave_id(1);

  // Create a separate anchor node that has no lane for track 1.
  const NodeId                 bare_node = fixture.project.add_node("Bare");
  std::vector<Measure>         measures(1, measure());
  std::vector<StaveDefinition> node_staves{
      fixture.project.find_active_track(track0)->layout().staves()[0],
      fixture.project.find_active_track(track1)->layout().staves()[0]};
  auto timeline = NodeTimeline::create(std::move(measures), node_staves);
  ASSERT_TRUE(timeline.has_value());
  fixture.project.find_node(bare_node)->set_timeline(std::move(*timeline));
  Node*      node  = fixture.project.find_node(bare_node);
  TrackLane* lane0 = node->lane(track0);
  if (lane0 == nullptr) {
    node->ensure_lane(track0);
    lane0 = node->lane(track0);
  }
  ASSERT_NE(lane0, nullptr);
  lane0->ensure_stave(stave0);
  // Do NOT create a lane for track 1 at all.

  // Mixed set: track 0's lane exists, track 1's does not — must fail.
  const auto existing_set =
      FullMeasureSet::create({FullMeasureItem{bare_node, track0, stave0, 0},
                              FullMeasureItem{bare_node, track1, stave1, 0}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set, {}).has_value());
}

TEST(MeasureExtensionTest,
     ExistingItemWithMissingStaveInLaneIsRejectedEvenWithAnotherValidItem) {
  Fixture       fixture({StaffLayout::grand_staff()}, 1);
  const auto    track = fixture.track_ids[0];
  const StaveId upper = fixture.stave_id(0, 0);
  const StaveId lower = fixture.stave_id(0, 1);

  // Manually create a node that has a lane for the track but only the
  // upper stave populated — the lower stave is missing from the lane.
  const NodeId                 node_id = fixture.project.add_node("Partial");
  std::vector<Measure>         measures(1, measure());
  std::vector<StaveDefinition> node_staves{
      fixture.project.find_active_track(track)->layout().staves()[0],
      fixture.project.find_active_track(track)->layout().staves()[1]};
  auto timeline = NodeTimeline::create(std::move(measures), node_staves);
  ASSERT_TRUE(timeline.has_value());
  fixture.project.find_node(node_id)->set_timeline(std::move(*timeline));
  Node* node = fixture.project.find_node(node_id);
  node->ensure_lane(track);
  TrackLane* lane = node->lane(track);
  ASSERT_NE(lane, nullptr);
  lane->ensure_stave(upper);
  ASSERT_TRUE(lane->has_stave(upper));
  ASSERT_FALSE(lane->has_stave(lower));

  // Mixed set: upper is valid, lower has no StaveVoices in the lane.
  const auto existing_set =
      FullMeasureSet::create({FullMeasureItem{node_id, track, upper, 0},
                              FullMeasureItem{node_id, track, lower, 0}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set, {}).has_value());
}

TEST(MeasureExtensionTest, UnknownTrackIsRejected) {
  Fixture       fixture(1);
  const auto    track = fixture.track_ids[0];
  const StaveId stave = fixture.stave_id();

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, stave, 0}});
  ASSERT_TRUE(existing_set.has_value());

  const TrackId unknown{TrackId::generate()};
  EXPECT_FALSE(extend_measure_selection(fixture.project, *existing_set,
                                        {MeasureScope{unknown, stave}})
                   .has_value());
}

TEST(MeasureExtensionTest, ArchivedTrackIsRejected) {
  // Archive a track after creating it, then try to extend onto it.
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const auto    track0         = fixture.track_ids[0];
  const StaveId stave0         = fixture.stave_id(0);
  const auto    archived       = fixture.track_ids[1];
  const StaveId archived_stave = fixture.stave_id(1);

  ASSERT_TRUE(fixture.project.archive_track(archived).ok());

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, stave0, 0}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set,
                               {MeasureScope{archived, archived_stave}})
          .has_value());
}

TEST(MeasureExtensionTest, StaveFromWrongTrackLayoutIsRejected) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kTreble),
                         StaffLayout::single_staff(Clef::kBass)},
                        1);
  const auto    track0 = fixture.track_ids[0];
  const StaveId stave0 = fixture.stave_id(0);
  const StaveId stave1 = fixture.stave_id(1);

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track0, stave0, 0}});
  ASSERT_TRUE(existing_set.has_value());

  // stave1 belongs to track 1's layout, not track 0's.
  EXPECT_FALSE(extend_measure_selection(fixture.project, *existing_set,
                                        {MeasureScope{track0, stave1}})
                   .has_value());
}

TEST(MeasureExtensionTest, MissingLaneInAnchorNodeIsRejected) {
  Fixture       fixture({StaffLayout::single_staff()}, 1);
  const auto    track = fixture.track_ids[0];
  const StaveId stave = fixture.stave_id();

  // Create a second node with NO lane for this track at all, then try to
  // extend onto the original node's track/stave but from the bare node.
  const NodeId bare_node = fixture.project.add_node("Bare");
  const auto   existing_set =
      FullMeasureSet::create({FullMeasureItem{bare_node, track, stave, 0}});
  // Creating it succeeds structurally (FullMeasureSet does not check lanes),
  // but extending it fails because the bare node has no lane for this track.
  ASSERT_TRUE(existing_set.has_value());
  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set, {}).has_value());
}

TEST(MeasureExtensionTest, MissingStaveInAnchorNodeLaneIsRejected) {
  // Manually construct a node that has a lane for a grand-staff track but
  // only one of its two staves populated -- the lane exists, but the other
  // stave has no StaveVoices entry at all.
  Project    project{ProjectId::generate(), "Extend"};
  const auto track_id = project.add_track("Track", StaffLayout::grand_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  const auto&   layout = project.find_active_track(*track_id)->layout();
  const StaveId upper  = layout.staves()[0].id;
  const StaveId lower  = layout.staves()[1].id;

  const NodeId                 node_id = project.add_node("Node");
  std::vector<Measure>         measures(2, measure());
  std::vector<StaveDefinition> node_staves;
  for (const auto& sd : layout.staves()) {
    node_staves.push_back(sd);
  }
  auto timeline = NodeTimeline::create(std::move(measures), node_staves);
  ASSERT_TRUE(timeline.has_value());
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  // Create a lane for the track but only ensure the upper stave -- the
  // lower stave is not populated in the lane.
  Node* node = project.find_node(node_id);
  node->ensure_lane(*track_id);
  TrackLane* lane = node->lane(*track_id);
  ASSERT_NE(lane, nullptr);
  lane->ensure_stave(upper);
  ASSERT_TRUE(lane->has_stave(upper));
  ASSERT_FALSE(lane->has_stave(lower));

  const auto existing_set =
      FullMeasureSet::create({FullMeasureItem{node_id, *track_id, upper, 0}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(extend_measure_selection(project, *existing_set,
                                        {MeasureScope{*track_id, lower}})
                   .has_value());
}

TEST(MeasureExtensionTest,
     ValidateSelectionRejectsResultWithBogusMeasureIndex) {
  // Construct an existing set with a measure index out of range for the
  // node's timeline.  FullMeasureSet::create accepts it (it only checks
  // non-empty and distinct), but validate_selection rejects it at
  // extend_measure_selection's post-construction validation gate.
  Fixture       fixture(1);
  const auto    track = fixture.track_ids[0];
  const StaveId stave = fixture.stave_id();

  // The fixture's node has exactly 1 measure, so index 99 is out of range.
  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, stave, 99}});
  ASSERT_TRUE(existing_set.has_value());

  EXPECT_FALSE(
      extend_measure_selection(fixture.project, *existing_set, {}).has_value());
}

TEST(MeasureExtensionTest,
     MultiItemExistingSetPreservesAllItemsWhenAdditionalIsEmpty) {
  Fixture       fixture({StaffLayout::grand_staff()}, 1);
  const auto    track = fixture.track_ids[0];
  const StaveId upper = fixture.stave_id(0, 0);
  const StaveId lower = fixture.stave_id(0, 1);

  const auto existing_set = FullMeasureSet::create(
      {FullMeasureItem{fixture.node_id, track, upper, 0},
       FullMeasureItem{fixture.node_id, track, lower, 0}});
  ASSERT_TRUE(existing_set.has_value());

  const auto selection =
      extend_measure_selection(fixture.project, *existing_set, {});
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  EXPECT_EQ(set->items()[0].stave, upper);
  EXPECT_EQ(set->items()[1].stave, lower);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}
}  // namespace
