// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/audition_mix.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/plugin_chain.hpp>
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

// A stave-qualified pedal span operation for incremental refresh.
struct PedalDeltaOp {
  StaveId          stave_id;
  RefOp<PedalSpan> op;
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

  // Copy/move assignment reset mutation tracking on the destination. Moving
  // also resets tracking on the source so its pre-move tokens become stale.
  TrackLane(const TrackLane& other);
  TrackLane& operator=(const TrackLane& other);
  TrackLane(TrackLane&& other) noexcept;
  TrackLane& operator=(TrackLane&& other) noexcept;

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

  // The maximum VoiceContent::total_length() across every stave and
  // voice in this lane, or Rational(0) if no staves exist. The result
  // is deterministic: computing a max is invariant under iteration
  // order of the staves_ unordered_map. This is the lane-extent
  // position used by caret validation (Selection contract: legal caret
  // positions are event boundaries ∪ TrackLane::total_length()).
  [[nodiscard]] Rational total_length() const;

  // Every stave id currently holding a StaveVoices entry, in
  // UUID-lexicographic order. Used to enumerate a lane's staves, e.g. for
  // referential validation across every stave/voice.
  [[nodiscard]] std::vector<StaveId> stave_ids() const;

  // Sustain-pedal spans for `stave_id`, or nullptr if none have been added.
  // Pedal is scoped per stave, not per voice.
  [[nodiscard]] const std::vector<PedalSpan>* pedal_spans(
      StaveId stave_id) const;

  [[nodiscard]] Result add_pedal_span(StaveId stave_id, PedalSpan span);

  [[nodiscard]] Result remove_pedal_span(StaveId stave_id, NotationEntityId id);

  // Mutation revision tracking for pedal spans. Works like VoiceContent's
  // revision mechanism and returns stave-qualified operation records.
  [[nodiscard]] VoiceRevision capture_revision() const noexcept;
  [[nodiscard]] std::optional<std::vector<PedalDeltaOp>> pedal_delta_since(
      VoiceRevision since) const;

  // Semantic equality excludes mutation-tracking state.
  [[nodiscard]] bool operator==(const TrackLane& other) const;

 private:
  // Commits prepared deltas to the fixed-capacity ring. The slot count is
  // fixed; vector payload allocation occurs while preparing each mutation.
  void advance_revision(std::vector<PedalDeltaOp> deltas) noexcept;
  void reset_revision_tracking() noexcept;

  static constexpr std::size_t kMaxTrackedRevisions = 16;

  std::unordered_map<StaveId, StaveVoices>            staves_;
  std::unordered_map<StaveId, std::vector<PedalSpan>> pedal_spans_;

  // Mutation tracking for pedal spans — excluded from semantic equality.
  VoiceRevision                                               pedal_revision_;
  std::array<std::vector<PedalDeltaOp>, kMaxTrackedRevisions> pedal_deltas_{};
  std::uint32_t pedal_ring_head_{0};
  std::uint32_t pedal_ring_count_{0};
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

  void set_index(TrackIndex index) noexcept { index_ = index; }

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  void set_name(std::string name) noexcept { name_ = std::move(name); }

  [[nodiscard]] const StaffLayout& layout() const noexcept { return layout_; }

  [[nodiscard]] MidiChannel channel() const noexcept { return channel_; }

  [[nodiscard]] TrackPluginChain& plugin_chain() noexcept {
    return plugin_chain_;
  }

  [[nodiscard]] const TrackPluginChain& plugin_chain() const noexcept {
    return plugin_chain_;
  }

  [[nodiscard]] AuditionMixSettings& mix_settings() noexcept {
    return mix_settings_;
  }

  [[nodiscard]] const AuditionMixSettings& mix_settings() const noexcept {
    return mix_settings_;
  }

 private:
  TrackId             id_;
  TrackIndex          index_;
  std::string         name_;
  StaffLayout         layout_;
  MidiChannel         channel_;
  TrackPluginChain    plugin_chain_;
  AuditionMixSettings mix_settings_;
};

}  // namespace graphscore
