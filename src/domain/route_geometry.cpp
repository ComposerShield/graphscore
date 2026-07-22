// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/route_geometry.hpp>

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace graphscore {

Result RouteGeometry::set_custom_route(std::vector<RoutePoint> waypoints) {
  for (const RoutePoint& point : waypoints) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
      return Result(ResultCode::kInvalidArgument);
  }

  for (std::size_t i = 1; i < waypoints.size(); ++i) {
    const RoutePoint& previous = waypoints[i - 1];
    const RoutePoint& current  = waypoints[i];
    if (previous == current)
      return Result(ResultCode::kInvalidArgument);
    if (previous.x != current.x && previous.y != current.y)
      return Result(ResultCode::kInvalidArgument);
  }

  waypoints_  = std::move(waypoints);
  customized_ = true;
  return Result();
}

}  // namespace graphscore
