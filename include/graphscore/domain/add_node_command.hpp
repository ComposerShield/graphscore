// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>

namespace graphscore {

// Adds a new, authorable node with the given name. The node contains one
// complete default measure, every active track/stave, and an initial tempo.
// When source_node is present, the exact authored Tempo governing the end of
// that node's main region is inherited; otherwise the project default is used.
//
// Reversibility: execute mints the node's id and snapshots the complete
// initialized node on first success. Undo removes the node through
// Project::remove_node -- a freshly added node has no inbound edges from
// any other node and is never the project's start node, so remove_node's
// cascade and start-node clearing are both no-ops here. Redo restores the
// complete snapshot, preserving identity, track/stave structure, timeline,
// and inherited tempo even if project defaults have since changed.
class AddNodeCommand : public Command {
 public:
  explicit AddNodeCommand(
      std::string           name        = {},
      std::optional<NodeId> source_node = std::nullopt) noexcept
      : name_(std::move(name)), source_node_(source_node) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  std::string           name_;
  std::optional<NodeId> source_node_;
  std::optional<Node>   created_node_;
  State                 state_ = State::kFresh;
};

}  // namespace graphscore
