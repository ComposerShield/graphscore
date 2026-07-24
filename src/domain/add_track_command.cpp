// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_track_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

Result AddTrackCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  std::optional<TrackId> id;
  try {
    id = project.add_track(name_, layout_, channel_);
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

Result AddTrackCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_id_.has_value())
    return Result(ResultCode::kInternalError);

  const Result result = project.hard_remove_track(*created_id_);
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result AddTrackCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_id_.has_value())
    return Result(ResultCode::kInternalError);

  Result result;
  try {
    result = project.add_track_with_id(*created_id_, name_, layout_, channel_);
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
