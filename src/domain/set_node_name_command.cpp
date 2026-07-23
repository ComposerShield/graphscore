// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_node_name_command.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result SetNodeNameCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  // Preconstruct both strings before any mutation so allocation failures
  // leave the command and model state completely unchanged.
  std::string prepared_old;
  std::string prepared_new;
  try {
    prepared_old = node->name();
    prepared_new = new_name_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  old_name_ = std::move(prepared_old);
  node->set_name(std::move(prepared_new));
  state_ = State::kDone;
  return Result();
}

Result SetNodeNameCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::string prepared;
  try {
    prepared = old_name_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  node->set_name(std::move(prepared));
  state_ = State::kUndone;
  return Result();
}

Result SetNodeNameCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::string prepared;
  try {
    prepared = new_name_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  node->set_name(std::move(prepared));
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
