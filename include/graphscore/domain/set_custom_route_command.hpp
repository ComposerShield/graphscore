// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

// Sets a customized interior route for an output connector (see
// RouteGeometry::set_custom_route). Fails, leaving the route unchanged, if
// the node/output id is unknown or the waypoints are rejected (non-finite
// coordinate, zero-length segment, or non-axis-aligned segment). Snapshots
// the old RouteGeometry -- whether automatic or already customized -- on
// first successful execution.
class SetCustomRouteCommand : public Command {
 public:
  SetCustomRouteCommand(NodeId node_id, ConnectorId output_id,
                        std::vector<RoutePoint> waypoints)
      : node_id_(node_id),
        output_id_(output_id),
        new_waypoints_(std::move(waypoints)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                  node_id_;
  ConnectorId             output_id_;
  std::vector<RoutePoint> new_waypoints_;
  RouteGeometry           old_route_;
  State                   state_ = State::kFresh;
};

}  // namespace graphscore
