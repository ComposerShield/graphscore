// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the audition-mix mute state of an active track identified by its
// stable TrackId. Snapshots the old mute state on first successful
// execution.
//
// Missing or archived TrackId returns kInvalidArgument without mutation.
class SetTrackMuteCommand : public Command {
 public:
  SetTrackMuteCommand(TrackId track_id, bool new_mute)
      : track_id_(track_id), new_mute_(new_mute) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  TrackId track_id_;
  bool    new_mute_;
  bool    old_mute_ = false;
  State   state_    = State::kFresh;
};

}  // namespace graphscore
