// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace graphscore {

Project::Project(ProjectId id, std::string name)
    : id_(id), name_(std::move(name)) {}

Result Project::set_start_node(NodeId node_id) {
  if (find_node(node_id) == nullptr)
    return Result(ResultCode::kInvalidArgument);
  start_node_ = node_id;
  return Result();
}

std::optional<TrackId> Project::add_track(std::string name, StaffLayout layout,
                                          MidiChannel channel) {
  if (active_tracks_.size() >= kMaxActiveTracks)
    return std::nullopt;

  const TrackId id = TrackId::generate();
  active_tracks_.emplace_back(id, TrackIndex(0), std::move(name),
                              std::move(layout), channel);
  reindex_active_tracks();

  for (Node& node : nodes_) {
    node.ensure_lane(id);
  }

  return id;
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

  active_tracks_.push_back(std::move(*it));
  archived_tracks_.erase(it);
  reindex_active_tracks();

  for (Node& node : nodes_) {
    node.ensure_lane(track_id);
  }

  return Result();
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
