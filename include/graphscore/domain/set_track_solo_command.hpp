// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the audition-mix solo state of an active track identified by its
// stable TrackId. Snapshots the old solo state on first successful
// execution.
//
// Missing or archived TrackId returns kInvalidArgument without mutation.
class SetTrackSoloCommand : public Command {
 public:
  SetTrackSoloCommand(TrackId track_id, bool new_solo)
      : track_id_(track_id), new_solo_(new_solo) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  TrackId track_id_;
  bool    new_solo_;
  bool    old_solo_ = false;
  State   state_    = State::kFresh;
};

}  // namespace graphscore
