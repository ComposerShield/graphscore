// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/command_history.hpp>

#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

Result CommandHistory::execute_new(std::unique_ptr<Command> command,
                                   Project&                 project) noexcept {
  if (command == nullptr)
    return Result(ResultCode::kInvalidArgument);

  // Reserve undo-stack capacity before any model mutation.
  if (undo_stack_.size() >= undo_stack_.max_size())
    return Result(ResultCode::kOutOfMemory);
  try {
    undo_stack_.reserve(undo_stack_.size() + 1);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const Result result = command->execute(project);
  if (!result.ok())
    return result;

  undo_stack_.push_back(std::move(command));
  redo_stack_.clear();
  return Result();
}

Result CommandHistory::undo(Project& project) noexcept {
  if (undo_stack_.empty())
    return Result();

  // Reserve redo-stack capacity before model mutation.
  if (redo_stack_.size() >= redo_stack_.max_size())
    return Result(ResultCode::kOutOfMemory);
  try {
    redo_stack_.reserve(redo_stack_.size() + 1);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  auto command = std::move(undo_stack_.back());
  undo_stack_.pop_back();

  const Result result = command->undo(project);
  if (!result.ok()) {
    undo_stack_.push_back(std::move(command));
    return result;
  }

  redo_stack_.push_back(std::move(command));
  return Result();
}

Result CommandHistory::redo(Project& project) noexcept {
  if (redo_stack_.empty())
    return Result();

  // Reserve undo-stack capacity before model mutation.
  if (undo_stack_.size() >= undo_stack_.max_size())
    return Result(ResultCode::kOutOfMemory);
  try {
    undo_stack_.reserve(undo_stack_.size() + 1);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  auto command = std::move(redo_stack_.back());
  redo_stack_.pop_back();

  const Result result = command->redo(project);
  if (!result.ok()) {
    redo_stack_.push_back(std::move(command));
    return result;
  }

  undo_stack_.push_back(std::move(command));
  return Result();
}

void CommandHistory::clear() noexcept {
  undo_stack_.clear();
  redo_stack_.clear();
}

}  // namespace graphscore
