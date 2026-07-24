// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

// Removes an input connector, identified by its stable ConnectorId, from a
// node identified by its stable NodeId (see Graph::remove_input). Fails,
// leaving the graph unchanged, if `node_id`/`input_id` is unknown.
//
// Reversibility subtlety: Graph::remove_input clears (to no destination)
// every output connector across the whole project whose destination
// referenced the removed input, and clearing a destination also resets
// that output's RouteGeometry to automatic
// (OutputConnector::set_destination). This command snapshots the removed
// InputConnector value plus, for every output the removal cascade will
// clear, its source node/connector id and its RouteGeometry, on first
// successful execute. Undo restores the input via Node::restore_input,
// then reconnects every snapshotted output to it via Graph::connect and
// restores each output's exact prior route. Redo re-runs the same cascade
// removal.
class RemoveInputConnectorCommand : public Command {
 public:
  RemoveInputConnectorCommand(NodeId node_id, ConnectorId input_id)
      : node_id_(node_id), input_id_(input_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  struct ClearedEdge {
    NodeId        source_node;
    ConnectorId   source_output;
    RouteGeometry route;
  };

  NodeId                        node_id_;
  ConnectorId                   input_id_;
  std::optional<InputConnector> removed_;
  std::vector<ClearedEdge>      cleared_;
  State                         state_ = State::kFresh;
};

}  // namespace graphscore
