// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/dynamic.hpp>
#include <graphscore/domain/event_registry.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/staff_layout.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

// The project-wide source of truth for tracks, nodes, and their alignment:
// metadata, a designated start node, project-wide tempo/dynamic defaults,
// the registry of event definitions, and the active/archived track and node
// collections.
class Project {
 public:
  // Product decision: up to 64 globally active track definitions.
  static constexpr std::size_t kMaxActiveTracks = 64;

  explicit Project(ProjectId id, std::string name = {});

  [[nodiscard]] ProjectId id() const noexcept { return id_; }

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  void set_name(std::string name) { name_ = std::move(name); }

  [[nodiscard]] std::optional<NodeId> start_node() const noexcept {
    return start_node_;
  }

  // Fails if `node_id` does not reference a node this project owns.
  [[nodiscard]] Result set_start_node(NodeId node_id);

  void clear_start_node() noexcept { start_node_.reset(); }

  [[nodiscard]] const Tempo& default_tempo() const noexcept {
    return default_tempo_;
  }

  void set_default_tempo(Tempo tempo) noexcept { default_tempo_ = tempo; }

  [[nodiscard]] Dynamic default_dynamic() const noexcept {
    return default_dynamic_;
  }

  void set_default_dynamic(Dynamic dynamic) noexcept {
    default_dynamic_ = dynamic;
  }

  // Adds an active track and creates an aligned empty lane for it in every
  // node the project owns. Fails if the project already has
  // kMaxActiveTracks active tracks.
  [[nodiscard]] std::optional<TrackId> add_track(std::string name,
                                                 StaffLayout layout,
                                                 MidiChannel channel);

  // Archives an active track: it moves out of the active-track set (so it
  // is excluded from playback/export), but every lane already recorded for
  // it, in every node, is left untouched and therefore recoverable.
  [[nodiscard]] Result archive_track(TrackId track_id);

  // Restores an archived track to the active set, re-aligning any node
  // created since it was archived with a fresh empty lane. Nodes that
  // already had a lane for it keep that lane's data unchanged. Fails if
  // `track_id` is not archived, or if restoring it would exceed
  // kMaxActiveTracks.
  [[nodiscard]] Result restore_track(TrackId track_id);

  [[nodiscard]] const std::vector<Track>& active_tracks() const noexcept {
    return active_tracks_;
  }

  [[nodiscard]] const std::vector<Track>& archived_tracks() const noexcept {
    return archived_tracks_;
  }

  [[nodiscard]] const Track* find_active_track(TrackId track_id) const;

  [[nodiscard]] const Track* find_archived_track(TrackId track_id) const;

  // Creates a node and aligns it with an empty lane for every currently
  // active track.
  NodeId add_node(std::string name = {});

  [[nodiscard]] const std::vector<Node>& nodes() const noexcept {
    return nodes_;
  }

  [[nodiscard]] Node* find_node(NodeId node_id);

  [[nodiscard]] const Node* find_node(NodeId node_id) const;

  // Prefer Graph::remove_event (graph.hpp) over events().remove_event()
  // directly -- the latter does not cascade to bound outputs/listeners.
  [[nodiscard]] EventRegistry& events() noexcept { return events_; }

  [[nodiscard]] const EventRegistry& events() const noexcept { return events_; }

 private:
  void reindex_active_tracks();

  ProjectId             id_;
  std::string           name_;
  std::optional<NodeId> start_node_;
  Tempo                 default_tempo_;
  Dynamic               default_dynamic_ = Dynamic::kMf;
  std::vector<Track>    active_tracks_;
  std::vector<Track>    archived_tracks_;
  std::vector<Node>     nodes_;
  EventRegistry         events_;
};

}  // namespace graphscore
