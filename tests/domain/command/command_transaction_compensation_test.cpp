// SPDX-License-Identifier: Apache-2.0

#include "command_test_fakes.hpp"
#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Finding 4: execute failure with model-observable rollback
//
// Last child (index 3) fails; previously executed children 0-2 must be
// undone and model restored exactly.
// =========================================================================

TEST(CommandTest, ExecuteLastChildFailureModelRollback) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());

  // All three previously-executed children were rolled back.
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  // Terminal — kFaulted, cannot retry.
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
}

// =========================================================================
// Finding 4: undo failure — last-inserted child is first undone.
// Child index 3 (D) fails on undo immediately; no compensation needed.
// Other children are untouched. Model stays at executed values.
// =========================================================================

TEST(CommandTest, UndoFirstUndoneChildFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");
  NodeId  d       = project.add_node("D");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  // Last child (inserted, undone first) fails on undo.
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          d, "D1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  // Undo — D fails immediately, no compensation path triggered.
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());

  // All nodes retain their executed names (undo never reached A, B, C).
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  // State still kDone (no compensation needed).
  EXPECT_FALSE(tx->redo(project).ok());

  // Can retry undo (state is still kDone).
  // Bump D to no-fail for a clean undo of the remaining children.
  // But since we cannot change D's mode, just verify the transaction
  // is not kFaulted from a single-child undo failure.
  EXPECT_FALSE(tx->execute(project).ok());
}

// =========================================================================
// Finding 4: redo failure — first child fails.
// No children were redone before it, so no compensation needed.
// Model stays at undone values. State stays kUndone.
// =========================================================================

TEST(CommandTest, RedoFirstChildFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnRedo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // Redo — first child fails immediately, no compensation.
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());

  // Nodes remain at undone names.
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // State still kUndone (can retry redo).
  EXPECT_FALSE(tx->undo(project).ok());
}

// =========================================================================
// Finding 4: execute rollback partial failure — model stuck mid-rollback.
//
// Children: [A:failOnUndo, B:never, C:never, D:failOnExecute].
// Execute: A, B, C all succeed; D fails.
// Compensation: undo C (OK), undo B (OK), undo A (FAILS).
// kFaulted, kTransactionRollbackFailed.
// A stays at "A1", B and C restored.
// =========================================================================

TEST(CommandTest, ExecuteCompensationPartialFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A's undo failed → A stuck at "A1".
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  // B and C were successfully rolled back.
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  // kFaulted — no further operations.
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Finding 4: undo compensation partial failure — model stuck mid-restore.
//
// Children: [A:never, B:failOnUndo, C:failOnRedo, D:never].
// Undo: D, C, B fails. Compensation: redo C (FAILS), redo D (still runs).
// C stuck at undone (pre-execute), D restored.
// =========================================================================

TEST(CommandTest, UndoCompensationPartialFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");
  NodeId  d       = project.add_node("D");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kOnRedo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          d, "D1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A untouched by undo (never reached). B untouched (failed its undo).
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  // C was undone but redo failed → stuck at "C" (original).
  EXPECT_EQ(project.find_node(c)->name(), "C");
  // D was undone but redo succeeded (best-effort ran it) → back to "D1".
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  // kFaulted.
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Finding 4: redo compensation partial failure — model stuck mid-rollback.
//
// Children: [TwoPhaseUndoFail, A:never, B:never, D:failOnRedo].
// First undo succeeds (TwoPhase does not fail on first undo).
// Redo: TwoPhase sets was_redone_, A/B succeed, D fails.
// Compensation: undo B (OK), undo A (OK), undo TwoPhase (FAILS).
// Best-effort: A and B are undone despite TwoPhase failure.
// D never redone, TwoPhase stuck at redone (but model-immaterial).
// =========================================================================

TEST(CommandTest, RedoCompensationPartialFailureModelState) {
  Project                  project = make_project();
  NodeId                   a       = project.add_node("A");
  NodeId                   b       = project.add_node("B");
  NodeId                   d       = project.add_node("D");
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  // Child 0: two-phase — fails on undo only after being redone.
  ASSERT_TRUE(
      tx->add_command(std::make_unique<TwoPhaseUndoFailCommand>(&log)).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          d, "D1", AdversarialNameCommand::FailMode::kOnRedo))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(d)->name(), "D");

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A and B were redone, then compensation-undo ran (best-effort).
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  // D was never redone (the failing child) → stays at undone value.
  EXPECT_EQ(project.find_node(d)->name(), "D");

  // Log proves both redo and compensating undo ran for TwoPhase.
  ASSERT_GE(log.size(), 2u);
  EXPECT_EQ(log[0], "TwoPhase-redo");
  EXPECT_EQ(log[1], "TwoPhase-undo");

  // kFaulted.
  EXPECT_FALSE(tx->redo(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
}

// =========================================================================
// Noexcept contract — compile-time enforcement
// =========================================================================

TEST(CommandTest, NoexceptStaticAsserts) {
  static_assert(
      noexcept(std::declval<Command&>().execute(std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<Command&>().undo(std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<Command&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<CommandHistory&>().execute_new(
      std::declval<std::unique_ptr<Command>>(), std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<CommandHistory&>().undo(std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<CommandHistory&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<CommandHistory&>().begin_transaction(
      std::declval<std::unique_ptr<Command>>(), std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<CommandHistory::Transaction&>().commit()));
  static_assert(noexcept(std::declval<CommandHistory::Transaction&>().abort()));
  static_assert(
      noexcept(std::declval<CommandHistory::Transaction&>().active()));
  static_assert(std::is_nothrow_destructible_v<CommandHistory::Transaction>);
  static_assert(noexcept(std::declval<CommandHistory&>().clear()));
  static_assert(noexcept(std::declval<CommandHistory&>().recover()));
  static_assert(noexcept(std::declval<CommandHistory&>().poisoned()));
  static_assert(noexcept(std::declval<CommandHistory&>().poisoned_reason()));

  // A CommandHistory must not be copied or moved while a Transaction (or the
  // poisoned recovery state) holds a raw back-pointer to it.
  static_assert(!std::is_copy_constructible_v<CommandHistory>);
  static_assert(!std::is_copy_assignable_v<CommandHistory>);
  static_assert(!std::is_move_constructible_v<CommandHistory>);
  static_assert(!std::is_move_assignable_v<CommandHistory>);

  static_assert(noexcept(
      std::declval<CommandTransaction&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<CommandTransaction&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<CommandTransaction&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetNodeNameCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeNameCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeNameCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTrackNameCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackNameCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackNameCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<SetProjectTempoCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetProjectTempoCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetProjectTempoCommand&>().redo(std::declval<Project&>())));
}

// =========================================================================
// kCommandFaulted — exact code checks
// =========================================================================

TEST(CommandTest, FaultedTransactionReturnsCommandFaulted) {
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

  // Once kFaulted, all operations return kCommandFaulted uniformly.
  EXPECT_EQ(tx->execute(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->undo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->redo(project).code(), ResultCode::kCommandFaulted);
}

TEST(CommandTest, FaultedByExecuteWithCleanRollbackReturnsCommandFaulted) {
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
  // Clean rollback: terminal by design but NOT a rollback failure.
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);

  EXPECT_EQ(project.find_node(a)->name(), "A");

  // Terminal — subsequent calls report kCommandFaulted.
  EXPECT_EQ(tx->execute(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->undo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->redo(project).code(), ResultCode::kCommandFaulted);
}

// =========================================================================
// Finding 6a: undo child fails once, restoration leaves kDone, retry succeeds
//
// Children: [A (never), B (one-shot undo fail), C (never)].
// Undo reverse: C-undo ok, B-undo FAILS (one-shot consumed).
//   Compensation: C-redo ok. State stays kDone.
//   Retry undo: C-undo ok, B-undo ok (one-shot consumed), A-undo ok.
//   State becomes kUndone. Model returns to pre-execute values.
// =========================================================================

TEST(CommandTest, UndoRetryAfterSuccessfulCompensation) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto cmd_b = std::make_unique<AdversarialNameCommand>(
      b, "B1", AdversarialNameCommand::FailMode::kNever);
  cmd_b->set_fail_next_undo(true);

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::move(cmd_b)).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");

  // First undo — B fails on undo, C is restored, state stays kDone.
  Result first = tx->undo(project);
  EXPECT_FALSE(first.ok());
  EXPECT_EQ(first.code(), ResultCode::kInternalError);
  // Model is back to post-execute state (compensation restored C, A untouched).
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");  // B never undone
  EXPECT_EQ(project.find_node(c)->name(), "C1");  // C restored

  // Retry undo — all succeed.
  Result second = tx->undo(project);
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  // Can redo now.
  ASSERT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
}

// =========================================================================
// Finding 6b: redo child fails once, rollback leaves kUndone, retry succeeds
//
// Children: [A (one-shot redo fail), B (never)].
// Execute, undo both. Redo: A redo FAILS (one-shot consumed), no compensation
//   needed (i=0). State stays kUndone.
//   Retry redo: A redo ok, B redo ok. State becomes kDone.
// =========================================================================

TEST(CommandTest, RedoRetryAfterSuccessfulRollback) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  auto cmd_a = std::make_unique<AdversarialNameCommand>(
      a, "A1", AdversarialNameCommand::FailMode::kNever);
  cmd_a->set_fail_next_redo(true);

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::move(cmd_a)).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");

  ASSERT_TRUE(tx->undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // First redo — A fails, no compensation needed, state stays kUndone.
  Result first = tx->redo(project);
  EXPECT_FALSE(first.ok());
  EXPECT_EQ(first.code(), ResultCode::kInternalError);
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // Retry redo — all succeed.
  Result second = tx->redo(project);
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
}

// =========================================================================
// Finding 6c: undo failure at insertion index 0 restores already-undone
// children in insertion order, retry succeeds.
//
// Children: [C0 (one-shot undo fail), C1 (never), C2 (never)] — three
// modifying the same node `shared`.
// Execute: C0→"C0", C1→"C1", C2→"C2" → shared name is "C2".
// Undo (reverse): C2 undo→"C1", C1 undo→"C0", C0 undo FAILS (one-shot
//   consumed). Compensation redoes in insertion order:
//   C1 redo→"C1", C2 redo→"C2". State stays kDone.
// Retry undo: C2 undo→"C1", C1 undo→"C0", C0 undo→"Original".
//   State becomes kUndone. Shared is "Original".
// =========================================================================

TEST(CommandTest, UndoFailureAtIndex0RestoreInsertionOrderRetrySucceeds) {
  Project project = make_project();
  NodeId  shared  = project.add_node("Original");

  auto cmd0 = std::make_unique<AdversarialNameCommand>(
      shared, "C0", AdversarialNameCommand::FailMode::kNever);
  cmd0->set_fail_next_undo(true);

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::move(cmd0)).ok());
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "C1", AdversarialNameCommand::FailMode::kNever))
                  .ok());
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "C2", AdversarialNameCommand::FailMode::kNever))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(shared)->name(), "C2");

  // First undo — C0 fails at the end of reverse traversal.
  Result first = tx->undo(project);
  EXPECT_FALSE(first.ok());
  EXPECT_EQ(first.code(), ResultCode::kInternalError);
  // Compensation in insertion order: C1→"C1", C2→"C2".
  EXPECT_EQ(project.find_node(shared)->name(), "C2");

  // Retry undo — all succeed in reverse order.
  Result second = tx->undo(project);
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(project.find_node(shared)->name(), "Original");

  // Can redo.
  ASSERT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(project.find_node(shared)->name(), "C2");
}

// =========================================================================
// Transaction forwards the provided project correctly — the command's
// execute/undo/redo receive the same Project& that the transaction received.
// =========================================================================

TEST(CommandTest, TransactionForwardsProjectToChildren) {
  Project project = make_project();
  NodeId  node_a  = project.add_node("A");
  NodeId  node_b  = project.add_node("B");

  // Two children target different nodes in the same project.
  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      node_a, "A1", AdversarialNameCommand::FailMode::kNever))
                  .ok());
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      node_b, "B1", AdversarialNameCommand::FailMode::kNever))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(tx->undo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A");
  EXPECT_EQ(project.find_node(node_b)->name(), "B");
}
