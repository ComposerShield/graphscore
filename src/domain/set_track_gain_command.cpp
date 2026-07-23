// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_track_gain_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

Result SetTrackGainCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_gain_ = track->mix_settings().gain();
  track->mix_settings().set_gain(new_gain_);
  state_ = State::kDone;
  return Result();
}

Result SetTrackGainCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_gain(old_gain_);
  state_ = State::kUndone;
  return Result();
}

Result SetTrackGainCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_gain(new_gain_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
