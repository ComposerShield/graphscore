// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the audition-mix pan of an active track identified by its stable
// TrackId. Snapshots the old pan on first successful execution.
//
// Missing or archived TrackId returns kInvalidArgument without mutation.
// Pan is stored as-is; legal-range validation is deferred to M08.
class SetTrackPanCommand : public Command {
 public:
  SetTrackPanCommand(TrackId track_id, float new_pan)
      : track_id_(track_id), new_pan_(new_pan) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  TrackId track_id_;
  float   new_pan_;
  float   old_pan_ = 0.0F;
  State   state_   = State::kFresh;
};

}  // namespace graphscore
