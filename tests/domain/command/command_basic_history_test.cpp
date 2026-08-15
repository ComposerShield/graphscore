// SPDX-License-Identifier: Apache-2.0

#include "command_test_fakes.hpp"
#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// SetNodeNameCommand
// =========================================================================

TEST(CommandTest, SetNodeNameRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Original");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "Renamed");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Renamed");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Original");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Renamed");
}

TEST(CommandTest, SetNodeNameMissingIdFails) {
  Project project = make_project();
  NodeId  missing = NodeId::generate();

  auto cmd = std::make_unique<SetNodeNameCommand>(missing, "X");

  Result result = cmd->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetNodeNameDoubleExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("A");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "B");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
}

TEST(CommandTest, SetNodeNameUndoWithoutExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("A");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "B");

  EXPECT_FALSE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
}

TEST(CommandTest, SetNodeNameRedoWithoutUndoRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("A");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "B");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
}

// =========================================================================
// SetTrackNameCommand
// =========================================================================

TEST(CommandTest, SetTrackNameRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Old", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<SetTrackNameCommand>(*track_id, "New");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "New");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "Old");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "New");
}

TEST(CommandTest, SetTrackNameMissingIdFails) {
  Project project = make_project();
  TrackId missing = TrackId::generate();

  auto cmd = std::make_unique<SetTrackNameCommand>(missing, "X");

  EXPECT_FALSE(cmd->execute(project).ok());
}

// =========================================================================
// SetProjectTempoCommand
// =========================================================================

TEST(CommandTest, SetProjectTempoRoundTrip) {
  Project project = make_project();

  const auto old_tempo = project.default_tempo();
  const auto new_tempo = *Tempo::create(Rational(160), NoteValue::kQuarter);

  auto cmd = std::make_unique<SetProjectTempoCommand>(new_tempo);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.default_tempo(), new_tempo);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.default_tempo(), old_tempo);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.default_tempo(), new_tempo);
}

// =========================================================================
// CommandHistory — basic stack behaviour
// =========================================================================

TEST(CommandTest, HistoryUndoRedoOrder) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(a, "A-renamed"),
                       project)
          .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(b, "B-renamed"),
                       project)
          .ok());

  EXPECT_EQ(project.find_node(a)->name(), "A-renamed");
  EXPECT_EQ(project.find_node(b)->name(), "B-renamed");
  EXPECT_EQ(history.undo_stack_size(), 2u);
  EXPECT_EQ(history.redo_stack_size(), 0u);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 2u);

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A-renamed");
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(b)->name(), "B-renamed");
  EXPECT_EQ(history.undo_stack_size(), 2u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryEmptyUndoIsSafe) {
  Project        project = make_project();
  CommandHistory history;

  EXPECT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.undo_stack_size(), 0u);
}

TEST(CommandTest, HistoryEmptyRedoIsSafe) {
  Project        project = make_project();
  CommandHistory history;

  EXPECT_TRUE(history.redo(project).ok());
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryClear) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());

  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  ASSERT_TRUE(history.clear().ok());

  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
}

TEST(CommandTest, HistoryFailedCommandNotRecorded) {
  Project project = make_project();
  NodeId  missing = NodeId::generate();

  CommandHistory history;

  Result result = history.execute_new(
      std::make_unique<SetNodeNameCommand>(missing, "X"), project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryRedoClearedByNewSuccessfulCommand) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.redo_stack_size(), 1u);

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "C"),
                       project)
          .ok());
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(project.find_node(node_id)->name(), "C");
}

TEST(CommandTest, HistoryRedoStackSurvivesFailedNewCommand) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.redo_stack_size(), 1u);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(project.find_node(node_id)->name(), "A");

  Result result = history.execute_new(
      std::make_unique<SetNodeNameCommand>(NodeId::generate(), "X"), project);
  EXPECT_FALSE(result.ok());

  EXPECT_EQ(history.redo_stack_size(), 1u);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_TRUE(history.can_redo());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
}

TEST(CommandTest, HistoryNullCommandReturnsInvalidArgument) {
  Project        project = make_project();
  CommandHistory history;

  Result result = history.execute_new(nullptr, project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

// =========================================================================
// History undo/redo failure does not lose the command
// =========================================================================

TEST(CommandTest, HistoryUndoFailureKeepsCommandInUndo) {
  Project project = make_project();
  auto cmd = std::make_unique<TestCommand>("X", TestCommand::FailMode::kOnUndo);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(cmd), project).ok());

  Result result = history.undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryRedoFailureKeepsCommandInRedo) {
  Project project = make_project();
  auto cmd = std::make_unique<TestCommand>("X", TestCommand::FailMode::kOnRedo);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(cmd), project).ok());
  ASSERT_TRUE(history.undo(project).ok());

  Result result = history.redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);
}

TEST(CommandTest, HistoryEmptyUndoRedoNoModelChange) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("Original");
  CommandHistory history;

  EXPECT_TRUE(history.undo(project).ok());
  EXPECT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Original");
}

// =========================================================================
// Full history round-trip with real commands
// =========================================================================

TEST(CommandTest, FullHistoryRoundTripWithRealCommands) {
  Project project = make_project();
  NodeId  node_a  = project.add_node("A");
  NodeId  node_b  = project.add_node("B");

  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_a, "A1"),
                       project)
          .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_b, "B1"),
                       project)
          .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_a, "A2"),
                       project)
          .ok());

  EXPECT_EQ(project.find_node(node_a)->name(), "A2");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B");

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_a, "A3"),
                       project)
          .ok());
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(project.find_node(node_a)->name(), "A3");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");
}

// =========================================================================
// Missing-ID mutation verification: model is unchanged on failure
// =========================================================================

TEST(CommandTest, SetNodeNameMissingIdDoesNotChangeProject) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Original");

  auto cmd = std::make_unique<SetNodeNameCommand>(NodeId::generate(), "X");
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Original");
}

TEST(CommandTest, SetTrackNameMissingIdDoesNotChangeProject) {
  Project    project  = make_project();
  const auto track_id = project.add_track(
      "Original", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<SetTrackNameCommand>(TrackId::generate(), "X");
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "Original");
}
