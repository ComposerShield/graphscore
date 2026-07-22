// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/node.hpp>

#include <string>
#include <utility>

namespace graphscore {

Node::Node(NodeId id, std::string name) : id_(id), name_(std::move(name)) {}

bool Node::has_lane(TrackId track_id) const {
  return lanes_.contains(track_id);
}

TrackLane* Node::lane(TrackId track_id) {
  const auto it = lanes_.find(track_id);
  return it == lanes_.end() ? nullptr : &it->second;
}

const TrackLane* Node::lane(TrackId track_id) const {
  const auto it = lanes_.find(track_id);
  return it == lanes_.end() ? nullptr : &it->second;
}

void Node::ensure_lane(TrackId track_id) {
  lanes_.try_emplace(track_id);
}

}  // namespace graphscore
