// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/graph_position.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

// A node in the project graph: identity, writer-facing metadata, one empty
// TrackLane per track the project considers aligned with this node, an
// optional NodeTimeline (the ordered measure map, per-measure time/key
// signatures, clef changes, main-region/pickdown boundary, and tempo
// lane), and the node's own named input/output connectors and event
// listeners. The timeline is optional at the Node level so a freshly
// created node — before its measures are authored — remains a valid Node;
// every NodeTimeline a node does hold satisfies the "0.1.0" timeline
// invariants (at least one main-region measure, valid pickdown bounds, and
// so on) because NodeTimeline can only be constructed through its own
// validated factory. The Phase 4 notation content carried inside each lane
// is out of scope here.
//
// Connectors and event listeners live here, on Node, rather than in a
// separate Graph-owned collection, because the plan phrases them as "per
// node" data and because Node already owns everything else about a node's
// identity (Phase 2/3). Graph (graph.hpp) is a thin service on top of an
// existing Project for the concerns that genuinely need more than one
// node's worth of state -- connecting an output to a destination on a
// *different* node, and validating an event binding against the project's
// EventRegistry -- rather than a second container that would duplicate
// Project's existing node ownership.
class Node {
 public:
  explicit Node(NodeId id, std::string name = {});

  [[nodiscard]] NodeId id() const noexcept { return id_; }

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  void set_name(std::string name) noexcept { name_ = std::move(name); }

  // Packed 0xRRGGBBAA custom color; writer display data only.
  [[nodiscard]] std::uint32_t color() const noexcept { return color_; }

  void set_color(std::uint32_t color) noexcept { color_ = color; }

  [[nodiscard]] const std::string& notes() const noexcept { return notes_; }

  void set_notes(std::string notes) noexcept { notes_ = std::move(notes); }

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

  // Adds a new, uniquely identified input/output connector to this node
  // and returns its stable id. There is no limit on how many of either a
  // node may hold (product decision: "any number of named stable input
  // and output connectors per node").
  [[nodiscard]] ConnectorId add_input(std::string name);

  [[nodiscard]] ConnectorId add_output(
      std::string name, ConnectorType type = ConnectorType::kSequential);

  [[nodiscard]] Result remove_output(ConnectorId id);

  [[nodiscard]] const std::vector<InputConnector>& inputs() const noexcept {
    return inputs_;
  }

  [[nodiscard]] const std::vector<OutputConnector>& outputs() const noexcept {
    return outputs_;
  }

  [[nodiscard]] InputConnector* find_input(ConnectorId id);

  [[nodiscard]] const InputConnector* find_input(ConnectorId id) const;

  [[nodiscard]] OutputConnector* find_output(ConnectorId id);

  [[nodiscard]] const OutputConnector* find_output(ConnectorId id) const;

  // Clears the destination of every output connector on this node whose
  // destination currently references (node, connector). Used by
  // Graph::remove_input to keep every other node's destinations consistent
  // when an input connector is removed from the graph.
  void clear_destinations_to(NodeId node, ConnectorId connector) noexcept;

  // Binds `output_id`'s type-appropriate trigger to `event` (or clears it
  // via nullopt), rejecting a vertical/sequential clash: a kVertical and a
  // kSequential output on this node may never share the same bound event
  // (product decision, enforced here). Does not validate that `event` is a
  // registered EventId -- Graph::bind_output_event (graph.hpp) does that
  // against the owning Project's EventRegistry before delegating here.
  //
  // Destructive round trip: unbinding the last output referencing `event`
  // erases this node's EventListener for it entirely (see
  // cleanup_listener_if_unused, node.cpp); a later rebind to the same
  // event creates a fresh listener with default policy/capacity. Any
  // queue policy/capacity configured in between is lost with no
  // diagnostic. TODO(Phase 8): the undo record for an unbind must snapshot
  // the EventListener itself if it needs to restore it exactly.
  [[nodiscard]] Result bind_output_event(ConnectorId            output_id,
                                         std::optional<EventId> event);

  // Changes `output_id`'s ConnectorType in place. Fails, leaving the
  // output and its listener unchanged, if `output_id` is bound to an
  // event that a different, opposite-typed output on this node is also
  // bound to -- the same vertical/sequential clash bind_output_event
  // rejects, reachable here because retyping bypasses that check
  // otherwise. On success, also updates the shared
  // EventListener::bound_type() for the output's bound event, if any, so
  // it never goes stale.
  [[nodiscard]] Result set_output_type(ConnectorId   output_id,
                                       ConnectorType type);

  // The shared listener configuration for `event` on this node, or nullptr
  // if no output on this node is currently bound to it.
  [[nodiscard]] const EventListener* find_listener(EventId event) const;

  // Fails if no output on this node is currently bound to `event` (i.e. no
  // listener exists yet to configure), or if `policy` is kFifo and
  // `capacity` is 0 -- a zero-capacity FIFO could never hold an
  // occurrence, so it is rejected rather than left constructible.
  [[nodiscard]] Result set_listener_policy(EventId event, QueuePolicy policy,
                                           std::size_t capacity);

 private:
  friend class Graph;

  // Fails if `id` does not reference an input connector on this node.
  // Private: removing an input does not clear any other node's output
  // destination that may still reference it, so only Graph::remove_input
  // (graph.hpp), the cross-node-safe entry point, may call this directly.
  [[nodiscard]] Result remove_input(ConnectorId id);

  void cleanup_listener_if_unused(EventId event);

  NodeId        id_;
  std::string   name_;
  std::uint32_t color_ = 0xFFFFFFFF;
  std::string   notes_;
  GraphPosition position_;

  std::unordered_map<TrackId, TrackLane> lanes_;
  std::optional<NodeTimeline>            timeline_;

  std::vector<InputConnector>                inputs_;
  std::vector<OutputConnector>               outputs_;
  std::unordered_map<EventId, EventListener> listeners_;
};

}  // namespace graphscore
