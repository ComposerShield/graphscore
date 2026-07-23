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
// execute_new().
//
// Allocation safety: every mutating operation (execute_new, undo, redo)
// reserves capacity in the destination stack BEFORE calling into the
// command. If the reserve fails the operation returns
// ResultCode::kOutOfMemory without touching the project. The command is
// moved into already-reserved storage only after a successful result.
//
// Undo/redo failure semantics: if a command's undo() or redo() signals
// failure, the history leaves that command on its original stack and
// returns the failure result. No command is silently discarded. The model
// may be in an inconsistent state after a mid-group failure — that is
// CommandTransaction's concern, and the history will faithfully report it.
class CommandHistory {
 public:
  CommandHistory() = default;

  // Execute a new command, recording it on success.
  //   Returns kInvalidArgument if `command` is null.
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
  // before the undo call.
  [[nodiscard]] Result undo(Project& project) noexcept;

  // Redo the most recently undone command. Safe to call with an empty redo
  // stack (returns success, no-op). On failure the command stays in the
  // redo stack. Returns kOutOfMemory if reserving undo-stack capacity fails
  // before the redo call.
  [[nodiscard]] Result redo(Project& project) noexcept;

  // Discards all undo and redo history without undoing or redoing anything.
  void clear() noexcept;

  [[nodiscard]] bool can_undo() const noexcept { return !undo_stack_.empty(); }

  [[nodiscard]] bool can_redo() const noexcept { return !redo_stack_.empty(); }

  [[nodiscard]] std::size_t undo_stack_size() const noexcept {
    return undo_stack_.size();
  }

  [[nodiscard]] std::size_t redo_stack_size() const noexcept {
    return redo_stack_.size();
  }

 private:
  std::vector<std::unique_ptr<Command>> undo_stack_;
  std::vector<std::unique_ptr<Command>> redo_stack_;
};

}  // namespace graphscore
