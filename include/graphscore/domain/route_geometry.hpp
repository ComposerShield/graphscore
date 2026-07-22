// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// One interior point of a customized connector route, in the same
// writer-only 2D canvas space as GraphPosition (graph_position.hpp): a
// plain double pair, never a Rational, since it carries no structural or
// runtime meaning.
struct RoutePoint {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool operator==(const RoutePoint&) const = default;
};

// The on-canvas path of one connector edge, independent of runtime
// semantics: either automatic (the writer computes it fresh from the
// endpoints on every layout) or user-customized (an explicit ordered list
// of interior waypoints between the fixed connector endpoints, which the
// writer must honor and which survive node moves). Product decision:
// routes are "straight or orthogonal with any number of 90-degree bends
// and rounded corners" -- this stores only the interior bend points and
// validates them as axis-aligned; corner rounding and endpoint placement
// are rendering concerns entirely outside the domain model.
class RouteGeometry {
 public:
  RouteGeometry() = default;

  [[nodiscard]] bool is_automatic() const noexcept { return !customized_; }

  [[nodiscard]] const std::vector<RoutePoint>& waypoints() const noexcept {
    return waypoints_;
  }

  // Records `waypoints` as the customized interior route. Fails, leaving
  // the route unchanged, if any coordinate is non-finite, if any
  // consecutive pair is identical (a zero-length segment), or if any
  // consecutive pair shares neither an x nor a y coordinate, so every
  // interior segment stays a well-formed, axis-aligned (orthogonal) bend.
  [[nodiscard]] Result set_custom_route(std::vector<RoutePoint> waypoints);

  // Discards any customization; the route is computed automatically again.
  void reset_to_automatic() noexcept {
    customized_ = false;
    waypoints_.clear();
  }

  [[nodiscard]] bool operator==(const RouteGeometry&) const = default;

 private:
  bool                    customized_ = false;
  std::vector<RoutePoint> waypoints_;
};

}  // namespace graphscore
