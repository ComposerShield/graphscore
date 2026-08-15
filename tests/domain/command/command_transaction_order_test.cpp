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
// CommandTransaction — execute / undo / redo order
// =========================================================================

TEST(CommandTest, TransactionExecuteUndoRedoOrder) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(b, "B1")).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(c, "C1")).ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");

  ASSERT_TRUE(tx->undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  ASSERT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
}

// =========================================================================
// Transaction failure rollback (execute)
// =========================================================================

TEST(CommandTest, TransactionExecuteMiddleFailureRollsBack) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(b, "B1")).ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());

  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
}

TEST(CommandTest, TransactionExecuteFirstChildFailureNoModelChange) {
  Project project = make_project();

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Transaction failure rollback (redo)
// =========================================================================

TEST(CommandTest, TransactionRedoMiddleFailureRollsBack) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnRedo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kNever, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());

  ASSERT_EQ(log.size(), 3u);
  EXPECT_EQ(log[0], "A-redo");
  EXPECT_EQ(log[1], "B-redo");
  EXPECT_EQ(log[2], "A-undo");
}

// =========================================================================
// Transaction failure rollback (undo)
// =========================================================================

TEST(CommandTest, TransactionUndoMiddleFailureRollsBack) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kNever, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());

  log.clear();
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());

  ASSERT_EQ(log.size(), 3u);
  EXPECT_EQ(log[0], "C-undo");
  EXPECT_EQ(log[1], "B-undo");
  EXPECT_EQ(log[2], "C-redo");
}

// =========================================================================
// Empty transaction
// =========================================================================

TEST(CommandTest, EmptyTransactionSucceedsOnAllOps) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  EXPECT_TRUE(tx->execute(project).ok());
  EXPECT_TRUE(tx->undo(project).ok());
  EXPECT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(tx->child_count(), 0u);
}

// =========================================================================
// State misuse edges
// =========================================================================

TEST(CommandTest, TransactionDoubleExecuteRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_FALSE(tx->execute(project).ok());
}

TEST(CommandTest, TransactionUndoWithoutExecuteRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  EXPECT_FALSE(tx->undo(project).ok());
}

TEST(CommandTest, TransactionRedoWithoutUndoRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Transaction — null child and post-build add_command rejection
// =========================================================================

TEST(CommandTest, TransactionAddNullChildReturnsInvalidArgument) {
  auto tx = std::make_unique<CommandTransaction>();

  Result result = tx->add_command(nullptr);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tx->child_count(), 0u);
}

TEST(CommandTest, TransactionAddCommandAfterExecuteRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  ASSERT_TRUE(tx->execute(project).ok());

  Result result = tx->add_command(std::make_unique<TestCommand>("X"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, TransactionAddCommandAfterExecuteFailureKfaulted) {
  Project project = make_project();

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());
  ASSERT_FALSE(tx->execute(project).ok());

  Result result = tx->add_command(std::make_unique<TestCommand>("X"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, TransactionAddCommandAfterUndoRejected) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  Result result = tx->add_command(std::make_unique<TestCommand>("X"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Transaction with real commands at history level
// =========================================================================

TEST(CommandTest, TransactionInHistoryRoundTrip) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  CommandHistory history;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(b, "B1")).ok());

  ASSERT_TRUE(history.execute_new(std::move(tx), project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
}

// =========================================================================
// Transaction execute failure rollback — verify model rollback
// =========================================================================

TEST(CommandTest, TransactionExecuteRollbackPreservesExactPreState) {
  Project    project  = make_project();
  NodeId     node_id  = project.add_node("Original");
  const auto track_id = project.add_track("Track1", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(node_id, "Node1"))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetTrackNameCommand>(
                                  *track_id, "Track1-renamed"))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());

  EXPECT_EQ(project.find_node(node_id)->name(), "Original");
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "Track1");
}

// =========================================================================
// Adversarial: execute-failure + rollback undo failure → kFaulted
// (model-observable: child 0 mutates A, undo fails, A stuck at new value)
// =========================================================================

TEST(CommandTest, ExecuteFailureRollbackUndoFailModelObservable) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // Child 0 executed (A→A1), then compensating undo failed → A stuck
  EXPECT_EQ(project.find_node(a)->name(), "A1");

  // kFaulted — no further operations
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
  Result add_result = tx->add_command(std::make_unique<TestCommand>("Z"));
  EXPECT_FALSE(add_result.ok());
}

// =========================================================================
// Adversarial: execute failure + successful rollback → kFaulted (no retry)
// =========================================================================

TEST(CommandTest, ExecuteFailureSuccessfulRollbackIsFaulted) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);

  // Rollback successful: A is back to "A"
  EXPECT_EQ(project.find_node(a)->name(), "A");

  // Terminal — cannot retry
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());

  Result add_result = tx->add_command(std::make_unique<TestCommand>("Z"));
  EXPECT_FALSE(add_result.ok());
}

// =========================================================================
// Adversarial: undo-failure + compensation redo failure → kFaulted
//
// Three children: [A:never, B:failOnUndo, C:failOnRedo]
// Undo (reverse order):
//   i=3: undo C → kUndone fine.
//   i=2: undo B → FAILS.
//   Compensation: redo C → kUndone, kOnRedo → FAILS → kFaulted.
// =========================================================================

TEST(CommandTest, UndoFailureCompensationRedoFailsFaulted) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kOnRedo, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());

  log.clear();
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  ASSERT_GE(log.size(), 3u);
  EXPECT_EQ(log[0], "C-undo");
  EXPECT_EQ(log[1], "B-undo");
  EXPECT_EQ(log[2], "C-redo");

  // kFaulted
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
  EXPECT_FALSE(tx->execute(project).ok());
}

// =========================================================================
// Adversarial: redo-failure + compensation undo failure → kFaulted
//
// Children: [TwoPhaseUndoFail, FailOnRedo]
// Execute, undo both. Redo: TwoPhase redoes fine (sets was_redone_=true).
//   Fail redo fails. Compensation: undo TwoPhase → fails (was_redone_).
// =========================================================================

TEST(CommandTest, RedoFailureCompensationUndoFailsFaulted) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<TwoPhaseUndoFailCommand>(&log)).ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "Fail", TestCommand::FailMode::kOnRedo, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  ASSERT_GE(log.size(), 3u);
  EXPECT_EQ(log[0], "TwoPhase-redo");
  EXPECT_EQ(log[1], "Fail-redo");
  EXPECT_EQ(log[2], "TwoPhase-undo");

  // kFaulted
  EXPECT_FALSE(tx->redo(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->execute(project).ok());
}

// =========================================================================
// Transaction — first child execute failure → kFaulted
// =========================================================================

TEST(CommandTest, TransactionFirstChildExecuteFailureTerminal) {
  Project project = make_project();

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Stateful single-child transaction: undo failure preserves model state
// =========================================================================

TEST(CommandTest, StatefulUndoFailurePreservesExecutedState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");

  // Undo fails — model stays at A1
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");

  // State is still kDone (single child, no compensation path triggered)
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Finding 1: undo-failure restoration insertion order (model-observable)
//
// Four children, two modifying the same node. The middle child (B) fails on
// undo. Already-undone children (C, D) must be redone in insertion order
// (C then D). If the restoration loop used reverse insertion order (D then
// C), the shared node would end up with C's value instead of D's.
// =========================================================================

TEST(CommandTest, UndoRestorationInsertionOrder) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  shared  = project.add_node("Shared");

  auto tx = std::make_unique<CommandTransaction>();
  // Child 0 — never fails, modifies node a
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  // Child 1 — fails on undo, modifies node b (model-observable: stays "B1")
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  // Child 2 — never fails, first modifier of `shared`
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "C1", AdversarialNameCommand::FailMode::kNever))
                  .ok());
  // Child 3 — never fails, second modifier of `shared`
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "D1", AdversarialNameCommand::FailMode::kNever))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(shared)->name(), "D1");

  // Undo fails at child 1 (B). Children 3 (D) and 2 (C) were already
  // undone. Compensation must redo C then D (insertion order).
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());

  // If insertion order was used: redo C → "C1", then redo D → "D1".
  // If reverse order was used: redo D → "D1", then redo C → "C1".
  EXPECT_EQ(project.find_node(shared)->name(), "D1");
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
}

// =========================================================================
// Finding 2: best-effort compensation — execute
//
// Children: [A:never, B:failOnUndo, C:never, D:failOnExecute]
// D fails on execute. Compensation undoes C, B, A in reverse order. B's
// undo fails but A's undo must STILL run. Result: kTransactionRollbackFailed.
// =========================================================================

TEST(CommandTest, ExecuteBestEffortCompensationContinuesAfterFailure) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "D", TestCommand::FailMode::kOnExecute, &log))
                  .ok());

  log.clear();
  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // All three compensators must have been attempted: C-undo, B-undo, A-undo.
  ASSERT_GE(log.size(), 5u);
  EXPECT_EQ(log[0], "A-execute");
  EXPECT_EQ(log[1], "B-execute");
  EXPECT_EQ(log[2], "C-execute");
  EXPECT_EQ(log[3], "D-execute");
  EXPECT_EQ(log[4], "C-undo");
  EXPECT_EQ(log[5], "B-undo");
  EXPECT_EQ(log[6], "A-undo");
  EXPECT_EQ(log.size(), 7u);
}

// =========================================================================
// Finding 2: best-effort compensation — undo
//
// Children: [A:never, B:failOnUndo, C:failOnRedo, D:never]
// B fails on undo. Compensation redoes C (fails) then D (still runs).
// Result: kTransactionRollbackFailed.
// =========================================================================

TEST(CommandTest, UndoBestEffortCompensationContinuesAfterFailure) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kOnRedo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "D", TestCommand::FailMode::kNever, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());

  log.clear();
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // D-undo, C-undo, B-undo (fails), C-redo (fails), D-redo (still runs).
  ASSERT_GE(log.size(), 5u);
  EXPECT_EQ(log[0], "D-undo");
  EXPECT_EQ(log[1], "C-undo");
  EXPECT_EQ(log[2], "B-undo");
  EXPECT_EQ(log[3], "C-redo");
  EXPECT_EQ(log[4], "D-redo");
  EXPECT_EQ(log.size(), 5u);
}

// =========================================================================
// Finding 2: best-effort compensation — redo
//
// Children: [A:never, TwoPhaseUndoFail, B:never, D:failOnRedo].
// First undo succeeds (TwoPhase does not fail on first undo).
// D fails on redo. Compensation undoes B, TwoPhase (FAILS), A in
// reverse order. B's undo and A's undo must still run.
// Result: kTransactionRollbackFailed.
// =========================================================================

TEST(CommandTest, RedoBestEffortCompensationContinuesAfterFailure) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<TwoPhaseUndoFailCommand>(&log)).ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "D", TestCommand::FailMode::kOnRedo, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A-redo, TwoPhase-redo, B-redo, D-redo (fails),
  // B-undo, TwoPhase-undo (fails, but best-effort continues),
  // A-undo (still runs).
  ASSERT_GE(log.size(), 7u);
  EXPECT_EQ(log[0], "A-redo");
  EXPECT_EQ(log[1], "TwoPhase-redo");
  EXPECT_EQ(log[2], "B-redo");
  EXPECT_EQ(log[3], "D-redo");
  EXPECT_EQ(log[4], "B-undo");
  EXPECT_EQ(log[5], "TwoPhase-undo");
  EXPECT_EQ(log[6], "A-undo");
  EXPECT_EQ(log.size(), 7u);
}
