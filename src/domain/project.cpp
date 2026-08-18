// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace graphscore {
namespace {

static_assert(std::is_nothrow_move_assignable_v<Node>);
static_assert(std::is_nothrow_move_constructible_v<Track>);

void align_track(Node& node, const Track& track) {
  node.ensure_lane(track.id());
  TrackLane* const lane = node.lane(track.id());
  for (const StaveDefinition& stave : track.layout().staves()) {
    lane->ensure_stave(stave.id);
    NodeTimeline* const timeline = node.timeline();
    if (timeline != nullptr && !timeline->has_clef_lane(stave.id)) {
      static_cast<void>(
          timeline->create_clef_lane(stave.id, ClefLane(stave.default_clef)));
    }
  }
}

[[nodiscard]] bool track_is_aligned(const Node& node, const Track& track) {
  const TrackLane* const lane = node.lane(track.id());
  if (lane == nullptr) {
    return false;
  }
  for (const StaveDefinition& stave : track.layout().staves()) {
    if (!lane->has_stave(stave.id)) {
      return false;
    }
    const NodeTimeline* const timeline = node.timeline();
    if (timeline != nullptr && !timeline->has_clef_lane(stave.id)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool staves_are_available(const StaffLayout&        layout,
                                        const std::vector<Track>& active,
                                        const std::vector<Track>& archived) {
  std::vector<StaveId> ids;
  ids.reserve(layout.stave_count());
  for (const StaveDefinition& stave : layout.staves()) {
    if (stave.id == StaveId() ||
        std::ranges::find(ids, stave.id) != ids.end()) {
      return false;
    }
    ids.push_back(stave.id);
  }
  const auto contains_stave = [&](const std::vector<Track>& tracks) {
    return std::ranges::any_of(tracks, [&](const Track& track) {
      return std::ranges::any_of(
          track.layout().staves(), [&](const StaveDefinition& stave) {
            return std::ranges::find(ids, stave.id) != ids.end();
          });
    });
  };
  return !contains_stave(active) && !contains_stave(archived);
}

void remove_track_alignment(Node& node, const Track& track) noexcept {
  node.remove_lane(track.id());
  NodeTimeline* const timeline = node.timeline();
  if (timeline == nullptr) {
    return;
  }
  for (const StaveDefinition& stave : track.layout().staves()) {
    timeline->remove_clef_lane(stave.id);
  }
}

}  // namespace

Project::Project(ProjectId id, std::string name)
    : id_(id), name_(std::move(name)) {}

Result Project::set_start_node(NodeId node_id) noexcept {
  if (find_node(node_id) == nullptr)
    return Result(ResultCode::kInvalidArgument);
  start_node_ = node_id;
  return Result();
}

std::optional<TrackId> Project::add_track(std::string name, StaffLayout layout,
                                          MidiChannel channel) {
  if (active_tracks_.size() >= kMaxActiveTracks ||
      !staves_are_available(layout, active_tracks_, archived_tracks_)) {
    return std::nullopt;
  }

  const TrackId id = TrackId::generate();
  Track track(id, TrackIndex(0), std::move(name), std::move(layout), channel);
  std::vector<Node> aligned_nodes = nodes_;
  for (Node& node : aligned_nodes) {
    align_track(node, track);
  }
  active_tracks_.push_back(std::move(track));
  reindex_active_tracks();
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    nodes_[index] = std::move(aligned_nodes[index]);
  }

  return id;
}

Result Project::add_track_with_id(TrackId id, std::string name,
                                  StaffLayout layout, MidiChannel channel) {
  if (active_tracks_.size() >= kMaxActiveTracks ||
      !staves_are_available(layout, active_tracks_, archived_tracks_)) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (find_active_track(id) != nullptr || find_archived_track(id) != nullptr)
    return Result(ResultCode::kInvalidArgument);

  Track track(id, TrackIndex(0), std::move(name), std::move(layout), channel);
  std::vector<Node> aligned_nodes = nodes_;
  for (Node& node : aligned_nodes) {
    align_track(node, track);
  }
  active_tracks_.push_back(std::move(track));
  reindex_active_tracks();
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    nodes_[index] = std::move(aligned_nodes[index]);
  }

  return Result();
}

Result Project::archive_track(TrackId track_id) {
  const auto it = std::find_if(
      active_tracks_.begin(), active_tracks_.end(),
      [track_id](const Track& track) { return track.id() == track_id; });
  if (it == active_tracks_.end())
    return Result(ResultCode::kInvalidArgument);

  archived_tracks_.push_back(std::move(*it));
  active_tracks_.erase(it);
  reindex_active_tracks();
  return Result();
}

Result Project::restore_track(TrackId track_id) {
  if (active_tracks_.size() >= kMaxActiveTracks)
    return Result(ResultCode::kInvalidArgument);

  const auto it = std::find_if(
      archived_tracks_.begin(), archived_tracks_.end(),
      [track_id](const Track& track) { return track.id() == track_id; });
  if (it == archived_tracks_.end())
    return Result(ResultCode::kInvalidArgument);

  std::optional<std::vector<Node>> aligned_nodes;
  if (std::ranges::any_of(nodes_, [&](const Node& node) {
        return !track_is_aligned(node, *it);
      })) {
    aligned_nodes = nodes_;
    for (Node& node : *aligned_nodes) {
      align_track(node, *it);
    }
  }

  active_tracks_.push_back(std::move(*it));
  archived_tracks_.erase(it);
  reindex_active_tracks();
  if (aligned_nodes.has_value()) {
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
      nodes_[index] = std::move((*aligned_nodes)[index]);
    }
  }

  return Result();
}

Result Project::hard_remove_track(TrackId track_id) {
  const auto it = std::find_if(
      active_tracks_.begin(), active_tracks_.end(),
      [track_id](const Track& track) { return track.id() == track_id; });
  if (it == active_tracks_.end())
    return Result(ResultCode::kInvalidArgument);

  for (Node& node : nodes_) {
    remove_track_alignment(node, *it);
  }

  active_tracks_.erase(it);
  reindex_active_tracks();

  return Result();
}

Track* Project::find_active_track(TrackId track_id) {
  const auto it = std::find_if(
      active_tracks_.begin(), active_tracks_.end(),
      [track_id](const Track& track) { return track.id() == track_id; });
  return it == active_tracks_.end() ? nullptr : &*it;
}

const Track* Project::find_active_track(TrackId track_id) const {
  const auto it = std::find_if(
      active_tracks_.begin(), active_tracks_.end(),
      [track_id](const Track& track) { return track.id() == track_id; });
  return it == active_tracks_.end() ? nullptr : &*it;
}

const Track* Project::find_archived_track(TrackId track_id) const {
  const auto it = std::find_if(
      archived_tracks_.begin(), archived_tracks_.end(),
      [track_id](const Track& track) { return track.id() == track_id; });
  return it == archived_tracks_.end() ? nullptr : &*it;
}

NodeId Project::add_node(std::string name) {
  const NodeId id = NodeId::generate();
  nodes_.emplace_back(id, std::move(name));

  for (const Track& track : active_tracks_) {
    nodes_.back().ensure_lane(track.id());
  }

  return id;
}

Result Project::add_node_with_id(NodeId id, std::string name) {
  if (find_node(id) != nullptr)
    return Result(ResultCode::kInvalidArgument);

  nodes_.emplace_back(id, std::move(name));

  for (const Track& track : active_tracks_) {
    nodes_.back().ensure_lane(track.id());
  }

  return Result();
}

Result Project::remove_node(NodeId id) {
  const Node* node = find_node(id);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::vector<ConnectorId> input_ids;
  input_ids.reserve(node->inputs().size());
  for (const InputConnector& input : node->inputs())
    input_ids.push_back(input.id());

  for (Node& other : nodes_) {
    if (other.id() == id)
      continue;
    for (const ConnectorId input_id : input_ids)
      other.clear_destinations_to(id, input_id);
  }

  const std::optional<NodeId> start = start_node_;
  if (start.has_value() && *start == id)
    clear_start_node();

  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [id](const Node& candidate) { return candidate.id() == id; });
  nodes_.erase(it);

  return Result();
}

Result Project::restore_node(Node node) {
  if (find_node(node.id()) != nullptr)
    return Result(ResultCode::kInvalidArgument);

  nodes_.push_back(std::move(node));
  return Result();
}

Result Project::restore_node_at(Node node, std::size_t index) {
  if (find_node(node.id()) != nullptr || index > nodes_.size())
    return Result(ResultCode::kInvalidArgument);

  nodes_.insert(nodes_.begin() + static_cast<std::ptrdiff_t>(index),
                std::move(node));
  return Result();
}

Node* Project::find_node(NodeId node_id) {
  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [node_id](const Node& node) { return node.id() == node_id; });
  return it == nodes_.end() ? nullptr : &*it;
}

const Node* Project::find_node(NodeId node_id) const {
  const auto it = std::find_if(
      nodes_.begin(), nodes_.end(),
      [node_id](const Node& node) { return node.id() == node_id; });
  return it == nodes_.end() ? nullptr : &*it;
}

void Project::reindex_active_tracks() {
  for (std::size_t i = 0; i < active_tracks_.size(); ++i) {
    active_tracks_[i].set_index(TrackIndex(static_cast<std::uint32_t>(i)));
  }
}

}  // namespace graphscore
