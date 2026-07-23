// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/command_transaction.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

Result CommandTransaction::add_command(std::unique_ptr<Command> command) {
  if (command == nullptr)
    return Result(ResultCode::kInvalidArgument);
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  if (children_.size() >= children_.max_size())
    return Result(ResultCode::kOutOfMemory);
  try {
    children_.reserve(children_.size() + 1);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  children_.push_back(std::move(command));
  return Result();
}

Result CommandTransaction::execute(Project& project) noexcept {
  if (state_ == State::kFaulted)
    return Result(ResultCode::kCommandFaulted);
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  const std::size_t count = children_.size();
  for (std::size_t i = 0; i < count; ++i) {
    const Result result = children_[i]->execute(project);
    if (!result.ok()) {
      // Roll back already-executed children 0..i-1 in reverse order.
      // Best-effort: attempt every compensator, not just the first.
      bool any_compensation_failed = false;
      for (std::size_t j = i; j > 0; --j) {
        const Result undo_result = children_[j - 1]->undo(project);
        if (!undo_result.ok())
          any_compensation_failed = true;
      }
      if (any_compensation_failed) {
        state_ = State::kFaulted;
        return Result(ResultCode::kTransactionRollbackFailed);
      }
      state_ = State::kFaulted;
      return result;
    }
  }

  state_ = State::kDone;
  return Result();
}

Result CommandTransaction::undo(Project& project) noexcept {
  if (state_ == State::kFaulted)
    return Result(ResultCode::kCommandFaulted);
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  const std::size_t count = children_.size();
  for (std::size_t i = count; i > 0; --i) {
    const Result result = children_[i - 1]->undo(project);
    if (!result.ok()) {
      // Restore already-undone children in insertion order.
      // Best-effort: attempt every compensator, not just the first.
      bool any_compensation_failed = false;
      for (std::size_t j = i; j < count; ++j) {
        const Result redo_result = children_[j]->redo(project);
        if (!redo_result.ok())
          any_compensation_failed = true;
      }
      if (any_compensation_failed) {
        state_ = State::kFaulted;
        return Result(ResultCode::kTransactionRollbackFailed);
      }
      // Restoration succeeded — keep state as kDone and report original
      // failure. The project is back where it was before undo() started.
      return result;
    }
  }

  state_ = State::kUndone;
  return Result();
}

Result CommandTransaction::redo(Project& project) noexcept {
  if (state_ == State::kFaulted)
    return Result(ResultCode::kCommandFaulted);
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  const std::size_t count = children_.size();
  for (std::size_t i = 0; i < count; ++i) {
    const Result result = children_[i]->redo(project);
    if (!result.ok()) {
      // Roll back already-redone children 0..i-1 in reverse order.
      // Best-effort: attempt every compensator, not just the first.
      bool any_compensation_failed = false;
      for (std::size_t j = i; j > 0; --j) {
        const Result undo_result = children_[j - 1]->undo(project);
        if (!undo_result.ok())
          any_compensation_failed = true;
      }
      if (any_compensation_failed) {
        state_ = State::kFaulted;
        return Result(ResultCode::kTransactionRollbackFailed);
      }
      // Restoration succeeded — keep state as kUndone and report original
      // failure. The project is back where it was before redo() started.
      return result;
    }
  }

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
