// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

class Project;

// Standalone undo/redo service. Owns the command history; does not live
// inside Project. Every mutation intended to be undoable flows through
// execute_new() (or a Transaction — see below).
//
// Allocation safety: every mutating operation (execute_new, undo, redo,
// begin_transaction) reserves capacity in the destination stack BEFORE
// calling into the command. If the reserve fails the operation returns
// ResultCode::kOutOfMemory without touching the project. The command is
// moved into already-reserved storage only after a successful result.
//
// Undo/redo failure semantics: if a command's undo() or redo() signals
// failure, the history leaves that command on its original stack and
// returns the failure result. No command is silently discarded. The model
// may be in an inconsistent state after a mid-group failure — that is
// CommandTransaction's concern, and the history will faithfully report it.
//
// Lifetime: a CommandHistory must outlive every Transaction it creates, and
// the Project a transaction mutates must outlive both. Transactions and the
// poisoned recovery state retain raw Project* back-pointers (never owned),
// so a Project that dies while a transaction is active — or while the
// history is poisoned — is a use-after-free. This is safe in the writer app
// because the handler declares the Project before the CommandHistory (so the
// history is destroyed first). CommandHistory is non-copyable and
// non-movable for exactly this reason: copying or moving it would leave
// every outstanding Transaction's back-pointer dangling.
//
// Poisoned state: when the rollback of a transaction cannot complete —
// abort() fails, or an abandoned transaction's destructor fails to undo —
// the history retains the provisional command and its exact Project
// association in a durable poisoned slot and rejects every operation
// (execute_new, undo, redo, clear, begin_transaction) with
// ResultCode::kCommandFaulted until recover() succeeds. recover() retries
// the retained command's undo() against the retained Project, so a one-shot
// rollback failure recovers coherently while a persistent failure keeps the
// history poisoned. No destructor ever silently discards a failed rollback.
class CommandHistory {
 public:
  CommandHistory()                                 = default;
  CommandHistory(const CommandHistory&)            = delete;
  CommandHistory& operator=(const CommandHistory&) = delete;
  CommandHistory(CommandHistory&&)                 = delete;
  CommandHistory& operator=(CommandHistory&&)      = delete;

  // A provisional command execution for a transactional mutation whose
  // visible publication is a separate, fallible step after the domain edit
  // (e.g. re-laying out and re-publishing a notation surface after a
  // notehead move). execute_new() clears the redo stack on success, so it
  // cannot represent "execute now, but let me restore the exact prior
  // history if publication fails"; a Transaction can.
  //
  // begin_transaction() executes `command` against `project` and, on
  // success, holds it in the returned Transaction WITHOUT touching the redo
  // stack, so the exact prior history — including a non-empty redo stack —
  // survives until the caller decides the outcome:
  //   commit() moves the command onto the undo stack and clears redo,
  //     exactly what execute_new()'s success path would have done.
  //   abort() undoes the command against the captured project and discards
  //     it, leaving both stacks exactly as they were before
  //     begin_transaction().
  //
  // Only one transaction may be in progress at a time. While one is active,
  // execute_new(), undo(), redo(), clear(), and a second begin_transaction()
  // all fail with kInvalidArgument, so the reserved undo capacity and the
  // provisional command can never be disturbed by an interleaved operation.
  //
  // This is a narrow, well-specified staging API. It never copies or exposes
  // the stacks' contents.
  class Transaction {
   public:
    Transaction()                              = default;
    Transaction(const Transaction&)            = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) noexcept        = default;
    Transaction& operator=(Transaction&&)      = delete;

    // Aborts (undoes) the command on destruction if it was never committed
    // or aborted. Undo is noexcept, but its Result is unobservable from a
    // destructor; if that rollback fails the history is POISONED — the
    // command and its Project association are retained for recover() —
    // rather than silently discarded. This is a safety net for the
    // abandoned-transaction programming error, not a replacement for an
    // explicit abort().
    ~Transaction();

    // True while the command has been executed but not yet committed or
    // aborted.
    [[nodiscard]] bool active() const noexcept { return command_ != nullptr; }

    // Record the command and clear redo (execute_new's success path). Fails
    // with kInvalidArgument when inactive. Cannot fail otherwise: capacity
    // was reserved before execute, and the active-transaction guard blocks
    // interleaved stack mutation.
    [[nodiscard]] Result commit() noexcept;

    // Undo the command and discard it, restoring both stacks to their exact
    // pre-begin_transaction() state. Operates only on the Project captured
    // at begin_transaction() — there is no Project parameter, so the rollback
    // can never be directed at a different project. Fails with
    // kInvalidArgument when inactive. On an undo failure the history is
    // poisoned (the command and Project are retained) and the failure is
    // returned; recover() retries the rollback against that Project.
    [[nodiscard]] Result abort() noexcept;

   private:
    friend class CommandHistory;

    Transaction(std::unique_ptr<Command> command, CommandHistory* history,
                Project& project) noexcept;

    std::unique_ptr<Command> command_;
    CommandHistory*          history_ = nullptr;
    Project*                 project_ = nullptr;
  };

  // Execute a new command provisionally (see Transaction). Returns an
  // inactive Transaction (active() == false) — with neither stack nor
  // project changed — when `command` is null, when a transaction is already
  // in progress, when the history is poisoned, when reserving undo capacity
  // fails, or when execute() fails.
  [[nodiscard]] Transaction begin_transaction(std::unique_ptr<Command> command,
                                              Project& project) noexcept;

  // Execute a new command, recording it on success.
  //   Returns kInvalidArgument if `command` is null or a transaction is
  //     already in progress.
  //   Returns kCommandFaulted while the history is poisoned.
  //   Returns kOutOfMemory if reserving undo-stack capacity fails before
  //     any model mutation.
  //   On success the command is pushed to the undo stack and the redo
  //   stack is cleared (any future-redo path is now stale).
  //   On failure the command is destroyed, both stacks are unchanged,
  //   and the model is unchanged.
  [[nodiscard]] Result execute_new(std::unique_ptr<Command> command,
                                   Project&                 project) noexcept;

  // Undo the most recent command. Safe to call with an empty undo stack
  // (returns success, no-op). On failure the command stays in the undo
  // stack. Returns kOutOfMemory if reserving redo-stack capacity fails
  // before the undo call. Returns kInvalidArgument while a transaction is
  // in progress, kCommandFaulted while poisoned.
  [[nodiscard]] Result undo(Project& project) noexcept;

  // Redo the most recently undone command. Safe to call with an empty redo
  // stack (returns success, no-op). On failure the command stays in the
  // redo stack. Returns kOutOfMemory if reserving undo-stack capacity fails
  // before the redo call. Returns kInvalidArgument while a transaction is
  // in progress, kCommandFaulted while poisoned.
  [[nodiscard]] Result redo(Project& project) noexcept;

  // Discards all undo and redo history without undoing or redoing anything.
  // A checked operation: returns kInvalidArgument while a transaction is
  // active and kCommandFaulted while the history is poisoned, preserving
  // both stacks exactly in either rejection case.
  [[nodiscard]] Result clear() noexcept;

  // Retries the rollback of the retained provisional command against the
  // retained Project after a failed rollback poisoned the history. On
  // success the command is destroyed, the poisoned state is cleared, and the
  // history is usable again with both stacks exactly as they were before the
  // failed transaction began. On failure the history stays poisoned (the
  // command and Project are retained) and the failure is returned. Returns
  // kInvalidArgument when the history is not poisoned.
  [[nodiscard]] Result recover() noexcept;

  // True while a failed rollback has left the history poisoned; every
  // mutating operation is rejected with kCommandFaulted until recover()
  // succeeds.
  [[nodiscard]] bool poisoned() const noexcept { return poisoned_; }

  // The ResultCode of the rollback failure that poisoned the history
  // (kSuccess when not poisoned).
  [[nodiscard]] ResultCode poisoned_reason() const noexcept {
    return poisoned_reason_;
  }

  [[nodiscard]] bool can_undo() const noexcept { return !undo_stack_.empty(); }

  [[nodiscard]] bool can_redo() const noexcept { return !redo_stack_.empty(); }

  [[nodiscard]] std::size_t undo_stack_size() const noexcept {
    return undo_stack_.size();
  }

  [[nodiscard]] std::size_t redo_stack_size() const noexcept {
    return redo_stack_.size();
  }

 private:
  // Moves a failed provisional command and its Project association into the
  // durable poisoned slot and clears the active-transaction guard, so a
  // failed rollback is never silently discarded and recover() can retry it.
  void poison(ResultCode reason, std::unique_ptr<Command> command,
              Project& project) noexcept;

  std::vector<std::unique_ptr<Command>> undo_stack_;
  std::vector<std::unique_ptr<Command>> redo_stack_;
  bool                                  transaction_active_ = false;
  bool                                  poisoned_           = false;
  std::unique_ptr<Command>              poisoned_command_;
  Project*                              poisoned_project_ = nullptr;
  ResultCode                            poisoned_reason_ = ResultCode::kSuccess;
};

}  // namespace graphscore
