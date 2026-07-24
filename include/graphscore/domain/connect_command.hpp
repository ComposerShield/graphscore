// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

// Connects an output connector to a destination input on (possibly the
// same) node (see Graph::connect). Fails, leaving both connectors
// unchanged, if either id is unknown or if the source output already has a
// destination.
//
// Reversibility subtlety: undo goes through Graph::disconnect, which also
// resets the source output's RouteGeometry to automatic
// (OutputConnector::set_destination). connect() itself never touches the
// route. This command snapshots the source output's route once, on first
// successful execute, and restores it after every disconnect performed by
// undo, so a route customized before connecting survives an undo/redo
// cycle intact.
class ConnectCommand : public Command {
 public:
  ConnectCommand(NodeId source_node, ConnectorId source_output,
                 NodeId dest_node, ConnectorId dest_input)
      : source_node_(source_node),
        source_output_(source_output),
        dest_node_(dest_node),
        dest_input_(dest_input) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId        source_node_;
  ConnectorId   source_output_;
  NodeId        dest_node_;
  ConnectorId   dest_input_;
  RouteGeometry old_route_;
  State         state_ = State::kFresh;
};

}  // namespace graphscore
