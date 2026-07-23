// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>

namespace graphscore {

class Project;

// Polymorphic reversible command protocol. Every mutation of a Project
// that must be undoable is represented as a Command. Concrete commands
// follow the three-phase lifecycle — execute, undo, redo — using the
// protected State enum declared by this base class.
//
// Commands store stable identifiers (UUIDs) and value snapshots, never raw
// pointers. A command that fails must leave the project unchanged relative
// to its own responsibility; grouped failure rollback is the
// CommandTransaction's concern.
//
// Exception contract: execute(), undo(), and redo() MUST NOT throw. Any
// internal allocation failure MUST be caught and translated to
// ResultCode::kOutOfMemory before the command returns. If a command's
// implementation cannot guarantee this (e.g. a third-party callback that
// may throw), the concrete command is responsible for catching all
// exceptions and translating them to ResultCode::kInternalError, while
// leaving the project in a defined state whenever possible.
class Command {
 public:
  virtual ~Command() = default;

  // Perform the edit and snapshot whatever old state is needed to undo it.
  // Only valid when the command is fresh; double-execute is rejected.
  virtual Result execute(Project& project) noexcept = 0;

  // Restore the project to the state it was in before execute() or the
  // most recent redo(). Only valid after a successful execute or redo.
  virtual Result undo(Project& project) noexcept = 0;

  // Re-apply the edit without re-snapshotting. Only valid after a
  // successful undo.
  virtual Result redo(Project& project) noexcept = 0;

 protected:
  // kFresh  — constructed but never successfully executed.
  // kDone   — executed or re-executed, ready to undo.
  // kUndone — successfully undone, ready to redo.
  // kFaulted — terminal; a recovery or rollback failure has left the
  //            command in an unrecoverable state. No further execute,
  //            undo, or redo is permitted.
  enum class State { kFresh, kDone, kUndone, kFaulted };
};

}  // namespace graphscore
