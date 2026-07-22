// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::StaffLayout;
using graphscore::TrackId;
using graphscore::TrackLane;

namespace {

Project make_project() {
  return Project(ProjectId::generate(), "Lane Alignment");
}

}  // namespace

TEST(LaneAlignmentTest, AddingTrackCreatesLaneInEveryExistingNode) {
  Project project = make_project();

  const NodeId node_a = project.add_node("A");
  const NodeId node_b = project.add_node("B");
  const NodeId node_c = project.add_node("C");

  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  for (const NodeId node_id : {node_a, node_b, node_c}) {
    const Node* node = project.find_node(node_id);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->has_lane(*track_id));
    EXPECT_EQ(node->lane_count(), 1u);
  }
}

TEST(LaneAlignmentTest, NewNodeAlignsWithExistingActiveTracks) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(1));
  ASSERT_TRUE(track_id.has_value());

  const NodeId node_id = project.add_node("Later Node");
  const Node*  node    = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  EXPECT_TRUE(node->has_lane(*track_id));
}

TEST(LaneAlignmentTest, ArchiveRetainsLaneDataAcrossRestore) {
  Project project = make_project();

  const NodeId node_id = project.add_node("Node A");

  const auto track_id = project.add_track("Piano", StaffLayout::grand_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  Node* node = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  TrackLane* lane_before_archive = node->lane(*track_id);
  ASSERT_NE(lane_before_archive, nullptr);

  ASSERT_TRUE(project.archive_track(*track_id).ok());
  EXPECT_EQ(project.find_active_track(*track_id), nullptr);
  EXPECT_NE(project.find_archived_track(*track_id), nullptr);

  // The lane is the same object: archiving a track never touches per-node
  // lane storage, so whatever notation content a lane holds (starting in
  // Phase 4) is retained byte-for-byte while the track is archived.
  TrackLane* lane_while_archived = node->lane(*track_id);
  EXPECT_EQ(lane_while_archived, lane_before_archive);

  ASSERT_TRUE(project.restore_track(*track_id).ok());
  EXPECT_NE(project.find_active_track(*track_id), nullptr);

  TrackLane* lane_after_restore = node->lane(*track_id);
  EXPECT_EQ(lane_after_restore, lane_before_archive);
}

TEST(LaneAlignmentTest, RestoreBackfillsNodesCreatedWhileArchived) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(2));
  ASSERT_TRUE(track_id.has_value());

  ASSERT_TRUE(project.archive_track(*track_id).ok());

  const NodeId node_id             = project.add_node("Node During Archive");
  const Node*  node_before_restore = project.find_node(node_id);
  ASSERT_NE(node_before_restore, nullptr);
  EXPECT_FALSE(node_before_restore->has_lane(*track_id));

  ASSERT_TRUE(project.restore_track(*track_id).ok());
  const Node* node_after_restore = project.find_node(node_id);
  ASSERT_NE(node_after_restore, nullptr);
  EXPECT_TRUE(node_after_restore->has_lane(*track_id));
}

TEST(LaneAlignmentTest, RepresentativeSixtyFourTracksAcrossSeveralNodes) {
  Project project = make_project();

  constexpr int       kNodeCount = 6;
  std::vector<NodeId> node_ids;
  for (int i = 0; i < kNodeCount; ++i) {
    node_ids.push_back(project.add_node("Node " + std::to_string(i)));
  }

  std::vector<TrackId> track_ids;
  for (int i = 0; i < 64; ++i) {
    const auto channel = MidiChannel::create(static_cast<std::uint8_t>(i % 16));
    ASSERT_TRUE(channel.has_value());
    const auto track_id = project.add_track(
        "Track " + std::to_string(i), StaffLayout::single_staff(), *channel);
    ASSERT_TRUE(track_id.has_value());
    track_ids.push_back(*track_id);
  }

  EXPECT_EQ(project.active_tracks().size(), 64u);

  for (const NodeId node_id : node_ids) {
    const Node* node = project.find_node(node_id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->lane_count(), 64u);
    for (const TrackId track_id : track_ids) {
      EXPECT_TRUE(node->has_lane(track_id));
    }
  }

  const auto overflow = project.add_track(
      "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_FALSE(overflow.has_value());
  EXPECT_EQ(project.active_tracks().size(), 64u);
}
