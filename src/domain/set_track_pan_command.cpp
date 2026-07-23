// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_track_pan_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

Result SetTrackPanCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_pan_ = track->mix_settings().pan();
  track->mix_settings().set_pan(new_pan_);
  state_ = State::kDone;
  return Result();
}

Result SetTrackPanCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_pan(old_pan_);
  state_ = State::kUndone;
  return Result();
}

Result SetTrackPanCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_pan(new_pan_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
