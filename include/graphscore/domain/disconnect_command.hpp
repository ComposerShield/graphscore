// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

// Removes the destination edge of an output connector (see
// Graph::disconnect). Fails, leaving the connector unchanged, if the node/
// connector id is unknown or the output has no destination to remove.
//
// Reversibility subtlety: Graph::disconnect resets the output's
// RouteGeometry to automatic (OutputConnector::set_destination). This
// command snapshots both the prior ConnectorDestination and the prior
// RouteGeometry on first successful execute, so undo can restore the exact
// destination edge and route the output had before the disconnect.
class DisconnectCommand : public Command {
 public:
  DisconnectCommand(NodeId source_node, ConnectorId source_output)
      : source_node_(source_node), source_output_(source_output) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId               source_node_;
  ConnectorId          source_output_;
  ConnectorDestination old_dest_{};
  RouteGeometry        old_route_;
  State                state_ = State::kFresh;
};

}  // namespace graphscore
