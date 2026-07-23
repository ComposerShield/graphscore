// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_track_mute_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

Result SetTrackMuteCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_mute_ = track->mix_settings().mute();
  track->mix_settings().set_mute(new_mute_);
  state_ = State::kDone;
  return Result();
}

Result SetTrackMuteCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_mute(old_mute_);
  state_ = State::kUndone;
  return Result();
}

Result SetTrackMuteCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_mute(new_mute_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
