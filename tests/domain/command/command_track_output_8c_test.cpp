// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8c-i — ArchiveTrackCommand
// =========================================================================

TEST(CommandTest, ArchiveTrackRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks()[0].id(), *track_id);
  EXPECT_EQ(project.find_active_track(*track_id), nullptr);
  EXPECT_NE(project.find_archived_track(*track_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);
  EXPECT_EQ(project.active_tracks()[0].id(), *track_id);
  EXPECT_NE(project.find_active_track(*track_id), nullptr);
  EXPECT_EQ(project.find_archived_track(*track_id), nullptr);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);
}

TEST(CommandTest, ArchiveTrackMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<ArchiveTrackCommand>(TrackId::generate());
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, ArchiveTrackDoubleExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ArchiveTrackUndoWithoutExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ArchiveTrackRedoWithoutUndoRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ArchiveTrackUndoFailsWhenRestoreWouldExceedCap) {
  Project project = make_project();

  std::optional<TrackId> first_track_id;
  for (int i = 0; i < static_cast<int>(Project::kMaxActiveTracks); ++i) {
    const auto id = project.add_track("Track", StaffLayout::single_staff(),
                                      *MidiChannel::create(0));
    ASSERT_TRUE(id.has_value());
    if (i == 0)
      first_track_id = id;
  }
  ASSERT_TRUE(first_track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*first_track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), Project::kMaxActiveTracks - 1);

  // Fill the freed slot with a different track so the cap is full again.
  const auto filler = project.add_track("Filler", StaffLayout::single_staff(),
                                        *MidiChannel::create(1));
  ASSERT_TRUE(filler.has_value());
  EXPECT_EQ(project.active_tracks().size(), Project::kMaxActiveTracks);

  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  // State stays kDone: the track remains archived.
  EXPECT_EQ(project.find_archived_track(*first_track_id) != nullptr, true);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — RestoreTrackCommand
// =========================================================================

TEST(CommandTest, RestoreTrackRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);
}

TEST(CommandTest, RestoreTrackMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<RestoreTrackCommand>(TrackId::generate());
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, RestoreTrackOnActiveTrackFails) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, RestoreTrackDoubleExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RestoreTrackUndoWithoutExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RestoreTrackRedoWithoutUndoRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputTypeCommand
// =========================================================================

TEST(CommandTest, SetOutputTypeRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->type(), ConnectorType::kVertical);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->type(), ConnectorType::kSequential);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->type(), ConnectorType::kVertical);
}

TEST(CommandTest, SetOutputTypeMissingNodeIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(NodeId::generate(), out_id,
                                                    ConnectorType::kVertical);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputTypeMissingConnectorIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetOutputTypeCommand>(
      node_id, ConnectorId::generate(), ConnectorType::kVertical);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputTypeDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeRejectsClashAndLeavesStateUnchanged) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto first   = node->add_output("First", ConnectorType::kSequential);
  const auto second  = node->add_output("Second", ConnectorType::kSequential);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(first, event).ok());
  ASSERT_TRUE(node->bind_output_event(second, event).ok());

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, first,
                                                    ConnectorType::kVertical);
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(first)->type(), ConnectorType::kSequential);
  EXPECT_EQ(node->find_output(second)->type(), ConnectorType::kSequential);

  // The command is still kFresh -- undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeUpdatesAndRestoresBoundListenerType) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());
  ASSERT_EQ(node->find_listener(event)->bound_type(),
            ConnectorType::kSequential);

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_listener(event)->bound_type(), ConnectorType::kVertical);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_listener(event)->bound_type(),
            ConnectorType::kSequential);
}

// =========================================================================
// Phase 8c-i — SetListenerPolicyCommand
// =========================================================================

TEST(CommandTest, SetListenerPolicyRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  const auto* listener = node->find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(listener->capacity(), 5u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(listener->capacity(), 5u);
}

TEST(CommandTest, SetListenerPolicyMissingNodeIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetListenerPolicyCommand>(
      NodeId::generate(), EventId::generate(), QueuePolicy::kFifo, 5);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetListenerPolicyNoListenerFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetListenerPolicyCommand>(
      node_id, EventId::generate(), QueuePolicy::kFifo, 5);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetListenerPolicyFifoZeroCapacityRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 0);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);

  const auto* listener = node->find_listener(event);
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);
}

TEST(CommandTest, SetListenerPolicyDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetListenerPolicyUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetListenerPolicyRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputPriorityCommand
// =========================================================================

TEST(CommandTest, SetOutputPriorityRoundTrip) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);
  EXPECT_EQ(output->priority(), 0);

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(output->priority(), 7);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(output->priority(), 0);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(output->priority(), 7);
}

TEST(CommandTest, SetOutputPriorityMissingNodeIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetOutputPriorityCommand>(
      NodeId::generate(), ConnectorId::generate(), 7);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputPriorityMissingConnectorIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(
      node_id, ConnectorId::generate(), 7);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputPriorityDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputPriorityUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputPriorityRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}
