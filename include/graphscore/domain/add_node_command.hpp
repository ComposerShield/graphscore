// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Adds a new node with the given name (see Project::add_node).
//
// Reversibility: execute mints the node's id via Project::add_node and
// snapshots it on first success. Undo removes the node through
// Project::remove_node -- a freshly added node has no inbound edges from
// any other node and is never the project's start node, so remove_node's
// cascade and start-node clearing are both no-ops here. Redo re-adds the
// exact same name through Project::add_node_with_id, preserving the same
// NodeId rather than minting a fresh one; the recreated node's lanes are
// empty and aligned to the current active-track set, exactly the state a
// freshly added node was in.
class AddNodeCommand : public Command {
 public:
  explicit AddNodeCommand(std::string name = {}) : name_(std::move(name)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  std::string           name_;
  std::optional<NodeId> created_id_;
  State                 state_ = State::kFresh;
};

}  // namespace graphscore
