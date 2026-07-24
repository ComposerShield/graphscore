// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/register_event_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/event_registry.hpp>
#include <graphscore/domain/graph.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result RegisterEventCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  std::optional<EventId> id;
  try {
    id = project.events().add_event(name_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!id.has_value())
    return Result(ResultCode::kInvalidArgument);

  created_id_ = *id;
  state_      = State::kDone;
  return Result();
}

Result RegisterEventCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_id_.has_value())
    return Result(ResultCode::kInternalError);

  Graph        graph(project);
  const Result result = graph.remove_event(*created_id_);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result RegisterEventCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_id_.has_value())
    return Result(ResultCode::kInternalError);

  Result result;
  try {
    result = project.events().add_event_with_id(*created_id_, name_);
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
