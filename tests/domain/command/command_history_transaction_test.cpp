// SPDX-License-Identifier: Apache-2.0

#include "command_test_fakes.hpp"
#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// CommandHistory::Transaction — provisional execute / commit / abort
// =========================================================================

TEST(CommandTest, TransactionAbortRestoresExactHistoryWithRedo) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  // Establish undo + redo history: "A" -> "B", then undo.
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // Provisional execute: the command applies, but neither stack changes yet.
  auto tx = history.begin_transaction(
      std::make_unique<SetNodeNameCommand>(node_id, "C"), project);
  ASSERT_TRUE(tx.active());
  EXPECT_EQ(project.find_node(node_id)->name(), "C");
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);
  EXPECT_TRUE(history.can_redo());

  // Abort: exact project, undo size, redo size, and redo capability restored.
  ASSERT_TRUE(tx.abort().ok());
  EXPECT_FALSE(tx.active());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);
  EXPECT_TRUE(history.can_redo());

  // The surviving redo entry is still executable.
  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
}

TEST(CommandTest, TransactionCommitClearsRedo) {
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

  auto tx = history.begin_transaction(
      std::make_unique<SetNodeNameCommand>(node_id, "C"), project);
  ASSERT_TRUE(tx.active());
  ASSERT_TRUE(tx.commit().ok());
  EXPECT_FALSE(tx.active());
  EXPECT_EQ(project.find_node(node_id)->name(), "C");
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_FALSE(history.can_redo());

  // The committed command is undoable like any execute_new result.
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
}

TEST(CommandTest, TransactionBeginExecuteFailureIsInactive) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  // A command whose execute() fails: begin_transaction records nothing.
  auto tx = history.begin_transaction(
      std::make_unique<SetNodeNameCommand>(NodeId::generate(), "X"), project);
  EXPECT_FALSE(tx.active());
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
}

TEST(CommandTest, TransactionNullCommandIsInactive) {
  Project        project = make_project();
  CommandHistory history;

  auto tx = history.begin_transaction(nullptr, project);
  EXPECT_FALSE(tx.active());
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, TransactionRejectsNestedBegin) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  auto first = history.begin_transaction(
      std::make_unique<SetNodeNameCommand>(node_id, "B"), project);
  ASSERT_TRUE(first.active());

  // A second begin_transaction while one is active is rejected.
  auto second = history.begin_transaction(
      std::make_unique<SetNodeNameCommand>(node_id, "C"), project);
  EXPECT_FALSE(second.active());

  // The other history operations are also rejected while a transaction is
  // active, so the provisional command cannot be disturbed.
  EXPECT_EQ(history
                .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "D"),
                             project)
                .code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(history.undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(history.redo(project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(first.abort().ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
}

TEST(CommandTest, TransactionAbortUndoFailurePoisonsAndReports) {
  Project                  project = make_project();
  CommandHistory           history;
  std::vector<std::string> log;

  auto tx = history.begin_transaction(
      std::make_unique<TestCommand>("Fail", TestCommand::FailMode::kOnUndo,
                                    &log),
      project);
  ASSERT_TRUE(tx.active());

  // abort() reports the undo failure, poisons the history, and the
  // transaction becomes inactive (the command was retained by the history).
  EXPECT_EQ(tx.abort().code(), ResultCode::kInternalError);
  EXPECT_FALSE(tx.active());
  EXPECT_TRUE(history.poisoned());
  EXPECT_EQ(history.poisoned_reason(), ResultCode::kInternalError);

  // While poisoned, every history operation is blocked with kCommandFaulted.
  EXPECT_EQ(
      history.execute_new(std::make_unique<TestCommand>("X"), project).code(),
      ResultCode::kCommandFaulted);
  EXPECT_EQ(history.undo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(history.redo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(history.clear().code(), ResultCode::kCommandFaulted);
  EXPECT_FALSE(
      history.begin_transaction(std::make_unique<TestCommand>("Y"), project)
          .active());

  // The command still fails on undo (persistent), so recover() stays
  // poisoned rather than silently clearing.
  EXPECT_FALSE(history.recover().ok());
  EXPECT_TRUE(history.poisoned());
}

// A one-shot undo failure recovers coherently when recover() retries: the
// retained command is destroyed, the poisoned state clears, and the project
// is restored to its exact pre-transaction state.
TEST(CommandTest, TransactionOneShotUndoFailureRecovers) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  auto cmd = std::make_unique<AdversarialNameCommand>(
      node_id, "B", AdversarialNameCommand::FailMode::kNever);
  cmd->set_fail_next_undo(true);  // fails exactly once, then succeeds

  auto tx = history.begin_transaction(std::move(cmd), project);
  ASSERT_TRUE(tx.active());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");

  // abort() consumes the one-shot failure and poisons the history.
  EXPECT_EQ(tx.abort().code(), ResultCode::kInternalError);
  EXPECT_FALSE(tx.active());
  EXPECT_TRUE(history.poisoned());
  // The project is still in the post-execute state (the rollback failed).
  EXPECT_EQ(project.find_node(node_id)->name(), "B");

  // recover() retries the retained command's undo: the one-shot failure has
  // been consumed, so the rollback succeeds and the project is restored.
  ASSERT_TRUE(history.recover().ok());
  EXPECT_FALSE(history.poisoned());
  EXPECT_EQ(history.poisoned_reason(), ResultCode::kSuccess);
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);

  // The history is usable again.
  EXPECT_TRUE(history.undo(project).ok());
}

// The MoveNoteheadCommand path through a transaction: abort restores the
// exact voice content while a pre-existing redo entry survives and stays
// executable — the precise domain invariant the notehead-move publication
// failure relies on.
TEST(CommandTest, TransactionAbortMoveNoteheadRestoresRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       note    = make_note(pitch_c4(), quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  CommandHistory history;

  // One committed move up (C4 -> D4), then undo it to leave a redo entry.
  ASSERT_TRUE(history
                  .execute_new(std::make_unique<MoveNoteheadCommand>(
                                   fx.node_id, fx.track_id, fx.stave_id,
                                   *Voice::create(1), note_id,
                                   NoteheadStepDirection::kUp),
                               fx.project)
                  .ok());
  ASSERT_TRUE(history.undo(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_c4());
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // Provisional move up again, then abort.
  auto tx = history.begin_transaction(
      std::make_unique<MoveNoteheadCommand>(
          fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
          NoteheadStepDirection::kUp),
      fx.project);
  ASSERT_TRUE(tx.active());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_d4());

  ASSERT_TRUE(tx.abort().ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_c4());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).id, note_id);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // The pre-existing redo remains executable.
  ASSERT_TRUE(history.redo(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_d4());
}

// =========================================================================
// CommandHistory::Transaction — destruction, poisoning, recovery, lifetime
// =========================================================================

// An abandoned transaction (never committed or aborted) whose undo fails
// persistently must poison the history rather than silently clearing the
// guard and discarding the command. The retained command and project
// survive destruction, every operation is blocked, and recover() retries.
TEST(CommandTest, TransactionDestructorPersistentUndoFailurePoisons) {
  Project                  project = make_project();
  CommandHistory           history;
  std::vector<std::string> log;

  {
    auto tx = history.begin_transaction(
        std::make_unique<TestCommand>("Fail", TestCommand::FailMode::kOnUndo,
                                      &log),
        project);
    ASSERT_TRUE(tx.active());
    // Abandoned: the destructor attempts the undo, fails, and poisons.
  }

  EXPECT_TRUE(history.poisoned());
  EXPECT_EQ(history.poisoned_reason(), ResultCode::kInternalError);

  EXPECT_EQ(history.undo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(history.redo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(
      history.execute_new(std::make_unique<TestCommand>("X"), project).code(),
      ResultCode::kCommandFaulted);
  EXPECT_EQ(history.clear().code(), ResultCode::kCommandFaulted);

  // The retained command still fails on undo (persistent), so recovery
  // cannot clear the poison.
  EXPECT_FALSE(history.recover().ok());
  EXPECT_TRUE(history.poisoned());
}

// An abandoned transaction whose undo fails exactly once recovers through
// recover(): the destructor poisons (retaining the command), and recover()
// retries against the retained project, restoring it and clearing the poison.
TEST(CommandTest, TransactionDestructorOneShotUndoFailureRecovers) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  {
    auto cmd = std::make_unique<AdversarialNameCommand>(
        node_id, "B", AdversarialNameCommand::FailMode::kNever);
    cmd->set_fail_next_undo(true);
    auto tx = history.begin_transaction(std::move(cmd), project);
    ASSERT_TRUE(tx.active());
    EXPECT_EQ(project.find_node(node_id)->name(), "B");
    // Abandoned: destructor undo fails once (one-shot consumed) and poisons.
  }

  EXPECT_TRUE(history.poisoned());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");

  ASSERT_TRUE(history.recover().ok());
  EXPECT_FALSE(history.poisoned());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

// recover() on a healthy history is a checked no-op returning kInvalidArgument.
TEST(CommandTest, RecoverWhenNotPoisonedInvalidArgument) {
  Project        project = make_project();
  CommandHistory history;
  EXPECT_EQ(history.recover().code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(history.poisoned());
}

// clear() rejects while a transaction is active, preserving the exact undo
// and redo stacks; after the abort the stacks are still exactly preserved.
TEST(CommandTest, ClearRejectedWhileTransactionActivePreservesStacks) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  // Undo stack: [A->B, B->C]; then undo so redo holds [C] while undo keeps
  // [A->B]: both stacks non-empty.
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "C"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 1u);
  EXPECT_EQ(project.find_node(node_id)->name(), "B");

  // Active provisional command: C -> D.
  auto tx = history.begin_transaction(
      std::make_unique<SetNodeNameCommand>(node_id, "D"), project);
  ASSERT_TRUE(tx.active());
  EXPECT_EQ(project.find_node(node_id)->name(), "D");

  // clear() is rejected while the transaction is active; stacks untouched.
  EXPECT_EQ(history.clear().code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // Abort restores the project and the exact stacks.
  ASSERT_TRUE(tx.abort().ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // Now clear() succeeds.
  ASSERT_TRUE(history.clear().ok());
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

// clear() rejects while poisoned, preserving both stacks exactly.
TEST(CommandTest, ClearRejectedWhilePoisonedPreservesStacks) {
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

  auto tx = history.begin_transaction(
      std::make_unique<TestCommand>("Fail", TestCommand::FailMode::kOnUndo),
      project);
  ASSERT_TRUE(tx.active());
  ASSERT_FALSE(tx.abort().ok());
  EXPECT_TRUE(history.poisoned());

  EXPECT_EQ(history.clear().code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);
}

// recovery cannot be directed at a different project: abort() and recover()
// operate only on the Project captured at begin_transaction(), so a second
// project is never touched.
TEST(CommandTest, RecoveryCannotBeDirectedAtDifferentProject) {
  Project project_a = make_project();
  NodeId  node_a    = project_a.add_node("A");
  Project project_b = make_project();
  NodeId  node_b    = project_b.add_node("B");

  CommandHistory history;
  auto           cmd = std::make_unique<AdversarialNameCommand>(
      node_a, "A1", AdversarialNameCommand::FailMode::kNever);
  cmd->set_fail_next_undo(true);

  auto tx = history.begin_transaction(std::move(cmd), project_a);
  ASSERT_TRUE(tx.active());
  EXPECT_EQ(project_a.find_node(node_a)->name(), "A1");

  EXPECT_FALSE(tx.abort().ok());
  EXPECT_TRUE(history.poisoned());

  // recover() (no project parameter) restores project_a only.
  ASSERT_TRUE(history.recover().ok());
  EXPECT_EQ(project_a.find_node(node_a)->name(), "A");
  EXPECT_EQ(project_b.find_node(node_b)->name(), "B");
  EXPECT_FALSE(history.poisoned());
}

// Moving a transaction leaves the moved-from object inert: destroying it must
// neither abort the command nor poison the history, while the moved-to
// transaction remains fully functional.
TEST(CommandTest, TransactionMoveMovedFromDestructionIsInert) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  auto make_moved = [&]() {
    auto tx = history.begin_transaction(
        std::make_unique<SetNodeNameCommand>(node_id, "B"), project);
    CommandHistory::Transaction moved(std::move(tx));
    // `tx` (moved-from) is destroyed here; its destructor must be inert.
    return moved;
  };

  CommandHistory::Transaction tx = make_moved();
  ASSERT_TRUE(tx.active());
  // The moved-from destruction did not abort: the mutation is still applied
  // and the history is not poisoned.
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
  EXPECT_FALSE(history.poisoned());

  ASSERT_TRUE(tx.abort().ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
  EXPECT_FALSE(history.poisoned());
}
