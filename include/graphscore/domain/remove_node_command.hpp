// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

// Permanently deletes a node identified by its stable NodeId (see
// Project::remove_node). Unlike a track, a node has no archived state --
// this is a genuine user-facing delete (docs/plan/06-graph-canvas.md:
// "delete nodes with deterministic connector remapping"). Ids are
// preserved on undo/redo; remapping connector ids is a copy/paste concern,
// entirely out of scope here.
//
// Reversibility subtlety: Project::remove_node clears (to no destination)
// every OTHER node's output whose destination targeted one of the removed
// node's inputs, and clearing a destination also resets that output's
// RouteGeometry to automatic (OutputConnector::set_destination); it also
// clears the project's start node if it referenced the removed node. This
// command snapshots, on first successful execute: the removed node's
// complete value (lanes, timeline, its own connectors -- including any
// self-loop destination targeting one of its own inputs, which vanishes
// with the node and is restored as part of this Node value, never touched
// by the cascade below), whether it was the start node, and, for every
// cross-node output the removal cascade will clear, its source node/
// connector id, the destination input connector id, and its prior
// RouteGeometry.
//
// Undo restores the node via Project::restore_node -- a pure value
// reinsertion that does not re-align lanes, relying on CommandHistory's
// linear-undo guarantee that the active-track set is unchanged between
// this command's execute and its undo -- restores the start node if it
// was one, then reconnects every snapshotted cross-node output via
// Graph::connect and restores each output's exact prior route. Redo
// re-runs Project::remove_node.
class RemoveNodeCommand : public Command {
 public:
  explicit RemoveNodeCommand(NodeId node_id) : node_id_(node_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  struct ClearedInboundEdge {
    NodeId        source_node;
    ConnectorId   source_output;
    ConnectorId   dest_input;
    RouteGeometry route;
  };

  NodeId                          node_id_;
  std::optional<Node>             removed_node_;
  bool                            was_start_ = false;
  std::vector<ClearedInboundEdge> cleared_inbound_;
  State                           state_ = State::kFresh;
};

}  // namespace graphscore
