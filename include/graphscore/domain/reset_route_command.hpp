// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

// Discards an output connector's customized route, reverting it to
// automatic (see RouteGeometry::reset_to_automatic). Fails, without
// mutation, if the node/output id is unknown. Snapshots the old
// RouteGeometry -- which must have been customized for this command to be
// meaningful, but is snapshotted verbatim regardless -- on first successful
// execution.
class ResetRouteCommand : public Command {
 public:
  ResetRouteCommand(NodeId node_id, ConnectorId output_id)
      : node_id_(node_id), output_id_(output_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId        node_id_;
  ConnectorId   output_id_;
  RouteGeometry old_route_;
  State         state_ = State::kFresh;
};

}  // namespace graphscore
