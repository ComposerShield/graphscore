// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/graph_position.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

// A node in the project graph: identity, writer-facing metadata, one empty
// TrackLane per track the project considers aligned with this node, and an
// optional NodeTimeline (the ordered measure map, per-measure time/key
// signatures, clef changes, main-region/pickdown boundary, and tempo
// lane). The timeline is optional at the Node level so a freshly created
// node — before its measures are authored — remains a valid Node; every
// NodeTimeline a node does hold satisfies the "0.1.0" timeline invariants
// (at least one main-region measure, valid pickdown bounds, and so on)
// because NodeTimeline can only be constructed through its own validated
// factory. The Phase 4 notation content carried inside each lane is out of
// scope here; Node exposes only the identity, metadata, lane-alignment, and
// timeline surface Phase 2/3 require.
class Node {
 public:
  explicit Node(NodeId id, std::string name = {});

  [[nodiscard]] NodeId id() const noexcept { return id_; }

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  void set_name(std::string name) { name_ = std::move(name); }

  // Packed 0xRRGGBBAA custom color; writer display data only.
  [[nodiscard]] std::uint32_t color() const noexcept { return color_; }

  void set_color(std::uint32_t color) noexcept { color_ = color; }

  [[nodiscard]] const std::string& notes() const noexcept { return notes_; }

  void set_notes(std::string notes) { notes_ = std::move(notes); }

  [[nodiscard]] const GraphPosition& position() const noexcept {
    return position_;
  }

  void set_position(GraphPosition position) noexcept { position_ = position; }

  [[nodiscard]] bool has_lane(TrackId track_id) const;

  [[nodiscard]] TrackLane* lane(TrackId track_id);

  [[nodiscard]] const TrackLane* lane(TrackId track_id) const;

  // Inserts an empty lane for `track_id` if one is not already present.
  // Never overwrites an existing lane, so notation data already recorded in
  // an archived track's lane is never disturbed by re-alignment.
  void ensure_lane(TrackId track_id);

  [[nodiscard]] std::size_t lane_count() const noexcept {
    return lanes_.size();
  }

  [[nodiscard]] bool has_timeline() const noexcept {
    return timeline_.has_value();
  }

  [[nodiscard]] NodeTimeline* timeline() noexcept {
    return timeline_ ? &*timeline_ : nullptr;
  }

  [[nodiscard]] const NodeTimeline* timeline() const noexcept {
    return timeline_ ? &*timeline_ : nullptr;
  }

  void set_timeline(NodeTimeline timeline) { timeline_ = std::move(timeline); }

  void clear_timeline() noexcept { timeline_.reset(); }

 private:
  NodeId        id_;
  std::string   name_;
  std::uint32_t color_ = 0xFFFFFFFF;
  std::string   notes_;
  GraphPosition position_;

  std::unordered_map<TrackId, TrackLane> lanes_;
  std::optional<NodeTimeline>            timeline_;
};

}  // namespace graphscore
