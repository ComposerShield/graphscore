// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <utility>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/staff_layout.hpp>

namespace graphscore {

// Placeholder for a track's notation content within one node. Intentionally
// empty: the Phase 4 notation model fills this in with the track's voices.
// Node keys a TrackLane collection by TrackId so a lane's identity survives
// track archival untouched (see Project::archive_track).
struct TrackLane {
  [[nodiscard]] bool operator==(const TrackLane&) const = default;
};

// One of up to 64 globally active track definitions: a stable identity, a
// project-order position, a display name, a fixed staff layout, and a fixed
// MIDI channel. The staff layout and MIDI channel are set at construction
// and never change afterward.
class Track {
 public:
  Track(TrackId id, TrackIndex index, std::string name, StaffLayout layout,
        MidiChannel channel)
      : id_(id),
        index_(index),
        name_(std::move(name)),
        layout_(std::move(layout)),
        channel_(channel) {}

  [[nodiscard]] TrackId id() const noexcept { return id_; }

  [[nodiscard]] TrackIndex index() const noexcept { return index_; }

  // Project-order position only; regenerated freely as tracks are added,
  // archived, or restored, and never used as identity.
  void set_index(TrackIndex index) noexcept { index_ = index; }

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  void set_name(std::string name) { name_ = std::move(name); }

  [[nodiscard]] const StaffLayout& layout() const noexcept { return layout_; }

  [[nodiscard]] MidiChannel channel() const noexcept { return channel_; }

 private:
  TrackId     id_;
  TrackIndex  index_;
  std::string name_;
  StaffLayout layout_;
  MidiChannel channel_;
};

}  // namespace graphscore
