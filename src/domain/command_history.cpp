// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/command_history.hpp>

#include <cstddef>
#include <memory>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

Result CommandHistory::execute_new(std::unique_ptr<Command> command,
                                   Project&                 project) noexcept {
  if (poisoned_)
    return Result(ResultCode::kCommandFaulted);
  if (transaction_active_)
    return Result(ResultCode::kInvalidArgument);
  if (command == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (undo_stack_.size() >= undo_stack_.max_size())
    return Result(ResultCode::kOutOfMemory);

  try {
    // Reserve undo-stack capacity before any model mutation, so a successful
    // execute() is always recordable. With capacity reserved the later
    // push_back cannot reallocate, so it cannot throw; the catch-all upholds
    // this function's noexcept contract even so, since the analyzer cannot
    // prove the container will not reallocate.
    undo_stack_.reserve(undo_stack_.size() + 1);

    const Result result = command->execute(project);
    if (!result.ok())
      return result;

    undo_stack_.push_back(std::move(command));
    redo_stack_.clear();
    return Result();
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

CommandHistory::Transaction CommandHistory::begin_transaction(
    std::unique_ptr<Command> command, Project& project) noexcept {
  if (poisoned_)
    return Transaction{};
  if (transaction_active_)
    return Transaction{};
  if (command == nullptr)
    return Transaction{};

  if (undo_stack_.size() >= undo_stack_.max_size())
    return Transaction{};

  try {
    // Reserve the undo slot this command will occupy on commit() BEFORE
    // executing, so a successful execute() is always recordable. With the
    // active-transaction guard blocking every interleaved stack mutation, the
    // later commit() push_back cannot reallocate and therefore cannot throw.
    undo_stack_.reserve(undo_stack_.size() + 1);

    const Result result = command->execute(project);
    if (!result.ok())
      return Transaction{};
  } catch (...) {
    return Transaction{};
  }

  transaction_active_ = true;
  return Transaction{std::move(command), this, project};
}

CommandHistory::Transaction::Transaction(std::unique_ptr<Command> command,
                                         CommandHistory*          history,
                                         Project& project) noexcept
    : command_(std::move(command)), history_(history), project_(&project) {}

CommandHistory::Transaction::~Transaction() {
  if (!active() || history_ == nullptr)
    return;
  // The transaction was abandoned without commit() or abort(): undo the
  // executed command. The Result is unobservable from a destructor; on
  // failure the history is poisoned (command + Project retained) so
  // recover() can retry — a half-rolled-back state is never silently
  // discarded, and subsequent history operations cannot pretend the model is
  // consistent.
  const Result result = command_->undo(*project_);
  if (result.ok()) {
    history_->transaction_active_ = false;
    command_.reset();
  } else {
    history_->poison(result.code(), std::move(command_), *project_);
  }
}

Result CommandHistory::Transaction::commit() noexcept {
  if (!active() || history_ == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    // begin_transaction() reserved one undo slot before executing and the
    // active-transaction guard blocks any interleaved stack mutation, so this
    // push_back cannot reallocate and therefore cannot throw; the catch-all
    // upholds the noexcept contract for the analyzer, which cannot prove it.
    history_->undo_stack_.push_back(std::move(command_));
    history_->redo_stack_.clear();
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }

  history_->transaction_active_ = false;
  return Result();
}

Result CommandHistory::Transaction::abort() noexcept {
  if (!active() || history_ == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result result = command_->undo(*project_);
  if (!result.ok()) {
    // The rollback failed: poison the history (retaining the command and its
    // exact Project association) and report the failure. The transaction
    // becomes inactive; recover() retries the rollback against the retained
    // Project.
    history_->poison(result.code(), std::move(command_), *project_);
    return result;
  }

  history_->transaction_active_ = false;
  command_.reset();
  return Result();
}

Result CommandHistory::undo(Project& project) noexcept {
  if (poisoned_)
    return Result(ResultCode::kCommandFaulted);
  if (transaction_active_)
    return Result(ResultCode::kInvalidArgument);
  if (undo_stack_.empty())
    return Result();

  if (redo_stack_.size() >= redo_stack_.max_size())
    return Result(ResultCode::kOutOfMemory);

  try {
    // Reserve redo-stack capacity before mutation. The reserved redo-stack
    // slot and the slot freed by pop_back below make both push_backs
    // non-throwing; the catch-all upholds the noexcept contract regardless,
    // since the analyzer cannot prove the containers will not reallocate.
    redo_stack_.reserve(redo_stack_.size() + 1);

    auto command = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    const Result result = command->undo(project);
    if (!result.ok()) {
      undo_stack_.push_back(std::move(command));
      return result;
    }

    redo_stack_.push_back(std::move(command));
    return Result();
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result CommandHistory::redo(Project& project) noexcept {
  if (poisoned_)
    return Result(ResultCode::kCommandFaulted);
  if (transaction_active_)
    return Result(ResultCode::kInvalidArgument);
  if (redo_stack_.empty())
    return Result();

  if (undo_stack_.size() >= undo_stack_.max_size())
    return Result(ResultCode::kOutOfMemory);

  try {
    // Reserve undo-stack capacity before mutation. The reserved undo-stack
    // slot and the slot freed by pop_back below make both push_backs
    // non-throwing; the catch-all upholds the noexcept contract regardless,
    // since the analyzer cannot prove the containers will not reallocate.
    undo_stack_.reserve(undo_stack_.size() + 1);

    auto command = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    const Result result = command->redo(project);
    if (!result.ok()) {
      redo_stack_.push_back(std::move(command));
      return result;
    }

    undo_stack_.push_back(std::move(command));
    return Result();
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result CommandHistory::clear() noexcept {
  if (poisoned_)
    return Result(ResultCode::kCommandFaulted);
  if (transaction_active_)
    return Result(ResultCode::kInvalidArgument);
  undo_stack_.clear();
  redo_stack_.clear();
  return Result();
}

Result CommandHistory::recover() noexcept {
  if (!poisoned_)
    return Result(ResultCode::kInvalidArgument);

  const Result result = poisoned_command_->undo(*poisoned_project_);
  if (!result.ok())
    return result;

  poisoned_command_.reset();
  poisoned_project_ = nullptr;
  poisoned_reason_  = ResultCode::kSuccess;
  poisoned_         = false;
  return Result();
}

void CommandHistory::poison(ResultCode reason, std::unique_ptr<Command> command,
                            Project& project) noexcept {
  poisoned_command_   = std::move(command);
  poisoned_project_   = &project;
  poisoned_reason_    = reason;
  poisoned_           = true;
  transaction_active_ = false;
}

}  // namespace graphscore
