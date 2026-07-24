// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ConnectorType;
using graphscore::Dynamic;
using graphscore::Graph;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NoteValue;
using graphscore::OutputConnector;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::RoutePoint;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::Tempo;
using graphscore::TrackId;
using graphscore::TrackLane;

namespace {

Project make_project() {
  return Project(ProjectId::generate(), "Test Project");
}

}  // namespace

TEST(ProjectTrackTest, AddTrackAssignsSequentialIndices) {
  Project    project = make_project();
  const auto first   = project.add_track("First", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  const auto second  = project.add_track("Second", StaffLayout::single_staff(),
                                         *MidiChannel::create(1));
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  EXPECT_EQ(project.find_active_track(*first)->index().value(), 0u);
  EXPECT_EQ(project.find_active_track(*second)->index().value(), 1u);
}

TEST(ProjectTrackTest, SixtyFourthTrackSucceedsSixtyFifthFails) {
  Project project = make_project();
  for (int i = 0; i < 64; ++i) {
    const auto id = project.add_track(
        "Track " + std::to_string(i), StaffLayout::single_staff(),
        *MidiChannel::create(static_cast<std::uint8_t>(i % 16)));
    ASSERT_TRUE(id.has_value()) << "track " << i;
  }
  EXPECT_EQ(project.active_tracks().size(), 64u);

  const auto overflow = project.add_track(
      "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_FALSE(overflow.has_value());
  EXPECT_EQ(project.active_tracks().size(), 64u);
}

TEST(ProjectTrackTest, ArchiveMovesTrackOutOfActiveSet) {
  Project    project = make_project();
  const auto id      = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  ASSERT_TRUE(id.has_value());

  EXPECT_TRUE(project.archive_track(*id).ok());
  EXPECT_EQ(project.find_active_track(*id), nullptr);
  ASSERT_NE(project.find_archived_track(*id), nullptr);
  EXPECT_EQ(project.find_archived_track(*id)->name(), "Track");
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);
}

TEST(ProjectTrackTest, ArchiveUnknownTrackFails) {
  Project project = make_project();
  EXPECT_FALSE(project.archive_track(TrackId::generate()).ok());
}

TEST(ProjectTrackTest, RestoreReturnsTrackToActiveSet) {
  Project    project = make_project();
  const auto id      = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  ASSERT_TRUE(id.has_value());
  ASSERT_TRUE(project.archive_track(*id).ok());

  EXPECT_TRUE(project.restore_track(*id).ok());
  EXPECT_NE(project.find_active_track(*id), nullptr);
  EXPECT_EQ(project.find_archived_track(*id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);
}

TEST(ProjectTrackTest, RestoreFailsWhenActiveSetIsFull) {
  Project    project     = make_project();
  const auto archived_id = project.add_track(
      "Archived", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(archived_id.has_value());
  ASSERT_TRUE(project.archive_track(*archived_id).ok());

  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }

  EXPECT_FALSE(project.restore_track(*archived_id).ok());
  EXPECT_NE(project.find_archived_track(*archived_id), nullptr);
}

TEST(ProjectTrackTest, RestoreUnknownTrackFails) {
  Project project = make_project();
  EXPECT_FALSE(project.restore_track(TrackId::generate()).ok());
}

TEST(ProjectTrackTest, AddTrackWithIdPreservesSuppliedIdAndAlignsLanes) {
  Project project = make_project();
  NodeId  node_a  = project.add_node("A");
  NodeId  node_b  = project.add_node("B");

  const TrackId id     = TrackId::generate();
  const Result  result = project.add_track_with_id(
      id, "Restored", StaffLayout::single_staff(), *MidiChannel::create(0));

  ASSERT_TRUE(result.ok());
  ASSERT_NE(project.find_active_track(id), nullptr);
  EXPECT_EQ(project.find_active_track(id)->name(), "Restored");
  EXPECT_TRUE(project.find_node(node_a)->has_lane(id));
  EXPECT_TRUE(project.find_node(node_b)->has_lane(id));
}

TEST(ProjectTrackTest, AddTrackWithIdRejectsDuplicateActiveId) {
  Project    project = make_project();
  const auto id = project.add_track("Existing", StaffLayout::single_staff(),
                                    *MidiChannel::create(0));
  ASSERT_TRUE(id.has_value());

  const Result result = project.add_track_with_id(
      *id, "Duplicate", StaffLayout::single_staff(), *MidiChannel::create(1));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(ProjectTrackTest, AddTrackWithIdRejectsDuplicateArchivedId) {
  Project    project = make_project();
  const auto id = project.add_track("Existing", StaffLayout::single_staff(),
                                    *MidiChannel::create(0));
  ASSERT_TRUE(id.has_value());
  ASSERT_TRUE(project.archive_track(*id).ok());

  const Result result = project.add_track_with_id(
      *id, "Duplicate", StaffLayout::single_staff(), *MidiChannel::create(1));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(project.find_active_track(*id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);
}

TEST(ProjectTrackTest, AddTrackWithIdRejectsWhenActiveSetIsFull) {
  Project project = make_project();
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }

  const TrackId id     = TrackId::generate();
  const Result  result = project.add_track_with_id(
      id, "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(project.find_active_track(id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 64u);
}

TEST(ProjectTrackTest, HardRemoveTrackErasesTrackReindexesAndRemovesLanes) {
  Project    project = make_project();
  const auto first   = project.add_track("First", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  const auto second  = project.add_track("Second", StaffLayout::single_staff(),
                                         *MidiChannel::create(1));
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  NodeId node_id = project.add_node("Node");
  ASSERT_TRUE(project.find_node(node_id)->has_lane(*first));
  ASSERT_TRUE(project.find_node(node_id)->has_lane(*second));

  EXPECT_TRUE(project.hard_remove_track(*first).ok());

  EXPECT_EQ(project.find_active_track(*first), nullptr);
  EXPECT_EQ(project.find_archived_track(*first), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_FALSE(project.find_node(node_id)->has_lane(*first));
  EXPECT_EQ(project.find_node(node_id)->lane_count(), 1u);
  EXPECT_EQ(project.find_active_track(*second)->index().value(), 0u);
}

TEST(ProjectTrackTest, HardRemoveTrackFailsForNonActiveId) {
  Project project = make_project();
  EXPECT_EQ(project.hard_remove_track(TrackId::generate()).code(),
            ResultCode::kInvalidArgument);
}

TEST(ProjectTrackTest, HardRemoveTrackRemovesLaneEvenWithNotationContent) {
  Project    project = make_project();
  const auto id      = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  ASSERT_TRUE(id.has_value());
  NodeId node_id = project.add_node("Node");

  Node*      node = project.find_node(node_id);
  TrackLane* lane = node->lane(*id);
  ASSERT_NE(lane, nullptr);
  lane->ensure_stave(StaveId::generate());
  EXPECT_EQ(lane->stave_count(), 1u);

  EXPECT_TRUE(project.hard_remove_track(*id).ok());
  EXPECT_FALSE(node->has_lane(*id));
}

TEST(ProjectTrackTest, StartNodeMustBeOwnedByProject) {
  Project project = make_project();
  EXPECT_FALSE(project.set_start_node(NodeId::generate()).ok());

  const NodeId node_id = project.add_node("Start");
  EXPECT_TRUE(project.set_start_node(node_id).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node_id);
}

TEST(ProjectTrackTest, DefaultTempoAndDynamicAreSettable) {
  Project    project = make_project();
  const auto tempo   = Tempo::create(Rational(96), NoteValue::kQuarter);
  ASSERT_TRUE(tempo.has_value());
  project.set_default_tempo(*tempo);
  EXPECT_EQ(project.default_tempo(), *tempo);

  project.set_default_dynamic(Dynamic::kPpp);
  EXPECT_EQ(project.default_dynamic(), Dynamic::kPpp);
}

// =========================================================================
// Phase 8d-iv — Project::add_node_with_id / remove_node / restore_node
// =========================================================================

static_assert(std::is_copy_constructible_v<Node>);

TEST(ProjectTrackTest, AddNodeWithIdPreservesSuppliedIdAndAlignsLanes) {
  Project    project = make_project();
  const auto track_a = project.add_track("A", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  const auto track_b = project.add_track("B", StaffLayout::single_staff(),
                                         *MidiChannel::create(1));
  ASSERT_TRUE(track_a.has_value());
  ASSERT_TRUE(track_b.has_value());

  const NodeId id     = NodeId::generate();
  const Result result = project.add_node_with_id(id, "Restored");

  ASSERT_TRUE(result.ok());
  const Node* node = project.find_node(id);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->name(), "Restored");
  EXPECT_TRUE(node->has_lane(*track_a));
  EXPECT_TRUE(node->has_lane(*track_b));
}

TEST(ProjectTrackTest, AddNodeWithIdRejectsDuplicateIdNoMutation) {
  Project      project = make_project();
  const NodeId id      = project.add_node("Existing");

  const Result result = project.add_node_with_id(id, "Duplicate");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 1u);
  EXPECT_EQ(project.find_node(id)->name(), "Existing");
}

TEST(ProjectTrackTest, RemoveNodeClearsOtherNodesInboundEdgesAndRoutes) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto c_id    = project.add_node("C");
  Node*      a       = project.find_node(a_id);
  Node*      b       = project.find_node(b_id);
  Node*      c       = project.find_node(c_id);
  const auto in_1    = b->add_input("In1");
  const auto in_2    = b->add_input("In2");
  const auto a_out   = a->add_output("Out");
  const auto c_out   = c->add_output("Out");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, in_1).ok());
  ASSERT_TRUE(graph.connect(c_id, c_out, b_id, in_2).ok());

  OutputConnector*              a_output    = a->find_output(a_out);
  OutputConnector*              c_output    = c->find_output(c_out);
  const std::vector<RoutePoint> a_waypoints = {{0.0, 0.0}, {5.0, 0.0}};
  ASSERT_TRUE(a_output->route().set_custom_route(a_waypoints).ok());

  ASSERT_TRUE(project.set_start_node(b_id).ok());

  const Result result = project.remove_node(b_id);
  ASSERT_TRUE(result.ok());

  EXPECT_EQ(project.find_node(b_id), nullptr);
  EXPECT_FALSE(a_output->destination().has_value());
  EXPECT_TRUE(a_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());
  EXPECT_FALSE(project.start_node().has_value());
}

TEST(ProjectTrackTest, RemoveNodeUnknownIdFails) {
  Project project = make_project();
  EXPECT_EQ(project.remove_node(NodeId::generate()).code(),
            ResultCode::kInvalidArgument);
}

TEST(ProjectTrackTest, RemoveNodeLeavesStartNodeUnaffectedIfNotReferenced) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  ASSERT_TRUE(project.set_start_node(a_id).ok());

  ASSERT_TRUE(project.remove_node(b_id).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), a_id);
}

TEST(ProjectTrackTest, RestoreNodeReinsertsFullValuePreservingId) {
  Project    project  = make_project();
  const auto track_id = project.add_track("T", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  const NodeId id   = project.add_node("Original");
  Node*        node = project.find_node(id);
  const auto   out  = node->add_output("Out", ConnectorType::kSequential);
  const auto   in   = node->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(id, out, id, in).ok());

  std::vector<graphscore::Measure> measures = {
      graphscore::Measure{*graphscore::TimeSignature::create(4, 4),
                          *graphscore::KeySignature::create(0)}};
  auto timeline = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));
  node->lane(*track_id)->ensure_stave(StaveId::generate());

  Node snapshot = *node;
  ASSERT_TRUE(project.remove_node(id).ok());
  EXPECT_EQ(project.find_node(id), nullptr);

  const Result restore_result = project.restore_node(snapshot);
  ASSERT_TRUE(restore_result.ok());

  const Node* restored = project.find_node(id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->id(), id);
  EXPECT_EQ(restored->name(), "Original");
  ASSERT_TRUE(restored->has_lane(*track_id));
  EXPECT_EQ(restored->lane(*track_id)->stave_count(), 1u);
  ASSERT_TRUE(restored->has_timeline());
  EXPECT_EQ(restored->timeline()->measures().measure_count(), 1u);
  const OutputConnector* restored_output = restored->find_output(out);
  ASSERT_NE(restored_output, nullptr);
  ASSERT_TRUE(restored_output->destination().has_value());
  EXPECT_EQ(restored_output->destination()->node, id);
  EXPECT_EQ(restored_output->destination()->connector, in);
}

TEST(ProjectTrackTest, RestoreNodeRejectsDuplicateIdNoMutation) {
  Project      project = make_project();
  const NodeId id      = project.add_node("Existing");
  const Node   duplicate(id, "Duplicate");

  const Result result = project.restore_node(duplicate);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 1u);
  EXPECT_EQ(project.find_node(id)->name(), "Existing");
}
