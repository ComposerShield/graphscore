// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

class Project;

// A command that owns an ordered sequence of child commands and executes
// them as one atomic unit. Intended for multi-measure operations and drag
// gestures. Children are stored in a vector and executed in insertion order.
//
// State machine:
//   kFresh → (execute succeeds) → kDone
//   kDone  → (undo succeeds)    → kUndone
//   kUndone → (redo succeeds)   → kDone
//
//   Any execute, undo, or redo failure where compensation (rollback) also
//   fails transitions to kFaulted, a terminal unrecoverable state.
//
//   execute failure WITH successful rollback: transitions to kFaulted.
//   The project is back in its pre-transaction state but the transaction
//   itself cannot be retried — its children are now in kUndone (after
//   rollback undo) while the transaction demands kFresh for re-execution.
//   This is deliberate: a transaction whose execute has already been
//   attempted must be discarded and replaced with a fresh one.
//
// Failure semantics:
//   execute() — executes children in insertion order. If any child fails:
//     1. Rolls back already-executed children (undo in reverse order).
//     2. If rollback succeeds: state → kFaulted (terminal), returns the
//        original child's failure.
//     3. If rollback fails (any compensating undo fails): state → kFaulted,
//        returns ResultCode::kTransactionRollbackFailed. The project is in
//        an unrecoverable partially-rolled-back state.
//   undo() — undoes children in reverse order. If any child fails:
//     1. Re-executes already-undone children (redo in insertion order) to
//        restore the pre-undo project state.
//     2. If restoration succeeds: state stays kDone, returns the original
//        child's failure.
//     3. If restoration fails: state → kFaulted, returns
//        kTransactionRollbackFailed. The project is in an unrecoverable
//        partially-undone / partially-restored state.
//   redo() — redoes children in insertion order. If any child fails:
//     1. Undoes already-redone children (undo in reverse order) to restore
//        the pre-redo project state.
//     2. If restoration succeeds: state stays kUndone, returns the original
//        child's failure.
//     3. If restoration fails: state → kFaulted, returns
//        kTransactionRollbackFailed. The project is in an unrecoverable
//        partially-redone / partially-undone state.
//
// An empty transaction (zero children) is valid: execute/undo/redo succeed
// with no model change.
//
// What is recoverable: a single child failing during execute, undo, or redo
//   when all compensating undo/redo calls succeed. The project returns to its
//   exact pre-operation state and the caller receives the child's failure.
// What is NOT recoverable: a compensation (rollback) failure. The project
//   may retain partial mutations from children whose compensating undo/redo
//   failed. The transaction enters kFaulted and must be discarded.
class CommandTransaction : public Command {
 public:
  CommandTransaction() = default;

  // Adds a child command. Children are owned by the transaction; calling
  // code must not retain the unique_ptr after this call.
  //
  // Returns kInvalidArgument if `command` is null or if the transaction
  // is not in the kFresh (buildable) state.
  // Returns kOutOfMemory if reserving child storage fails.
  // Children can only be added while the transaction is freshly constructed
  // (before any execute/undo/redo call).
  [[nodiscard]] Result add_command(std::unique_ptr<Command> command);

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

  [[nodiscard]] std::size_t child_count() const noexcept {
    return children_.size();
  }

 private:
  std::vector<std::unique_ptr<Command>> children_;
  State                                 state_ = State::kFresh;
};

}  // namespace graphscore
