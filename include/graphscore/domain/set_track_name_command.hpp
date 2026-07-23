// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the display name of an active track identified by its stable
// TrackId. Snapshots the old name on first successful execution.
class SetTrackNameCommand : public Command {
 public:
  SetTrackNameCommand(TrackId track_id, std::string new_name)
      : track_id_(track_id), new_name_(std::move(new_name)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  TrackId     track_id_;
  std::string new_name_;
  std::string old_name_;
  State       state_ = State::kFresh;
};

}  // namespace graphscore
