// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/staff_layout.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// One stave's up to four explicit rhythmic voices (product decision: "up
// to four explicitly selected rhythmic voices per staff"). Voice numbering
// matches core Voice (1-4); every stave always holds exactly four
// VoiceContent slots, empty until notes/rests are appended to them.
class StaveVoices {
 public:
  StaveVoices() = default;

  [[nodiscard]] VoiceContent& voice(Voice voice_number) {
    return voices_[voice_number.index() - Voice::kMin];
  }

  [[nodiscard]] const VoiceContent& voice(Voice voice_number) const {
    return voices_[voice_number.index() - Voice::kMin];
  }

  [[nodiscard]] bool operator==(const StaveVoices&) const = default;

 private:
  std::array<VoiceContent, Voice::kMax> voices_;
};

// A track's notation content within one node: up to four voices for each
// stave in the track's fixed StaffLayout, keyed by StaveId. Node keys a
// TrackLane collection by TrackId so a lane's identity survives track
// archival untouched (see Project::archive_track). TrackLane never stores
// the owning NodeTimeline itself; normalization/completeness operations on
// its voices take the node's musical length as an explicit parameter.
class TrackLane {
 public:
  TrackLane() = default;

  [[nodiscard]] bool has_stave(StaveId stave_id) const {
    return staves_.contains(stave_id);
  }

  [[nodiscard]] StaveVoices* stave(StaveId stave_id);

  [[nodiscard]] const StaveVoices* stave(StaveId stave_id) const;

  // Inserts an empty (four-voice) StaveVoices for `stave_id` if one is not
  // already present. Never overwrites an existing entry, mirroring
  // Node::ensure_lane's never-overwrite guarantee.
  void ensure_stave(StaveId stave_id);

  [[nodiscard]] std::size_t stave_count() const noexcept {
    return staves_.size();
  }

  [[nodiscard]] bool operator==(const TrackLane&) const = default;

 private:
  std::unordered_map<StaveId, StaveVoices> staves_;
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
