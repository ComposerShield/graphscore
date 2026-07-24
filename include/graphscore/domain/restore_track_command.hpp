// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Restores an archived track identified by its stable TrackId back into the
// active-track set (see Project::restore_track). No value snapshot is
// needed: the inverse operation, archiving the track, puts it back in place
// exactly.
//
// Propagates Project::restore_track's failure Result (e.g. an unknown
// TrackId, or restoring would exceed the active-track cap) without changing
// state. Propagates Project::archive_track's failure Result on undo without
// changing state.
class RestoreTrackCommand : public Command {
 public:
  explicit RestoreTrackCommand(TrackId track_id) : track_id_(track_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  TrackId track_id_;
  State   state_ = State::kFresh;
};

}  // namespace graphscore
