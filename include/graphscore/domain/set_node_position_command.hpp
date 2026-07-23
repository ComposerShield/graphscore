// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/graph_position.hpp>

namespace graphscore {

// Sets the graph-canvas position of a node identified by its stable
// NodeId. Snapshots the old position on first successful execution.
//
// Missing NodeId returns kInvalidArgument without mutation.
class SetNodePositionCommand : public Command {
 public:
  SetNodePositionCommand(NodeId node_id, GraphPosition new_position)
      : node_id_(node_id), new_position_(new_position) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId        node_id_;
  GraphPosition new_position_;
  GraphPosition old_position_;
  State         state_ = State::kFresh;
};

}  // namespace graphscore
