// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Archives an active track identified by its stable TrackId, moving it out
// of the active-track set while leaving every lane already recorded for it
// untouched (see Project::archive_track). No value snapshot is needed: the
// inverse operation, restoring the track, puts it back in place exactly.
//
// Propagates Project::archive_track's failure Result (e.g. an unknown
// TrackId) without changing state. Propagates Project::restore_track's
// failure Result on undo (e.g. restoring would exceed the active-track cap)
// without changing state.
class ArchiveTrackCommand : public Command {
 public:
  explicit ArchiveTrackCommand(TrackId track_id) : track_id_(track_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  TrackId track_id_;
  State   state_ = State::kFresh;
};

}  // namespace graphscore
