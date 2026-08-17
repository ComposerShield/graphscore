// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8d-iii — AddTrackCommand
// =========================================================================

TEST(CommandTest, AddTrackRoundTripPreservesId) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));

  ASSERT_TRUE(cmd->execute(project).ok());
  ASSERT_EQ(project.active_tracks().size(), 1u);
  const TrackId created_id = project.active_tracks().front().id();
  EXPECT_EQ(project.active_tracks().front().name(), "Track");
  EXPECT_TRUE(project.find_node(node_id)->has_lane(created_id));

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_active_track(created_id), nullptr);
  EXPECT_EQ(project.find_archived_track(created_id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_FALSE(project.find_node(node_id)->has_lane(created_id));

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_NE(project.find_active_track(created_id), nullptr);
  EXPECT_EQ(project.find_active_track(created_id)->id(), created_id);
  EXPECT_TRUE(project.find_node(node_id)->has_lane(created_id));
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, AddTrackDoubleExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, AddTrackUndoWithoutExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 0u);
}

TEST(CommandTest, AddTrackRedoWithoutUndoRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, AddTrackAtCapFailsNoMutation) {
  Project project = make_project();
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }

  auto cmd = std::make_unique<AddTrackCommand>(
      "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 64u);
}

// Linear-history safety: a later command must undo before the AddTrack does,
// so hard_remove_track always runs against an empty, just-added lane.
TEST(CommandTest, AddTrackLinearHistoryUndoLeavesNoOrphanState) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  CommandHistory history;
  ASSERT_TRUE(history
                  .execute_new(std::make_unique<AddTrackCommand>(
                                   "Track", StaffLayout::single_staff(),
                                   *MidiChannel::create(0)),
                               project)
                  .ok());
  const TrackId track_id = project.active_tracks().front().id();

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "Renamed"),
                       project)
          .ok());

  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.find_node(node_id)->name(), "Renamed");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Node");
  EXPECT_EQ(project.active_tracks().size(), 1u);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.find_active_track(track_id), nullptr);
  EXPECT_FALSE(project.find_node(node_id)->has_lane(track_id));
}

// 64-track/64-measure practicality: AddTrackCommand at the cap fails
// cleanly, and archive/restore of an existing track still round-trips.
TEST(CommandTest, SixtyFourTrackAndMeasureNodePracticality) {
  Project project = make_project();

  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }
  ASSERT_EQ(project.active_tracks().size(), 64u);

  NodeId node_id = project.add_node("Node");
  Node*  node    = project.find_node(node_id);
  ASSERT_EQ(node->lane_count(), 64u);

  std::vector<Measure> measures;
  measures.reserve(64);
  for (int i = 0; i < 64; ++i) {
    measures.push_back(
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  }
  auto timeline = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(timeline.has_value());
  EXPECT_EQ(timeline->measures().measure_count(), 64u);
  node->set_timeline(std::move(*timeline));

  auto add_cmd = std::make_unique<AddTrackCommand>(
      "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_EQ(add_cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 64u);

  const TrackId archive_id  = project.active_tracks().front().id();
  auto          archive_cmd = std::make_unique<ArchiveTrackCommand>(archive_id);
  ASSERT_TRUE(archive_cmd->execute(project).ok());
  EXPECT_EQ(project.find_active_track(archive_id), nullptr);

  ASSERT_TRUE(archive_cmd->undo(project).ok());
  EXPECT_NE(project.find_active_track(archive_id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 64u);
}

// =========================================================================
// Phase 8d-iii — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8diii) {
  auto run_sequence = [](Project& project) {
    CommandHistory history;

    EXPECT_TRUE(history
                    .execute_new(std::make_unique<AddTrackCommand>(
                                     "First", StaffLayout::single_staff(),
                                     *MidiChannel::create(0)),
                                 project)
                    .ok());
    const TrackId first_id = project.active_tracks().front().id();

    EXPECT_TRUE(history
                    .execute_new(std::make_unique<AddTrackCommand>(
                                     "Second", StaffLayout::single_staff(),
                                     *MidiChannel::create(1)),
                                 project)
                    .ok());

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<ArchiveTrackCommand>(first_id),
                         project)
            .ok());

    return project.active_tracks().front().id();
  };

  Project first  = make_project();
  Project second = make_project();

  const TrackId first_survivor  = run_sequence(first);
  const TrackId second_survivor = run_sequence(second);

  EXPECT_EQ(first.active_tracks().size(), 1u);
  EXPECT_EQ(second.active_tracks().size(), 1u);
  EXPECT_EQ(first.archived_tracks().size(), 1u);
  EXPECT_EQ(second.archived_tracks().size(), 1u);
  EXPECT_EQ(first.active_tracks().front().name(), "Second");
  EXPECT_EQ(second.active_tracks().front().name(), "Second");
  EXPECT_NE(first.find_active_track(first_survivor), nullptr);
  EXPECT_NE(second.find_active_track(second_survivor), nullptr);
}

// =========================================================================
// Phase 8d-iv — AddNodeCommand
// =========================================================================

TEST(CommandTest, AddNodeRoundTripPreservesId) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<AddNodeCommand>("Node");

  ASSERT_TRUE(cmd->execute(project).ok());
  ASSERT_EQ(project.nodes().size(), 1u);
  const NodeId created_id = project.nodes().front().id();
  EXPECT_EQ(project.nodes().front().name(), "Node");
  EXPECT_TRUE(project.find_node(created_id)->has_lane(*track_id));

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(created_id), nullptr);
  EXPECT_EQ(project.nodes().size(), 0u);

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_NE(project.find_node(created_id), nullptr);
  EXPECT_EQ(project.find_node(created_id)->id(), created_id);
  EXPECT_EQ(project.find_node(created_id)->name(), "Node");
  EXPECT_TRUE(project.find_node(created_id)->has_lane(*track_id));
  EXPECT_EQ(project.nodes().size(), 1u);
}

TEST(CommandTest, AddNodeUsesProjectTempoAndCompleteActiveTrackStructure) {
  Project     project       = make_project();
  const Tempo default_tempo = *Tempo::create(Rational(72), NoteValue::kHalf);
  project.set_default_tempo(default_tempo);
  const TrackId single =
      *project.add_track("Single", StaffLayout::single_staff(Clef::kAlto),
                         *MidiChannel::create(0));
  const TrackId grand = *project.add_track("Grand", StaffLayout::grand_staff(),
                                           *MidiChannel::create(1));

  AddNodeCommand command("Node");
  ASSERT_TRUE(command.execute(project).ok());

  ASSERT_EQ(project.nodes().size(), 1u);
  const Node& node = project.nodes().front();
  ASSERT_NE(node.lane(single), nullptr);
  ASSERT_NE(node.lane(grand), nullptr);
  EXPECT_NE(node.lane(single)->stave(
                project.find_active_track(single)->layout().staves()[0].id),
            nullptr);
  for (const graphscore::StaveDefinition& stave :
       project.find_active_track(grand)->layout().staves()) {
    EXPECT_NE(node.lane(grand)->stave(stave.id), nullptr);
  }
  ASSERT_NE(node.timeline(), nullptr);
  EXPECT_EQ(node.timeline()->measures().measure_count(), 1u);
  EXPECT_EQ(node.timeline()->measures().measure(0).time_signature,
            *TimeSignature::create(4, 4));
  ASSERT_NE(node.timeline()->tempo(), nullptr);
  ASSERT_EQ(node.timeline()->tempo()->points().size(), 1u);
  EXPECT_EQ(node.timeline()->tempo()->points()[0].tempo, default_tempo);
}

TEST(CommandTest, AddNodeInheritsExactTempoAtSourceMainBoundary) {
  Project project = make_project();
  static_cast<void>(project.add_track("Track", StaffLayout::single_staff(),
                                      *MidiChannel::create(0)));
  const NodeId source_id = project.add_node("Source");
  Node* const  source    = project.find_node(source_id);
  auto         timeline  = *NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), KeySignature{}}}, {});
  ASSERT_TRUE(timeline.set_pickdown(*Rational::create(1, 4)).ok());
  const Tempo boundary_tempo =
      *Tempo::create(Rational(144), NoteValue::kEighth);
  ASSERT_TRUE(
      timeline
          .set_tempo(
              {TempoPoint{Rational(0),
                          *Tempo::create(Rational(80), NoteValue::kQuarter),
                          TempoSegmentKind::kLinear},
               TempoPoint{Rational(1), boundary_tempo,
                          TempoSegmentKind::kSmooth},
               TempoPoint{*Rational::create(9, 8),
                          *Tempo::create(Rational(200), NoteValue::kHalf),
                          TempoSegmentKind::kStep}})
          .ok());
  source->set_timeline(std::move(timeline));

  AddNodeCommand command("Destination", source_id);
  ASSERT_TRUE(command.execute(project).ok());

  ASSERT_EQ(project.nodes().size(), 2u);
  const Node& destination = project.nodes().back();
  ASSERT_NE(destination.timeline(), nullptr);
  ASSERT_NE(destination.timeline()->tempo(), nullptr);
  ASSERT_EQ(destination.timeline()->tempo()->points().size(), 1u);
  EXPECT_EQ(destination.timeline()->tempo()->points()[0].tempo, boundary_tempo);
}

TEST(CommandTest, AddNodeRedoRestoresInheritedTempoSnapshot) {
  Project     project  = make_project();
  const Tempo original = *Tempo::create(Rational(96), NoteValue::kQuarter);
  project.set_default_tempo(original);
  AddNodeCommand command("Node");
  ASSERT_TRUE(command.execute(project).ok());
  const NodeId created_id = project.nodes().front().id();
  ASSERT_TRUE(command.undo(project).ok());

  project.set_default_tempo(*Tempo::create(Rational(180), NoteValue::kEighth));
  ASSERT_TRUE(command.redo(project).ok());

  const Node* const restored = project.find_node(created_id);
  ASSERT_NE(restored, nullptr);
  ASSERT_NE(restored->timeline(), nullptr);
  ASSERT_NE(restored->timeline()->tempo(), nullptr);
  EXPECT_EQ(restored->timeline()->tempo()->points()[0].tempo, original);
}

TEST(CommandTest, AddNodeRejectsMissingSourceWithoutMutation) {
  Project        project = make_project();
  AddNodeCommand command("Node", NodeId::generate());

  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(project.nodes().empty());
}

TEST(CommandTest, AddNodeDoubleExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddNodeCommand>("Node");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 1u);
}

TEST(CommandTest, AddNodeUndoWithoutExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddNodeCommand>("Node");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 0u);
}

TEST(CommandTest, AddNodeRedoWithoutUndoRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddNodeCommand>("Node");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 1u);
}

// =========================================================================
// Phase 8d-iv — RemoveNodeCommand
// =========================================================================

TEST(CommandTest, RemoveNodeMissingIdFailsNoMutation) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(NodeId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id), nullptr);
  EXPECT_EQ(project.nodes().size(), 1u);
}

TEST(CommandTest, RemoveNodeDoubleExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveNodeUndoWithoutExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id), nullptr);
}

TEST(CommandTest, RemoveNodeRedoWithoutUndoRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// The crux test: fan-in cascade across two other nodes, an outgoing edge
// from the removed node to one of them, a NodeTimeline, notation content
// in a lane, and start-node status -- everything RemoveNodeCommand's
// snapshot must carry, all restored exactly on undo.
TEST(CommandTest, RemoveNodeFullAggregateRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");
  const NodeId c_id = project.add_node("C");
  Node*        a    = project.find_node(a_id);
  Node*        b    = project.find_node(b_id);
  Node*        c    = project.find_node(c_id);

  const ConnectorId a_in   = a->add_input("AIn");
  const ConnectorId b_in_1 = b->add_input("BIn1");
  const ConnectorId b_in_2 = b->add_input("BIn2");
  const ConnectorId a_out  = a->add_output("AOut");
  const ConnectorId c_out  = c->add_output("COut");
  const ConnectorId b_out  = b->add_output("BOut");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, b_in_1).ok());
  ASSERT_TRUE(graph.connect(c_id, c_out, b_id, b_in_2).ok());
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, a_in).ok());

  OutputConnector*              a_output    = a->find_output(a_out);
  OutputConnector*              c_output    = c->find_output(c_out);
  OutputConnector*              b_output    = b->find_output(b_out);
  const std::vector<RoutePoint> a_waypoints = {{0.0, 0.0}, {5.0, 0.0}};
  const std::vector<RoutePoint> c_waypoints = {{0.0, 0.0}, {0.0, 7.0}};
  const std::vector<RoutePoint> b_waypoints = {{1.0, 1.0}, {1.0, 2.0}};
  ASSERT_TRUE(a_output->route().set_custom_route(a_waypoints).ok());
  ASSERT_TRUE(c_output->route().set_custom_route(c_waypoints).ok());
  ASSERT_TRUE(b_output->route().set_custom_route(b_waypoints).ok());

  ASSERT_TRUE(project.set_start_node(b_id).ok());

  b->lane(*track_id)->ensure_stave(StaveId::generate());
  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto timeline = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(timeline.has_value());
  b->set_timeline(std::move(*timeline));

  auto cmd = std::make_unique<RemoveNodeCommand>(b_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(b_id), nullptr);
  EXPECT_FALSE(a_output->destination().has_value());
  EXPECT_TRUE(a_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());
  EXPECT_FALSE(project.start_node().has_value());

  ASSERT_TRUE(cmd->undo(project).ok());
  const Node* restored_b = project.find_node(b_id);
  ASSERT_NE(restored_b, nullptr);
  EXPECT_EQ(restored_b->id(), b_id);
  ASSERT_TRUE(restored_b->has_timeline());
  EXPECT_EQ(restored_b->timeline()->measures().measure_count(), 1u);
  ASSERT_TRUE(restored_b->has_lane(*track_id));
  EXPECT_EQ(restored_b->lane(*track_id)->stave_count(), 1u);
  const OutputConnector* restored_b_output = restored_b->find_output(b_out);
  ASSERT_NE(restored_b_output, nullptr);
  ASSERT_TRUE(restored_b_output->destination().has_value());
  EXPECT_EQ(restored_b_output->destination()->node, a_id);
  EXPECT_EQ(restored_b_output->destination()->connector, a_in);
  EXPECT_FALSE(restored_b_output->route().is_automatic());
  EXPECT_EQ(restored_b_output->route().waypoints(), b_waypoints);

  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), b_id);

  ASSERT_TRUE(a_output->destination().has_value());
  EXPECT_EQ(a_output->destination()->node, b_id);
  EXPECT_EQ(a_output->destination()->connector, b_in_1);
  EXPECT_FALSE(a_output->route().is_automatic());
  EXPECT_EQ(a_output->route().waypoints(), a_waypoints);

  ASSERT_TRUE(c_output->destination().has_value());
  EXPECT_EQ(c_output->destination()->node, b_id);
  EXPECT_EQ(c_output->destination()->connector, b_in_2);
  EXPECT_FALSE(c_output->route().is_automatic());
  EXPECT_EQ(c_output->route().waypoints(), c_waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(b_id), nullptr);
  EXPECT_FALSE(a_output->destination().has_value());
  EXPECT_TRUE(a_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());
  EXPECT_FALSE(project.start_node().has_value());
}

// Dedicated self-loop test: a node's own output targeting one of its own
// inputs must be restored intact as part of the Node snapshot, and must
// never be treated as a cross-node cascade edge that undo tries (and
// fails) to reconnect via Graph::connect on an already-destined output.
TEST(CommandTest, RemoveNodeSelfLoopSurvivesRoundTrip) {
  Project           project = make_project();
  const NodeId      node_id = project.add_node("Node");
  Node*             node    = project.find_node(node_id);
  const ConnectorId in      = node->add_input("In");
  const ConnectorId out     = node->add_output("Out");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(node_id, out, node_id, in).ok());

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const Node* restored = project.find_node(node_id);
  ASSERT_NE(restored, nullptr);
  const OutputConnector* restored_output = restored->find_output(out);
  ASSERT_NE(restored_output, nullptr);
  ASSERT_TRUE(restored_output->destination().has_value());
  EXPECT_EQ(restored_output->destination()->node, node_id);
  EXPECT_EQ(restored_output->destination()->connector, in);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id), nullptr);
}

// 64-track/64-measure practicality: remove-then-undo a node carrying a
// 64-measure timeline in a project with 64 active tracks.
TEST(CommandTest, RemoveNodeSixtyFourTrackAndMeasurePracticality) {
  Project project = make_project();
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }
  ASSERT_EQ(project.active_tracks().size(), 64u);

  const NodeId node_id = project.add_node("Node");
  Node*        node    = project.find_node(node_id);
  ASSERT_EQ(node->lane_count(), 64u);

  std::vector<Measure> measures;
  measures.reserve(64);
  for (int i = 0; i < 64; ++i) {
    measures.push_back(
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  }
  auto timeline = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const Node* restored = project.find_node(node_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->lane_count(), 64u);
  ASSERT_TRUE(restored->has_timeline());
  EXPECT_EQ(restored->timeline()->measures().measure_count(), 64u);
}

// =========================================================================
// Phase 8d-iv — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8div) {
  auto run_sequence = [](Project& project) {
    CommandHistory history;

    EXPECT_TRUE(
        history.execute_new(std::make_unique<AddNodeCommand>("A"), project)
            .ok());
    const NodeId a_id = project.nodes().front().id();

    EXPECT_TRUE(
        history.execute_new(std::make_unique<AddNodeCommand>("B"), project)
            .ok());
    const NodeId b_id = project.nodes().back().id();

    EXPECT_TRUE(
        history.execute_new(std::make_unique<RemoveNodeCommand>(a_id), project)
            .ok());

    return b_id;
  };

  Project first  = make_project();
  Project second = make_project();

  const NodeId first_survivor  = run_sequence(first);
  const NodeId second_survivor = run_sequence(second);

  EXPECT_EQ(first.nodes().size(), 1u);
  EXPECT_EQ(second.nodes().size(), 1u);
  EXPECT_EQ(first.nodes().front().name(), "B");
  EXPECT_EQ(second.nodes().front().name(), "B");
  EXPECT_NE(first.find_node(first_survivor), nullptr);
  EXPECT_NE(second.find_node(second_survivor), nullptr);
}
