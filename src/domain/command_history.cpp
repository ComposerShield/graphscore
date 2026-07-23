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

Result CommandHistory::undo(Project& project) noexcept {
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

void CommandHistory::clear() noexcept {
  undo_stack_.clear();
  redo_stack_.clear();
}

}  // namespace graphscore
