// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_node_notes_command.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetNodeNotesCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::string prepared_old;
  std::string prepared_new;
  try {
    prepared_old = node->notes();
    prepared_new = new_notes_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  old_notes_ = std::move(prepared_old);
  node->set_notes(std::move(prepared_new));
  state_ = State::kDone;
  return Result();
}

Result SetNodeNotesCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::string prepared;
  try {
    prepared = old_notes_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  node->set_notes(std::move(prepared));
  state_ = State::kUndone;
  return Result();
}

Result SetNodeNotesCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::string prepared;
  try {
    prepared = new_notes_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  node->set_notes(std::move(prepared));
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
