// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_track_solo_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

Result SetTrackSoloCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_solo_ = track->mix_settings().solo();
  track->mix_settings().set_solo(new_solo_);
  state_ = State::kDone;
  return Result();
}

Result SetTrackSoloCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_solo(old_solo_);
  state_ = State::kUndone;
  return Result();
}

Result SetTrackSoloCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Track* track = project.find_active_track(track_id_);
  if (track == nullptr)
    return Result(ResultCode::kInvalidArgument);

  track->mix_settings().set_solo(new_solo_);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
