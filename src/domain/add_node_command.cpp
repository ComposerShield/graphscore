// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_node_command.hpp>

#include <new>
#include <stdexcept>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result AddNodeCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  NodeId id;
  try {
    id = project.add_node(name_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  created_id_ = id;
  state_      = State::kDone;
  return Result();
}

Result AddNodeCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_id_.has_value())
    return Result(ResultCode::kInternalError);

  Result result;
  try {
    result = project.remove_node(*created_id_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result AddNodeCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_id_.has_value())
    return Result(ResultCode::kInternalError);

  Result result;
  try {
    result = project.add_node_with_id(*created_id_, name_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
