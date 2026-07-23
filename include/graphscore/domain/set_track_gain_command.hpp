// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the audition-mix gain of an active track identified by its stable
// TrackId. Snapshots the old gain on first successful execution.
//
// Missing or archived TrackId returns kInvalidArgument without mutation.
// Gain is stored as-is; legal-range validation is deferred to M08.
class SetTrackGainCommand : public Command {
 public:
  SetTrackGainCommand(TrackId track_id, float new_gain)
      : track_id_(track_id), new_gain_(new_gain) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  TrackId track_id_;
  float   new_gain_;
  float   old_gain_ = 0.0F;
  State   state_    = State::kFresh;
};

}  // namespace graphscore
