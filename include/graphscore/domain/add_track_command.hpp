// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/staff_layout.hpp>

namespace graphscore {

// Adds a new active track with the given name, staff layout, and MIDI
// channel (see Project::add_track). Fails, leaving the project unchanged,
// if the project already has kMaxActiveTracks active tracks.
//
// Reversibility: execute mints the track's id and snapshots it on first
// success. Undo fully erases the track through Project::hard_remove_track --
// the undo-only inverse of add_track, never the user-facing archive. This
// is correct because a freshly added track has no notation content yet: the
// only "removal" a user ever performs on an existing track is archive
// (recoverable), but the inverse of adding one must actually be gone. Redo
// re-adds the exact same name/layout/channel through
// Project::add_track_with_id, preserving the same TrackId rather than
// minting a fresh one; the recreated lanes are empty, which is the exact
// state a freshly added track was in.
class AddTrackCommand : public Command {
 public:
  AddTrackCommand(std::string name, StaffLayout layout, MidiChannel channel)
      : name_(std::move(name)), layout_(std::move(layout)), channel_(channel) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  std::string            name_;
  StaffLayout            layout_;
  MidiChannel            channel_;
  std::optional<TrackId> created_id_;
  State                  state_ = State::kFresh;
};

}  // namespace graphscore
