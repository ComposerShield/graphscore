// SPDX-License-Identifier: Apache-2.0

#include <graphscore/accessibility/graphscore_accessibility.hpp>
#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/bind_output_event_command.hpp>
#include <graphscore/domain/command_transaction.hpp>
#include <graphscore/domain/connect_command.hpp>
#include <graphscore/domain/disconnect_command.hpp>
#include <graphscore/domain/duplicate_nodes_command.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/remove_node_command.hpp>
#include <graphscore/domain/reset_route_command.hpp>
#include <graphscore/domain/set_custom_route_command.hpp>
#include <graphscore/domain/set_listener_policy_command.hpp>
#include <graphscore/domain/set_node_position_command.hpp>
#include <graphscore/domain/set_output_connector_name_command.hpp>
#include <graphscore/domain/set_output_priority_command.hpp>
#include <graphscore/domain/set_output_type_command.hpp>
#include <graphscore/domain/set_output_weight_command.hpp>
#include <graphscore/domain/validation_service.hpp>
#include <graphscore/notation/notation_selection.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

static_assert(!std::is_copy_constructible_v<TrackpadGestureController>);
static_assert(!std::is_move_constructible_v<TrackpadGestureController>);
static_assert(!std::is_copy_assignable_v<TrackpadGestureController>);
static_assert(!std::is_move_assignable_v<TrackpadGestureController>);
static_assert(!std::is_copy_constructible_v<CanvasNavigationController>);
static_assert(!std::is_move_constructible_v<CanvasNavigationController>);

namespace {
constexpr int kCanvasVersion = 1;

[[nodiscard]] bool is_finite(GraphPosition position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y);
}

[[nodiscard]] bool is_finite(ViewportPosition position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y);
}

[[nodiscard]] constexpr unsigned char fold_ascii(unsigned char value) noexcept {
  if (value >= static_cast<unsigned char>('A') &&
      value <= static_cast<unsigned char>('Z')) {
    return static_cast<unsigned char>(value + ('a' - 'A'));
  }
  return value;
}

[[nodiscard]] bool contains_ascii_case_insensitive(
    std::string_view text, std::string_view query) noexcept {
  if (query.empty()) {
    return true;
  }
  if (query.size() > text.size()) {
    return false;
  }
  for (std::size_t start = 0; start + query.size() <= text.size(); ++start) {
    bool matches = true;
    for (std::size_t offset = 0; offset < query.size(); ++offset) {
      const auto text_byte  = static_cast<unsigned char>(text[start + offset]);
      const auto query_byte = static_cast<unsigned char>(query[offset]);
      if (fold_ascii(text_byte) != fold_ascii(query_byte)) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool contains(const NotationRect& bounds,
                            GraphPosition       point) noexcept {
  if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
      !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
      bounds.width < 0.0 || bounds.height < 0.0 || !is_finite(point)) {
    return false;
  }
  const double right  = bounds.x + bounds.width;
  const double bottom = bounds.y + bounds.height;
  return std::isfinite(right) && std::isfinite(bottom) && point.x >= bounds.x &&
         point.x <= right && point.y >= bounds.y && point.y <= bottom;
}

[[nodiscard]] CanvasNodeHeader node_header(const Node&               node,
                                           CanvasNodeValidationState validation,
                                           double                    width) {
  constexpr double kButtonGap     = 8.0;
  constexpr double kButtonPadding = 16.0;
  constexpr double kButtonY =
      (CanvasNodeGeometry::kHeaderHeight - CanvasNodeHeaderButton::kSize) / 2.0;
  const double play_x  = width - kButtonPadding - CanvasNodeHeaderButton::kSize;
  const double tempo_x = play_x - kButtonGap - CanvasNodeHeaderButton::kSize;
  const double notes_x = tempo_x - kButtonGap - CanvasNodeHeaderButton::kSize;
  return CanvasNodeHeader{
      node.name(),
      node.color(),
      !node.notes().empty(),
      validation,
      node.timeline() != nullptr && node.timeline()->tempo() != nullptr,
      {CanvasNodeHeaderAction::kEditFreeformNotes,
       {notes_x, kButtonY, CanvasNodeHeaderButton::kSize,
        CanvasNodeHeaderButton::kSize}},
      {CanvasNodeHeaderAction::kOpenTempoLane,
       {tempo_x, kButtonY, CanvasNodeHeaderButton::kSize,
        CanvasNodeHeaderButton::kSize}},
      {CanvasNodeHeaderAction::kPlay,
       {play_x, kButtonY, CanvasNodeHeaderButton::kSize,
        CanvasNodeHeaderButton::kSize}}};
}

[[nodiscard]] bool diagnostic_applies_to_node(const Diagnostic& diagnostic,
                                              NodeId node_id) noexcept {
  const NodeId* const subject = std::get_if<NodeId>(&diagnostic.entity);
  return (subject != nullptr && *subject == node_id) ||
         diagnostic.node == node_id;
}

[[nodiscard]] CanvasNodeValidationState validation_state_for_node(
    const ValidationReport& report, NodeId node_id) noexcept {
  CanvasNodeValidationState state = CanvasNodeValidationState::kValid;
  for (const Diagnostic& diagnostic : report.diagnostics) {
    if (!diagnostic_applies_to_node(diagnostic, node_id)) {
      continue;
    }
    if (diagnostic.severity == DiagnosticSeverity::kError) {
      return CanvasNodeValidationState::kError;
    }
    state = CanvasNodeValidationState::kWarning;
  }
  return state;
}

[[nodiscard]] CanvasNodeGeometry node_geometry(
    GraphPosition                        position,
    const std::optional<NotationLayout>& layout) noexcept {
  const double notation_width = layout.has_value() ? layout->bounds.width : 0.0;
  const double content_height =
      layout.has_value() ? layout->bounds.height
                         : CanvasNodeGeometry::kFallbackContentHeight;
  const double width =
      std::max(CanvasNodeGeometry::kMinimumWidth, notation_width);

  return CanvasNodeGeometry{
      {position, width, CanvasNodeGeometry::kHeaderHeight + content_height},
      {0.0, 0.0, width, CanvasNodeGeometry::kHeaderHeight},
      {0.0, CanvasNodeGeometry::kHeaderHeight, width, content_height},
      {0.0, CanvasNodeGeometry::kHeaderHeight, notation_width,
       layout.has_value() ? layout->bounds.height : 0.0}};
}

[[nodiscard]] AccessibilityConnectorDirection accessibility_direction(
    CanvasPortDirection direction) noexcept {
  return direction == CanvasPortDirection::kInput
             ? AccessibilityConnectorDirection::kInput
             : AccessibilityConnectorDirection::kOutput;
}

template <typename Connector>
void append_ports(std::vector<CanvasNodePort>& ports, NodeId node_id,
                  const std::vector<Connector>& connectors,
                  CanvasPortDirection direction, double node_width,
                  double node_height) {
  const double x       = direction == CanvasPortDirection::kInput
                             ? -CanvasNodePort::kDiameter / 2.0
                             : node_width - CanvasNodePort::kDiameter / 2.0;
  const double divisor = static_cast<double>(connectors.size() + 1U);
  const auto   accessible_direction = accessibility_direction(direction);
  for (std::size_t index = 0; index < connectors.size(); ++index) {
    const Connector& connector = connectors[index];
    const double     center_y =
        node_height * static_cast<double>(index + 1U) / divisor;
    ports.push_back(CanvasNodePort{
        connector.id(),
        direction,
        connector.name(),
        connector_accessibility_id(node_id, connector.id(),
                                   accessible_direction),
        connector_accessibility_label(connector.name(), accessible_direction),
        {x, center_y - CanvasNodePort::kDiameter / 2.0,
         CanvasNodePort::kDiameter, CanvasNodePort::kDiameter}});
  }
}

[[nodiscard]] std::vector<CanvasNodePort> node_ports(
    const Node& node, const CanvasNodeGeometry& geometry) {
  std::vector<CanvasNodePort> ports;
  ports.reserve(node.inputs().size() + node.outputs().size());
  append_ports(ports, node.id(), node.inputs(), CanvasPortDirection::kInput,
               geometry.bounds.width, geometry.bounds.height);
  append_ports(ports, node.id(), node.outputs(), CanvasPortDirection::kOutput,
               geometry.bounds.width, geometry.bounds.height);
  return ports;
}

[[nodiscard]] const CanvasNodeNotation* find_scene_node(
    const CanvasNotationScene& scene, NodeId node_id) noexcept {
  const auto found =
      std::ranges::find(scene.nodes, node_id, &CanvasNodeNotation::node_id);
  return found == scene.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] const CanvasNodePort* find_port(const CanvasNodeNotation& node,
                                              ConnectorId         connector,
                                              CanvasPortDirection direction) {
  const auto found = std::ranges::find_if(
      node.ports, [connector, direction](const CanvasNodePort& port) {
        return port.connector_id == connector && port.direction == direction;
      });
  return found == node.ports.end() ? nullptr : &*found;
}

[[nodiscard]] CanvasNodePort* find_port(CanvasNodeNotation& node,
                                        ConnectorId         connector,
                                        CanvasPortDirection direction) {
  const auto found = std::ranges::find_if(
      node.ports, [connector, direction](const CanvasNodePort& port) {
        return port.connector_id == connector && port.direction == direction;
      });
  return found == node.ports.end() ? nullptr : &*found;
}

[[nodiscard]] CanvasNodeNotation* find_scene_node(CanvasNotationScene& scene,
                                                  NodeId node_id) noexcept {
  const auto found =
      std::ranges::find(scene.nodes, node_id, &CanvasNodeNotation::node_id);
  return found == scene.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] bool diagnostic_applies_to_connector(
    const Diagnostic& diagnostic, NodeId source_node,
    ConnectorId output_id) noexcept {
  if (const auto* const connector =
          std::get_if<ConnectorId>(&diagnostic.entity);
      connector != nullptr && *connector == output_id) {
    return !diagnostic.node.has_value() || diagnostic.node == source_node;
  }
  const auto* const node = std::get_if<NodeId>(&diagnostic.entity);
  return node != nullptr && *node == source_node;
}

[[nodiscard]] bool valid_queue_policy(QueuePolicy policy) noexcept {
  return policy == QueuePolicy::kFirstWins ||
         policy == QueuePolicy::kLatestValidWins ||
         policy == QueuePolicy::kFifo;
}

[[nodiscard]] std::optional<CanvasConnectorEndpointLeg> endpoint_leg(
    const CanvasNodeNotation& node, ConnectorId connector,
    CanvasPortDirection direction) noexcept {
  const CanvasNodePort* const port = find_port(node, connector, direction);
  if (port == nullptr) {
    return std::nullopt;
  }
  const bool   source       = direction == CanvasPortDirection::kOutput;
  const double local_x      = port->bounds.x + port->bounds.width / 2.0;
  const double local_y      = port->bounds.y + port->bounds.height / 2.0;
  const double attachment_x = node.position.x + local_x;
  const double attachment_y = node.position.y + local_y;
  const double outer_x =
      attachment_x + (source ? CanvasConnectorGeometry::kEndpointClearance
                             : -CanvasConnectorGeometry::kEndpointClearance);
  if (!std::isfinite(attachment_x) || !std::isfinite(attachment_y) ||
      !std::isfinite(outer_x) ||
      (local_x != 0.0 && attachment_x == node.position.x) ||
      (local_y != 0.0 && attachment_y == node.position.y) ||
      outer_x == attachment_x) {
    return std::nullopt;
  }
  return CanvasConnectorEndpointLeg{{attachment_x, attachment_y},
                                    {outer_x, attachment_y}};
}

struct RouteObstacle {
  double left;
  double top;
  double right;
  double bottom;
};

[[nodiscard]] std::optional<std::vector<RouteObstacle>> route_obstacles(
    const CanvasNotationScene& scene) {
  std::vector<RouteObstacle> obstacles;
  obstacles.reserve(scene.nodes.size());
  for (const CanvasNodeNotation& node : scene.nodes) {
    const WorldBounds& bounds = node.geometry.bounds;
    const double       right  = bounds.origin.x + bounds.width;
    const double       bottom = bounds.origin.y + bounds.height;
    if (!is_finite(bounds.origin) || !std::isfinite(bounds.width) ||
        !std::isfinite(bounds.height) || !std::isfinite(right) ||
        !std::isfinite(bottom) || bounds.width < 0.0 || bounds.height < 0.0) {
      continue;
    }
    const double left_clear =
        bounds.origin.x - CanvasConnectorGeometry::kCornerRadius;
    const double top_clear =
        bounds.origin.y - CanvasConnectorGeometry::kCornerRadius;
    const double right_clear  = right + CanvasConnectorGeometry::kCornerRadius;
    const double bottom_clear = bottom + CanvasConnectorGeometry::kCornerRadius;
    if (!std::isfinite(left_clear) || !std::isfinite(top_clear) ||
        !std::isfinite(right_clear) || !std::isfinite(bottom_clear)) {
      continue;
    }
    obstacles.push_back(
        RouteObstacle{left_clear, top_clear, right_clear, bottom_clear});
  }
  return obstacles;
}

[[nodiscard]] bool point_clear(GraphPosition                     point,
                               const std::vector<RouteObstacle>& obstacles) {
  return std::ranges::none_of(
      obstacles, [point](const RouteObstacle& obstacle) {
        return point.x > obstacle.left && point.x < obstacle.right &&
               point.y > obstacle.top && point.y < obstacle.bottom;
      });
}

[[nodiscard]] bool obstacle_contains(GraphPosition        point,
                                     const RouteObstacle& obstacle) noexcept {
  return point.x > obstacle.left && point.x < obstacle.right &&
         point.y > obstacle.top && point.y < obstacle.bottom;
}

[[nodiscard]] bool segment_clear(GraphPosition first, GraphPosition second,
                                 const std::vector<RouteObstacle>& obstacles) {
  if (first.x == second.x) {
    const double low  = std::min(first.y, second.y);
    const double high = std::max(first.y, second.y);
    return std::ranges::none_of(
        obstacles, [first, low, high](const RouteObstacle& obstacle) {
          return first.x > obstacle.left && first.x < obstacle.right &&
                 low < obstacle.bottom && high > obstacle.top;
        });
  }
  if (first.y == second.y) {
    const double low  = std::min(first.x, second.x);
    const double high = std::max(first.x, second.x);
    return std::ranges::none_of(
        obstacles, [first, low, high](const RouteObstacle& obstacle) {
          return first.y > obstacle.top && first.y < obstacle.bottom &&
                 low < obstacle.right && high > obstacle.left;
        });
  }
  return false;
}

enum class RouteDirection : std::uint8_t {
  kNone = 0,
  kHorizontal,
  kVertical,
};

struct RouteCost {
  double      distance = std::numeric_limits<double>::infinity();
  std::size_t bends    = std::numeric_limits<std::size_t>::max();
};

[[nodiscard]] bool route_cost_less(RouteCost left, RouteCost right) noexcept {
  return left.distance < right.distance ||
         (left.distance == right.distance && left.bends < right.bends);
}

struct RouteQueueEntry {
  RouteCost   cost;
  std::size_t state;
};

struct RouteQueueGreater {
  [[nodiscard]] bool operator()(const RouteQueueEntry& left,
                                const RouteQueueEntry& right) const noexcept {
    if (route_cost_less(right.cost, left.cost)) {
      return true;
    }
    if (route_cost_less(left.cost, right.cost)) {
      return false;
    }
    return left.state > right.state;
  }
};

[[nodiscard]] std::vector<double> route_coordinates(
    double first, double second, const std::vector<RouteObstacle>& obstacles,
    bool horizontal) {
  std::vector<double> coordinates;
  coordinates.reserve(2U + obstacles.size() * 2U);
  coordinates.push_back(first);
  coordinates.push_back(second);
  for (const RouteObstacle& obstacle : obstacles) {
    coordinates.push_back(horizontal ? obstacle.left : obstacle.top);
    coordinates.push_back(horizontal ? obstacle.right : obstacle.bottom);
  }
  std::ranges::sort(coordinates);
  const auto unique_end = std::ranges::unique(coordinates).begin();
  coordinates.erase(unique_end, coordinates.end());
  return coordinates;
}

[[nodiscard]] std::optional<std::vector<GraphPosition>> automatic_route(
    GraphPosition start, GraphPosition finish,
    const std::vector<RouteObstacle>& obstacles) {
  std::vector<RouteObstacle> effective_obstacles;
  effective_obstacles.reserve(obstacles.size());
  for (const RouteObstacle& obstacle : obstacles) {
    // Overlapping endpoint nodes can cover the fixed clearance point of the
    // other endpoint. That node cannot be avoided until the overlap is
    // resolved, so do not let it make the retained connector disappear.
    if (!obstacle_contains(start, obstacle) &&
        !obstacle_contains(finish, obstacle)) {
      effective_obstacles.push_back(obstacle);
    }
  }
  if (start == finish) {
    return std::vector<GraphPosition>{start};
  }
  if ((start.x == finish.x || start.y == finish.y) &&
      segment_clear(start, finish, effective_obstacles)) {
    return std::vector<GraphPosition>{start, finish};
  }

  const std::vector<double> xs =
      route_coordinates(start.x, finish.x, effective_obstacles, true);
  const std::vector<double> ys =
      route_coordinates(start.y, finish.y, effective_obstacles, false);
  if (xs.empty() || ys.empty() ||
      xs.size() > std::numeric_limits<std::size_t>::max() / ys.size()) {
    return std::nullopt;
  }
  const std::size_t     point_count     = xs.size() * ys.size();
  constexpr std::size_t kDirectionCount = 3U;
  if (point_count > std::numeric_limits<std::size_t>::max() / kDirectionCount) {
    return std::nullopt;
  }
  const std::size_t state_count = point_count * kDirectionCount;
  std::vector<bool> valid_points(point_count, false);
  for (std::size_t y = 0; y < ys.size(); ++y) {
    for (std::size_t x = 0; x < xs.size(); ++x) {
      valid_points[y * xs.size() + x] =
          point_clear({xs[x], ys[y]}, effective_obstacles);
    }
  }

  const auto start_x  = std::ranges::lower_bound(xs, start.x) - xs.begin();
  const auto start_y  = std::ranges::lower_bound(ys, start.y) - ys.begin();
  const auto finish_x = std::ranges::lower_bound(xs, finish.x) - xs.begin();
  const auto finish_y = std::ranges::lower_bound(ys, finish.y) - ys.begin();
  const std::size_t start_point =
      static_cast<std::size_t>(start_y) * xs.size() +
      static_cast<std::size_t>(start_x);
  const std::size_t finish_point =
      static_cast<std::size_t>(finish_y) * xs.size() +
      static_cast<std::size_t>(finish_x);
  const std::size_t start_state = start_point * kDirectionCount;

  std::vector<RouteCost>   costs(state_count);
  std::vector<std::size_t> parents(state_count,
                                   std::numeric_limits<std::size_t>::max());
  std::priority_queue<RouteQueueEntry, std::vector<RouteQueueEntry>,
                      RouteQueueGreater>
      queue;
  costs[start_state] = {0.0, 0U};
  queue.push({costs[start_state], start_state});

  const auto relax = [&](std::size_t from_state, std::size_t to_point,
                         RouteDirection direction, double length,
                         auto& pending) {
    if (!std::isfinite(length) || length <= 0.0) {
      return;
    }
    const auto prior_direction =
        static_cast<RouteDirection>(from_state % kDirectionCount);
    const std::size_t to_state =
        to_point * kDirectionCount + static_cast<std::size_t>(direction);
    const RouteCost candidate{
        costs[from_state].distance + length,
        costs[from_state].bends +
            static_cast<std::size_t>(prior_direction != RouteDirection::kNone &&
                                     prior_direction != direction)};
    if (!std::isfinite(candidate.distance) ||
        !route_cost_less(candidate, costs[to_state])) {
      return;
    }
    costs[to_state]   = candidate;
    parents[to_state] = from_state;
    pending.push(RouteQueueEntry{candidate, to_state});
  };

  while (!queue.empty()) {
    const RouteQueueEntry current = queue.top();
    queue.pop();
    if (current.cost.distance != costs[current.state].distance ||
        current.cost.bends != costs[current.state].bends) {
      continue;
    }
    const std::size_t   point = current.state / kDirectionCount;
    const std::size_t   x     = point % xs.size();
    const std::size_t   y     = point / xs.size();
    const GraphPosition position{xs[x], ys[y]};
    if (x > 0U && valid_points[point - 1U] &&
        segment_clear(position, {xs[x - 1U], ys[y]}, effective_obstacles)) {
      relax(current.state, point - 1U, RouteDirection::kHorizontal,
            xs[x] - xs[x - 1U], queue);
    }
    if (x + 1U < xs.size() && valid_points[point + 1U] &&
        segment_clear(position, {xs[x + 1U], ys[y]}, effective_obstacles)) {
      relax(current.state, point + 1U, RouteDirection::kHorizontal,
            xs[x + 1U] - xs[x], queue);
    }
    if (y > 0U && valid_points[point - xs.size()] &&
        segment_clear(position, {xs[x], ys[y - 1U]}, effective_obstacles)) {
      relax(current.state, point - xs.size(), RouteDirection::kVertical,
            ys[y] - ys[y - 1U], queue);
    }
    if (y + 1U < ys.size() && valid_points[point + xs.size()] &&
        segment_clear(position, {xs[x], ys[y + 1U]}, effective_obstacles)) {
      relax(current.state, point + xs.size(), RouteDirection::kVertical,
            ys[y + 1U] - ys[y], queue);
    }
  }

  std::size_t finish_state =
      finish_point * kDirectionCount +
      static_cast<std::size_t>(RouteDirection::kHorizontal);
  const std::size_t vertical_finish =
      finish_point * kDirectionCount +
      static_cast<std::size_t>(RouteDirection::kVertical);
  if (route_cost_less(costs[vertical_finish], costs[finish_state])) {
    finish_state = vertical_finish;
  }
  if (!std::isfinite(costs[finish_state].distance)) {
    return std::nullopt;
  }

  std::vector<GraphPosition> reversed;
  for (std::size_t state = finish_state;; state = parents[state]) {
    const std::size_t point = state / kDirectionCount;
    reversed.push_back({xs[point % xs.size()], ys[point / xs.size()]});
    if (state == start_state) {
      break;
    }
    if (parents[state] == std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
  }
  std::ranges::reverse(reversed);

  std::vector<GraphPosition> simplified;
  simplified.reserve(reversed.size());
  for (GraphPosition point : reversed) {
    if (simplified.size() >= 2U) {
      const GraphPosition before = simplified[simplified.size() - 2U];
      const GraphPosition last   = simplified.back();
      if ((before.x == last.x && last.x == point.x) ||
          (before.y == last.y && last.y == point.y)) {
        simplified.back() = point;
        continue;
      }
    }
    simplified.push_back(point);
  }
  return simplified;
}

[[nodiscard]] std::optional<std::vector<GraphPosition>> customized_route(
    GraphPosition start, GraphPosition finish,
    std::span<const RoutePoint>       waypoints,
    const std::vector<RouteObstacle>& obstacles) {
  const auto append_route = [](std::vector<GraphPosition>&    target,
                               std::span<const GraphPosition> addition) {
    for (const GraphPosition point : addition) {
      if (target.empty() || target.back() != point) {
        target.push_back(point);
      }
    }
  };
  const auto append_repair = [&](std::vector<GraphPosition>& target,
                                 GraphPosition from, GraphPosition to) {
    const auto repair = automatic_route(from, to, obstacles);
    if (!repair.has_value()) {
      return false;
    }
    append_route(target, *repair);
    return true;
  };

  std::vector<GraphPosition> route;
  route.reserve(waypoints.size() + 2U);
  route.push_back(start);
  GraphPosition anchor         = start;
  bool          repair_pending = true;
  for (const RoutePoint waypoint : waypoints) {
    const GraphPosition point{waypoint.x, waypoint.y};
    if (!point_clear(point, obstacles)) {
      repair_pending = true;
      continue;
    }
    if (repair_pending || !segment_clear(anchor, point, obstacles)) {
      if (!append_repair(route, anchor, point)) {
        return std::nullopt;
      }
    } else if (route.back() != point) {
      route.push_back(point);
    }
    anchor         = point;
    repair_pending = false;
  }
  if (!append_repair(route, anchor, finish)) {
    return std::nullopt;
  }
  return route;
}

[[nodiscard]] std::optional<CanvasConnectorGeometry> connector_geometry(
    const CanvasNotationScene& scene, NodeId source_node,
    ConnectorId source_connector, const ConnectorDestination& destination,
    ConnectorType type, const RouteGeometry& route_geometry) {
  const CanvasNodeNotation* const source = find_scene_node(scene, source_node);
  const CanvasNodeNotation* const target =
      find_scene_node(scene, destination.node);
  if (source == nullptr || target == nullptr) {
    return std::nullopt;
  }
  const auto source_leg =
      endpoint_leg(*source, source_connector, CanvasPortDirection::kOutput);
  const auto destination_leg =
      endpoint_leg(*target, destination.connector, CanvasPortDirection::kInput);
  if (!source_leg.has_value() || !destination_leg.has_value()) {
    return std::nullopt;
  }
  const auto obstacles = route_obstacles(scene);
  if (!obstacles.has_value()) {
    return std::nullopt;
  }
  std::optional<std::vector<GraphPosition>> route;
  if (route_geometry.is_automatic()) {
    route =
        automatic_route(source_leg->outer, destination_leg->outer, *obstacles);
  } else if (route_geometry.waypoints().empty()) {
    // An empty customized route is the explicit straight-path form. Keep it
    // distinct from automatic routing so an obstacle does not erase a user's
    // cleared bends on the next relayout.
    if (source_leg->outer == destination_leg->outer) {
      route = std::vector<GraphPosition>{source_leg->outer};
    } else if (source_leg->outer.x == destination_leg->outer.x ||
               source_leg->outer.y == destination_leg->outer.y) {
      route =
          std::vector<GraphPosition>{source_leg->outer, destination_leg->outer};
    } else {
      route = std::vector<GraphPosition>{
          source_leg->outer,
          {destination_leg->outer.x, source_leg->outer.y},
          destination_leg->outer};
    }
  } else {
    try {
      route = customized_route(source_leg->outer, destination_leg->outer,
                               route_geometry.waypoints(), *obstacles);
    } catch (...) {
      return std::nullopt;
    }
  }
  if (!route.has_value()) {
    return std::nullopt;
  }
  std::vector<GraphPosition> route_points;
  route_points.reserve(route->size() + 2U);
  route_points.push_back(source_leg->attachment);
  route_points.insert(route_points.end(), route->begin(), route->end());
  route_points.push_back(destination_leg->attachment);
  auto render_path = canvas_connector_render_path(route_points);
  return CanvasConnectorGeometry{source_node,
                                 source_connector,
                                 destination.node,
                                 destination.connector,
                                 *source_leg,
                                 *destination_leg,
                                 {destination_leg->outer},
                                 std::move(route_points),
                                 std::move(render_path),
                                 type,
                                 canvas_connector_style(type)};
}

[[nodiscard]] bool scene_connectors_match_project(
    const CanvasNotationScene& scene, const Project& project) noexcept {
  std::size_t scene_index = 0;
  for (const Node& node : project.nodes()) {
    for (const OutputConnector& output : node.outputs()) {
      if (!output.destination().has_value()) {
        continue;
      }
      if (scene_index >= scene.connectors.size()) {
        return false;
      }
      const CanvasConnectorGeometry& geometry = scene.connectors[scene_index];
      if (geometry.source_node != node.id() ||
          geometry.source_connector != output.id() ||
          geometry.destination_node != output.destination()->node ||
          geometry.destination_connector != output.destination()->connector ||
          geometry.type != output.type() ||
          geometry.style != canvas_connector_style(output.type())) {
        return false;
      }
      ++scene_index;
    }
  }
  return scene_index == scene.connectors.size();
}

[[nodiscard]] bool scene_nodes_match_project(const CanvasNotationScene& scene,
                                             const Project& project) noexcept {
  if (scene.nodes.size() != project.nodes().size()) {
    return false;
  }
  return std::ranges::all_of(scene.nodes, [&project](const auto& scene_node) {
    const Node* const node = project.find_node(scene_node.node_id);
    return node != nullptr && scene_node.position == node->position() &&
           scene_node.geometry.bounds.origin == scene_node.position;
  });
}

[[nodiscard]] std::optional<std::size_t> connector_insertion_index(
    const Project& project, NodeId source_node,
    ConnectorId source_output) noexcept {
  std::size_t connected_before = 0;
  for (const Node& node : project.nodes()) {
    for (const OutputConnector& output : node.outputs()) {
      if (node.id() == source_node && output.id() == source_output) {
        return connected_before;
      }
      if (output.destination().has_value()) {
        ++connected_before;
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool refresh_connector_routes(CanvasNotationScene& scene,
                                            const Project& project) noexcept {
  try {
    std::vector<CanvasConnectorGeometry> refreshed;
    refreshed.reserve(scene.connectors.size());
    for (const CanvasConnectorGeometry& connector : scene.connectors) {
      const ConnectorDestination destination{connector.destination_node,
                                             connector.destination_connector};
      const Node* const source = project.find_node(connector.source_node);
      const OutputConnector* const output =
          source == nullptr ? nullptr
                            : source->find_output(connector.source_connector);
      if (output == nullptr) {
        return false;
      }
      auto geometry = connector_geometry(
          scene, connector.source_node, connector.source_connector, destination,
          connector.type, output->route());
      if (!geometry.has_value()) {
        return false;
      }
      refreshed.push_back(std::move(*geometry));
    }
    scene.connectors = std::move(refreshed);
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] std::optional<double> map_forward_raw(
    double value, double input_anchor, double scale,
    double output_anchor) noexcept {
  const double delta = value - input_anchor;
  if (!std::isfinite(delta)) {
    return std::nullopt;
  }
  const double mapped = std::fma(delta, scale, output_anchor);
  if (!std::isfinite(mapped)) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] std::optional<double> map_inverse_raw(
    double value, double input_anchor, double scale,
    double output_anchor) noexcept {
  const double delta = value - input_anchor;
  if (!std::isfinite(delta)) {
    return std::nullopt;
  }
  const double scaled = delta / scale;
  if (!std::isfinite(scaled)) {
    return std::nullopt;
  }
  const double mapped = scaled + output_anchor;
  if (!std::isfinite(mapped)) {
    return std::nullopt;
  }
  return mapped;
}

// A forward→inverse (or inverse→forward) round trip is not bit-exact for
// most zoom scales: `fma(delta, scale, anchor)` forward composed with the
// `(mapped - anchor)/scale + anchor` inverse introduces rounding at the
// magnitude of the operands. Requiring exact equality here therefore rejected
// ordinary pinch-zoom increments — `to_viewport` returned nullopt and the
// writer's render pass skipped the notation surface, which is the observed
// flicker.
//
// The acceptance criterion derives its budget from `original` and `recovered`
// alone — never from the mapping's anchors or scale, and never as a percentage
// of the value. Both of those alternatives admit a genuine many-to-one
// collapse because they grow without bound as the operands grow:
//
//   * A nonzero value must survive within a small ULP budget of its own
//     magnitude. A relative tolerance (one part per billion) admits millions
//     of ULPs: at input_anchor 2^622, scale 1, output_anchor 0 the values
//     2^600 and 2^600 + 2^568 forward-map to the same image, and the second
//     recovers with relative error ~2.33e-10 — comfortably inside 1e-9 — even
//     though it lost 2^20 ULPs. The round-off of an otherwise-invertible
//     mapping is a handful of ULPs (measured at most ~600 across the ordinary
//     pinch-zoom sweep), while a collapse absorbs value bits into a far larger
//     magnitude, so its recovery error is millions to quadrillions of ULPs and
//     fails this budget no matter how large the anchors are.
//
//   * Zero is exactly representable and `0 - input_anchor` is exact, but a
//     *nonzero* value can still be absorbed next to it and forward-map to the
//     same image: at input_anchor 3*2^1000, scale 2^-100, output_anchor 2^954
//     both 0 and -2^1000 map identically and inverse-recover as -2^1000. Zero
//     is therefore admitted only against a small absolute arithmetic-noise
//     floor — enough for the ordinary canvas-origin round-trip, whose operands
//     are window-sized and land within ~1e-12 of zero — never against a floor
//     scaled by the anchors, which that counterexample inflates past DBL_MAX
//     to infinity. The floor is symmetric around zero, so a recovered ±0 is
//     accepted for either sign of zero (`-0.0 == 0.0` is the zero case).
//
// Non-finite operands are rejected before this point (the finite checks in the
// raw helpers and the exact-absorption check in the callers), so `original`
// and `recovered` are always finite. Their difference can still overflow to
// ±inf — e.g. a sign flip at DBL_MAX — but ±inf fails any finite budget, so
// the comparison below is well-defined and correctly rejects it.
[[nodiscard]] bool round_trip_acceptable(double original,
                                         double recovered) noexcept {
  if (original == 0.0) {
    constexpr double kZeroNoiseFloor = 1e-9;
    return std::abs(recovered) <= kZeroNoiseFloor;
  }
  // ULP budget: 2^12 ULPs of the larger of the two magnitudes. This is ~6.7x
  // the measured ordinary-path maximum (~600 ULPs), yet ~2.4 orders of
  // magnitude below the ~1e6-ULP collapse the large-value counterexample
  // produces (and ~12 orders below the value-1-collapses-to-0 case), so it
  // separates faithful round-trips from many-to-one collapse without ever
  // depending on the anchors or growing with the value.
  constexpr double kUlpBudget = 4096.0;
  const double magnitude = std::max(std::abs(original), std::abs(recovered));

  // One finite ULP at `magnitude`. The successor spacing
  // (nextafter(magnitude, +∞) − magnitude) is finite for every magnitude
  // except DBL_MAX, whose successor is infinity; fall back to the
  // predecessor-toward-zero spacing there, which is DBL_MAX's true ULP (2^971).
  // A non-finite ULP here would make the budget infinite and admit any
  // recovery error at ±DBL_MAX.
  const double successor =
      std::nextafter(magnitude, std::numeric_limits<double>::infinity());
  const double ulp = std::isfinite(successor)
                         ? successor - magnitude
                         : magnitude - std::nextafter(magnitude, 0.0);

  // The budget is finite by construction: ulp ≤ 2^971 and kUlpBudget = 2^12,
  // so the product is ≤ 2^983 and can never overflow. Comparing the (possibly
  // ±inf) error against it is therefore always well-defined.
  return std::abs(recovered - original) <= kUlpBudget * ulp;
}

[[nodiscard]] std::optional<double> map_forward(double value,
                                                double input_anchor,
                                                double scale,
                                                double output_anchor) noexcept {
  const auto mapped =
      map_forward_raw(value, input_anchor, scale, output_anchor);
  if (!mapped || (value != input_anchor && *mapped == output_anchor)) {
    return std::nullopt;
  }
  const auto round_trip =
      map_inverse_raw(*mapped, output_anchor, scale, input_anchor);
  if (!round_trip) {
    return std::nullopt;
  }
  if (!round_trip_acceptable(value, *round_trip)) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] std::optional<double> map_inverse(double value,
                                                double input_anchor,
                                                double scale,
                                                double output_anchor) noexcept {
  const auto mapped =
      map_inverse_raw(value, input_anchor, scale, output_anchor);
  if (!mapped || (value != input_anchor && *mapped == output_anchor)) {
    return std::nullopt;
  }
  const auto round_trip =
      map_forward_raw(*mapped, output_anchor, scale, input_anchor);
  if (!round_trip) {
    return std::nullopt;
  }
  if (!round_trip_acceptable(value, *round_trip)) {
    return std::nullopt;
  }
  return mapped;
}
}  // namespace

std::optional<ViewportPosition> ViewportTransform::to_viewport(
    GraphPosition world_position) const noexcept {
  if (!is_finite(world_position)) {
    return std::nullopt;
  }
  const auto x =
      map_forward(world_position.x, world_anchor_.x, zoom_, viewport_anchor_.x);
  const auto y =
      map_forward(world_position.y, world_anchor_.y, zoom_, viewport_anchor_.y);
  if (!x || !y) {
    return std::nullopt;
  }
  return ViewportPosition{*x, *y};
}

std::optional<GraphPosition> ViewportTransform::to_world(
    ViewportPosition viewport_position) const noexcept {
  if (!is_finite(viewport_position)) {
    return std::nullopt;
  }
  const auto x = map_inverse(viewport_position.x, viewport_anchor_.x, zoom_,
                             world_anchor_.x);
  const auto y = map_inverse(viewport_position.y, viewport_anchor_.y, zoom_,
                             world_anchor_.y);
  if (!x || !y) {
    return std::nullopt;
  }
  return GraphPosition{*x, *y};
}

bool ViewportTransform::set_anchor(GraphPosition    world_anchor,
                                   ViewportPosition viewport_anchor) noexcept {
  if (!is_finite(world_anchor) || !is_finite(viewport_anchor)) {
    return false;
  }
  world_anchor_    = world_anchor;
  viewport_anchor_ = viewport_anchor;
  return true;
}

bool ViewportTransform::pan_by(ViewportPosition viewport_delta) noexcept {
  if (!is_finite(viewport_delta)) {
    return false;
  }
  const ViewportPosition translated{viewport_anchor_.x + viewport_delta.x,
                                    viewport_anchor_.y + viewport_delta.y};
  if (!is_finite(translated) ||
      (viewport_delta.x != 0.0 && translated.x == viewport_anchor_.x) ||
      (viewport_delta.y != 0.0 && translated.y == viewport_anchor_.y)) {
    return false;
  }
  viewport_anchor_ = translated;
  return true;
}

bool ViewportTransform::zoom_to(double           zoom,
                                ViewportPosition focal_point) noexcept {
  if (!std::isfinite(zoom) || zoom <= 0.0 || !is_finite(focal_point)) {
    return false;
  }
  if (zoom == zoom_) {
    return true;
  }
  const auto focal_world = to_world(focal_point);
  if (!focal_world) {
    return false;
  }
  world_anchor_    = *focal_world;
  viewport_anchor_ = focal_point;
  zoom_            = zoom;
  return true;
}

bool ViewportTransform::zoom_by(double           factor,
                                ViewportPosition focal_point) noexcept {
  if (!std::isfinite(factor) || factor <= 0.0) {
    return false;
  }
  const double new_zoom = zoom_ * factor;
  if (!std::isfinite(new_zoom) || new_zoom <= 0.0) {
    return false;
  }
  if (factor != 1.0 && new_zoom == zoom_) {
    return false;
  }
  return zoom_to(new_zoom, focal_point);
}

TrackpadGestureController::TrackpadGestureController(
    ViewportTransform& transform) noexcept
    : transform_(transform) {}

bool TrackpadGestureController::pan(ScrollDelta delta) noexcept {
  return transform_.pan_by(ViewportPosition{delta.x, delta.y});
}

bool TrackpadGestureController::pinch(PinchUpdate update) noexcept {
  if (!std::isfinite(update.scale) || update.scale <= 0.0) {
    return false;
  }
  if (update.focal_point.has_value() && !is_finite(*update.focal_point)) {
    return false;
  }
  const auto focal = resolve_focal(update);
  if (!focal.has_value() || !is_finite(*focal)) {
    return false;
  }
  return transform_.zoom_by(update.scale, *focal);
}

bool TrackpadGestureController::finger_down(FingerContact finger) noexcept {
  if (!is_finite(finger.position)) {
    return false;
  }
  for (auto& slot : fingers_) {
    if (slot.has_value() && slot->finger_id == finger.finger_id) {
      slot = finger;
      return true;
    }
  }
  for (auto& slot : fingers_) {
    if (!slot.has_value()) {
      slot = finger;
      return true;
    }
  }
  return false;
}

bool TrackpadGestureController::finger_move(FingerContact finger) noexcept {
  if (!is_finite(finger.position)) {
    return false;
  }
  for (auto& slot : fingers_) {
    if (slot.has_value() && slot->finger_id == finger.finger_id) {
      slot->position = finger.position;
      return true;
    }
  }
  return false;
}

void TrackpadGestureController::finger_up(std::uint64_t finger_id) noexcept {
  for (auto& slot : fingers_) {
    if (slot.has_value() && slot->finger_id == finger_id) {
      slot.reset();
      return;
    }
  }
}

void TrackpadGestureController::cancel_tracking() noexcept {
  fingers_[0].reset();
  fingers_[1].reset();
}

void TrackpadGestureController::set_window_center(
    ViewportPosition center) noexcept {
  window_center_ = center;
}

ViewportPosition TrackpadGestureController::window_center() const noexcept {
  return window_center_;
}

std::optional<ViewportPosition> TrackpadGestureController::active_centroid()
    const noexcept {
  if (!fingers_[0].has_value() || !fingers_[1].has_value()) {
    return std::nullopt;
  }
  return ViewportPosition{
      (fingers_[0]->position.x + fingers_[1]->position.x) / 2.0,
      (fingers_[0]->position.y + fingers_[1]->position.y) / 2.0};
}

std::size_t TrackpadGestureController::tracked_finger_count() const noexcept {
  return (fingers_[0].has_value() ? 1U : 0U) +
         (fingers_[1].has_value() ? 1U : 0U);
}

std::optional<ViewportPosition> TrackpadGestureController::resolve_focal(
    const PinchUpdate& update) const noexcept {
  if (update.focal_point.has_value()) {
    return update.focal_point;
  }
  if (const auto centroid = active_centroid(); centroid.has_value()) {
    return centroid;
  }
  return window_center_;
}

CanvasNavigationController::CanvasNavigationController(
    ViewportTransform& transform) noexcept
    : transform_(transform) {}

bool CanvasNavigationController::wheel_pan(ScrollDelta delta) noexcept {
  return transform_.pan_by({delta.x, delta.y});
}

bool CanvasNavigationController::wheel_zoom(
    double delta_y, ViewportPosition focal_point) noexcept {
  if (!std::isfinite(delta_y)) {
    return false;
  }
  const double factor = std::pow(kWheelZoomStepPerUnit, delta_y);
  return transform_.zoom_by(factor, focal_point);
}

bool CanvasNavigationController::pan(ViewportPosition delta) noexcept {
  return transform_.pan_by(delta);
}

bool CanvasNavigationController::zoom_in(
    ViewportPosition focal_point) noexcept {
  return transform_.zoom_by(kKeyboardZoomStep, focal_point);
}

bool CanvasNavigationController::zoom_out(
    ViewportPosition focal_point) noexcept {
  return transform_.zoom_by(1.0 / kKeyboardZoomStep, focal_point);
}

std::vector<CanvasConnectorPathElement> canvas_connector_render_path(
    std::span<const GraphPosition> route_points) {
  if (route_points.empty()) {
    return {};
  }

  std::vector<CanvasConnectorPathElement> path;
  path.reserve(route_points.size() * 2U);
  path.push_back({CanvasConnectorPathVerb::kMove, {}, route_points.front()});
  GraphPosition current     = route_points.front();
  const auto    append_line = [&path, &current](GraphPosition end) {
    if (end != current) {
      path.push_back({CanvasConnectorPathVerb::kLine, {}, end});
      current = end;
    }
  };

  for (std::size_t index = 1U; index + 1U < route_points.size(); ++index) {
    const GraphPosition before              = route_points[index - 1U];
    const GraphPosition corner              = route_points[index];
    const GraphPosition after               = route_points[index + 1U];
    const bool          incoming_horizontal = before.y == corner.y;
    const bool          outgoing_horizontal = corner.y == after.y;
    if (incoming_horizontal == outgoing_horizontal || before == corner ||
        corner == after) {
      append_line(corner);
      continue;
    }

    const double incoming_length = incoming_horizontal
                                       ? std::abs(corner.x - before.x)
                                       : std::abs(corner.y - before.y);
    const double outgoing_length = outgoing_horizontal
                                       ? std::abs(after.x - corner.x)
                                       : std::abs(after.y - corner.y);
    const double radius =
        std::min({CanvasConnectorGeometry::kCornerRadius, incoming_length / 2.0,
                  outgoing_length / 2.0});
    const GraphPosition approach{
        corner.x - (incoming_horizontal
                        ? std::copysign(radius, corner.x - before.x)
                        : 0.0),
        corner.y - (!incoming_horizontal
                        ? std::copysign(radius, corner.y - before.y)
                        : 0.0)};
    const GraphPosition departure{
        corner.x + (outgoing_horizontal
                        ? std::copysign(radius, after.x - corner.x)
                        : 0.0),
        corner.y + (!outgoing_horizontal
                        ? std::copysign(radius, after.y - corner.y)
                        : 0.0)};
    if (approach == corner || departure == corner || approach == departure) {
      append_line(corner);
      continue;
    }
    append_line(approach);
    path.push_back({CanvasConnectorPathVerb::kQuadratic, corner, departure});
    current = departure;
  }

  if (route_points.size() > 1U) {
    append_line(route_points.back());
  }
  return path;
}

std::optional<CanvasConnectorSegmentHover> canvas_connector_segment_hover(
    std::span<const GraphPosition> route_points, GraphPosition pointer,
    double hit_tolerance) noexcept {
  if (!is_finite(pointer) || !std::isfinite(hit_tolerance) ||
      hit_tolerance < 0.0) {
    return std::nullopt;
  }

  std::optional<CanvasConnectorSegmentHover> hover;
  double                                     nearest_distance = 0.0;
  for (std::size_t index = 0U; index + 1U < route_points.size(); ++index) {
    const GraphPosition first  = route_points[index];
    const GraphPosition second = route_points[index + 1U];
    if (!is_finite(first) || !is_finite(second) || first == second) {
      continue;
    }

    const bool horizontal = first.y == second.y;
    const bool vertical   = first.x == second.x;
    if (horizontal == vertical) {
      continue;
    }

    const double parallel_value  = horizontal ? pointer.x : pointer.y;
    const double parallel_first  = horizontal ? first.x : first.y;
    const double parallel_second = horizontal ? second.x : second.y;
    if (parallel_value < std::min(parallel_first, parallel_second) ||
        parallel_value > std::max(parallel_first, parallel_second)) {
      continue;
    }

    const double distance = std::abs((horizontal ? pointer.y : pointer.x) -
                                     (horizontal ? first.y : first.x));
    if (distance > hit_tolerance ||
        (hover.has_value() && distance >= nearest_distance)) {
      continue;
    }

    hover = CanvasConnectorSegmentHover{
        index, horizontal ? CanvasCursorShape::kResizeNorthSouth
                          : CanvasCursorShape::kResizeEastWest};
    nearest_distance = distance;
  }
  return hover;
}

std::optional<std::vector<GraphPosition>> canvas_connector_drag_segment(
    std::span<const GraphPosition> route_points, std::size_t segment_index,
    GraphPosition pointer) noexcept {
  if (route_points.size() < 3U || !is_finite(pointer) ||
      segment_index >= route_points.size() - 1U) {
    return std::nullopt;
  }

  for (std::size_t index = 1U; index < route_points.size(); ++index) {
    const GraphPosition first  = route_points[index - 1U];
    const GraphPosition second = route_points[index];
    if (!is_finite(first) || !is_finite(second) || first == second ||
        (first.x != second.x && first.y != second.y)) {
      return std::nullopt;
    }
  }

  const GraphPosition first      = route_points[segment_index];
  const GraphPosition second     = route_points[segment_index + 1U];
  const bool          horizontal = first.y == second.y;
  const bool          vertical   = first.x == second.x;
  if (horizontal == vertical) {
    return std::nullopt;
  }

  try {
    std::vector<GraphPosition> edited(route_points.begin(), route_points.end());
    if (edited.size() == 3U) {
      const GraphPosition shared_outer = edited[1U];
      const double        target       = horizontal ? pointer.y : pointer.x;
      if (target == (horizontal ? shared_outer.y : shared_outer.x)) {
        return edited;
      }
      const double detour =
          horizontal
              ? shared_outer.x +
                    std::copysign(CanvasConnectorGeometry::kEndpointClearance,
                                  shared_outer.x - edited.front().x)
              : shared_outer.y +
                    std::copysign(CanvasConnectorGeometry::kEndpointClearance,
                                  shared_outer.y - edited.front().y);
      if (!std::isfinite(detour)) {
        return std::nullopt;
      }
      const GraphPosition first_bend =
          horizontal ? GraphPosition{shared_outer.x, target}
                     : GraphPosition{target, shared_outer.y};
      const GraphPosition second_bend = horizontal
                                            ? GraphPosition{detour, target}
                                            : GraphPosition{target, detour};
      const GraphPosition third_bend =
          horizontal ? GraphPosition{detour, shared_outer.y}
                     : GraphPosition{shared_outer.x, detour};
      edited.insert(edited.begin() + 2,
                    {first_bend, second_bend, third_bend, shared_outer});
      return edited;
    }
    const std::size_t last_editable_segment = edited.size() - 3U;
    double            target = horizontal ? pointer.y : pointer.x;
    const auto        constrain_endpoint_clearance =
        [&target, horizontal](GraphPosition attachment, GraphPosition outer) {
          if (horizontal) {
            return;
          }
          // The endpoint legs leave the node horizontally in the current
          // canvas model. Keep a dragged vertical segment on the outside of
          // its fixed outer point rather than allowing it back through the
          // node body.
          if (outer.x > attachment.x) {
            target = std::max(target, outer.x);
          } else {
            target = std::min(target, outer.x);
          }
        };
    if (segment_index == 1U) {
      constrain_endpoint_clearance(edited[0U], edited[1U]);
    }
    if (segment_index == last_editable_segment) {
      constrain_endpoint_clearance(edited.back(), edited[edited.size() - 2U]);
    }
    if (target == (horizontal ? first.y : first.x)) {
      return edited;
    }
    const auto moved = [horizontal, target](GraphPosition point) {
      if (horizontal) {
        point.y = target;
      } else {
        point.x = target;
      }
      return point;
    };
    const auto bend_from_source = [horizontal, target](GraphPosition point) {
      return horizontal ? GraphPosition{point.x, target}
                        : GraphPosition{target, point.y};
    };
    const auto bend_to_destination = [horizontal, target](GraphPosition point) {
      return horizontal ? GraphPosition{point.x, target}
                        : GraphPosition{target, point.y};
    };

    if (segment_index == 0U && last_editable_segment == 1U) {
      const GraphPosition source_outer      = edited[1U];
      const GraphPosition destination_outer = edited[edited.size() - 2U];
      edited.erase(edited.begin() + 2, edited.end() - 2);
      edited.insert(edited.begin() + 2,
                    {bend_from_source(source_outer),
                     bend_to_destination(destination_outer)});
    } else if (segment_index == edited.size() - 2U &&
               last_editable_segment == 1U) {
      const GraphPosition source_outer      = edited[1U];
      const GraphPosition destination_outer = edited[edited.size() - 2U];
      edited.erase(edited.begin() + 2, edited.end() - 2);
      edited.insert(edited.begin() + 2,
                    {bend_from_source(source_outer),
                     bend_to_destination(destination_outer)});
    } else if (segment_index == 0U) {
      const GraphPosition source_outer = edited[1U];
      const GraphPosition next_point   = edited[2U];
      if ((horizontal && next_point.x == source_outer.x) ||
          (vertical && next_point.y == source_outer.y)) {
        const double detour =
            horizontal
                ? source_outer.x +
                      std::copysign(CanvasConnectorGeometry::kEndpointClearance,
                                    source_outer.x - edited.front().x)
                : source_outer.y +
                      std::copysign(CanvasConnectorGeometry::kEndpointClearance,
                                    source_outer.y - edited.front().y);
        if (!std::isfinite(detour)) {
          return std::nullopt;
        }
        edited.insert(edited.begin() + 2,
                      {bend_from_source(source_outer),
                       horizontal ? GraphPosition{detour, target}
                                  : GraphPosition{target, detour},
                       horizontal ? GraphPosition{detour, next_point.y}
                                  : GraphPosition{next_point.x, detour},
                       next_point});
      } else {
        edited.insert(edited.begin() + 2, {bend_from_source(source_outer),
                                           bend_from_source(next_point)});
      }
    } else if (segment_index == edited.size() - 2U) {
      const GraphPosition destination_outer = edited[edited.size() - 2U];
      const GraphPosition previous_point    = edited[edited.size() - 3U];
      if ((horizontal && previous_point.x == destination_outer.x) ||
          (vertical && previous_point.y == destination_outer.y)) {
        const double detour =
            horizontal
                ? destination_outer.x +
                      std::copysign(CanvasConnectorGeometry::kEndpointClearance,
                                    destination_outer.x - edited.back().x)
                : destination_outer.y +
                      std::copysign(CanvasConnectorGeometry::kEndpointClearance,
                                    destination_outer.y - edited.back().y);
        if (!std::isfinite(detour)) {
          return std::nullopt;
        }
        edited.insert(
            edited.end() - 2,
            {bend_to_destination(previous_point),
             horizontal ? GraphPosition{detour, target}
                        : GraphPosition{target, detour},
             horizontal ? GraphPosition{detour, destination_outer.y}
                        : GraphPosition{destination_outer.x, detour}});
      } else {
        edited.insert(edited.end() - 2,
                      {bend_to_destination(previous_point),
                       bend_to_destination(destination_outer)});
      }
    } else if (segment_index == 1U && segment_index == last_editable_segment) {
      // The only editable segment is between both fixed outer points. Keep
      // both endpoint clearances and insert the parallel detour between them.
      const GraphPosition source_outer      = edited[1U];
      const GraphPosition destination_outer = edited[edited.size() - 2U];
      edited.erase(edited.begin() + 2, edited.end() - 2);
      edited.insert(edited.begin() + 2,
                    {bend_from_source(source_outer),
                     bend_to_destination(destination_outer)});
    } else if (segment_index == 1U) {
      const GraphPosition source_outer = edited[1U];
      edited[segment_index + 1U]       = moved(edited[segment_index + 1U]);
      edited.insert(edited.begin() + 2, bend_from_source(source_outer));
    } else if (segment_index == last_editable_segment) {
      edited[segment_index] = moved(edited[segment_index]);
      edited.insert(
          edited.begin() + static_cast<std::ptrdiff_t>(segment_index + 1U),
          bend_to_destination(edited[edited.size() - 2U]));
    } else {
      edited[segment_index]      = moved(edited[segment_index]);
      edited[segment_index + 1U] = moved(edited[segment_index + 1U]);
    }

    // A drag that aligns adjacent runs removes the now-superfluous corner.
    // The two outer points are deliberately excluded: they are the minimum
    // clearance contract for the node attachments.
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t index = 2U; index + 1U < edited.size() - 1U; ++index) {
        const GraphPosition before  = edited[index - 1U];
        const GraphPosition current = edited[index];
        const GraphPosition after   = edited[index + 1U];
        if (current == before || current == after ||
            (before.x == current.x && current.x == after.x) ||
            (before.y == current.y && current.y == after.y)) {
          edited.erase(edited.begin() + static_cast<std::ptrdiff_t>(index));
          changed = true;
          break;
        }
      }
    }

    if (edited[1U] != route_points[1U] ||
        edited[edited.size() - 2U] != route_points[route_points.size() - 2U] ||
        edited.size() < 4U) {
      return std::nullopt;
    }
    for (std::size_t index = 1U; index < edited.size(); ++index) {
      const GraphPosition before  = edited[index - 1U];
      const GraphPosition current = edited[index];
      if (!is_finite(current) || before == current ||
          (before.x != current.x && before.y != current.y)) {
        return std::nullopt;
      }
    }
    return edited;
  } catch (...) {
    return std::nullopt;
  }
}

bool CanvasNotationScene::complete() const noexcept {
  return std::ranges::all_of(nodes, [](const CanvasNodeNotation& node) {
    return static_cast<bool>(node);
  });
}

std::optional<CanvasSingleClickSelection> canvas_single_click_selection(
    const Project& project, const CanvasNotationScene& scene,
    const NotePaletteState& palette, GraphPosition pointer,
    double connector_hit_tolerance) {
  if (!is_finite(pointer) || !std::isfinite(connector_hit_tolerance) ||
      connector_hit_tolerance < 0.0) {
    return std::nullopt;
  }

  for (auto node_iterator = scene.nodes.rbegin();
       node_iterator != scene.nodes.rend(); ++node_iterator) {
    const CanvasNodeNotation& node = *node_iterator;
    const GraphPosition       local{pointer.x - node.position.x,
                              pointer.y - node.position.y};
    if (!is_finite(local)) {
      continue;
    }

    for (const CanvasNodePort& port : node.ports) {
      if (contains(port.bounds, local)) {
        return CanvasPortSelection{node.node_id, port.connector_id,
                                   port.direction};
      }
    }

    constexpr std::size_t kHeaderButtonCount = 3U;
    const std::array<const CanvasNodeHeaderButton*, kHeaderButtonCount> buttons{
        &node.header.freeform_notes_button, &node.header.tempo_lane_button,
        &node.header.play_button};
    for (const CanvasNodeHeaderButton* button : buttons) {
      if (contains(button->bounds, local)) {
        return CanvasControlSelection{node.node_id, button->action};
      }
    }

    if (node.layout.has_value() &&
        contains(node.geometry.notation_bounds, local)) {
      const NotationPoint notation_point{
          local.x - node.geometry.notation_bounds.x,
          local.y - node.geometry.notation_bounds.y};
      if (std::isfinite(notation_point.x) && std::isfinite(notation_point.y)) {
        auto selection = resolve_selection_at(project, *node.layout, palette,
                                              notation_point);
        if (selection.has_value()) {
          return CanvasNotationSelection{node.node_id, std::move(*selection)};
        }
      }
    }

    if (contains(
            {0.0, 0.0, node.geometry.bounds.width, node.geometry.bounds.height},
            local)) {
      return CanvasNodeSelection{node.node_id};
    }
  }

  for (auto connector_iterator = scene.connectors.rbegin();
       connector_iterator != scene.connectors.rend(); ++connector_iterator) {
    const auto hit = canvas_connector_segment_hover(
        connector_iterator->route_points, pointer, connector_hit_tolerance);
    if (hit.has_value()) {
      return CanvasConnectorPathSelection{
          {connector_iterator->source_node,
           connector_iterator->source_connector},
          hit->segment_index};
    }
  }
  return std::nullopt;
}

std::optional<CanvasNodePlaybackActionRequest>
canvas_node_playback_action_request(
    const CanvasControlSelection& control) noexcept {
  if (control.action != CanvasNodeHeaderAction::kPlay) {
    return std::nullopt;
  }
  return CanvasNodePlaybackActionRequest{control.node_id};
}

std::optional<CanvasConnectorPlaybackActionRequest>
canvas_double_click_playback_action_request(const Project&             project,
                                            const CanvasNotationScene& scene,
                                            const NotePaletteState&    palette,
                                            GraphPosition              pointer,
                                            double connector_hit_tolerance) {
  auto selection = canvas_single_click_selection(
      project, scene, palette, pointer, connector_hit_tolerance);
  if (!selection.has_value()) {
    return std::nullopt;
  }
  const auto* const connector =
      std::get_if<CanvasConnectorPathSelection>(&*selection);
  if (connector == nullptr) {
    return std::nullopt;
  }
  return CanvasConnectorPlaybackActionRequest{connector->connector};
}

CanvasConnectorPlaybackActionResult canvas_connector_playback_action(
    const CanvasConnectorPlaybackActionRequest& request,
    std::optional<NodeId>                       active_node) {
  if (!active_node.has_value()) {
    return {std::nullopt, "playback is not active"};
  }
  if (*active_node != request.connector.source_node) {
    return {std::nullopt, "connection source is not the active node"};
  }
  return {request, {}};
}

CanvasConnectorPlaybackActionResult canvas_connector_playback_action(
    const Project& project, const CanvasConnectorPlaybackActionRequest& request,
    CanvasConnectorPlaybackController& controller) {
  auto result =
      canvas_connector_playback_action(request, controller.active_node());
  if (!result.available()) {
    return result;
  }

  const Node* const source = project.find_node(request.connector.source_node);
  const OutputConnector* const output =
      source == nullptr
          ? nullptr
          : source->find_output(request.connector.source_connector);
  if (output == nullptr || !output->destination().has_value()) {
    return {std::nullopt, "connection is no longer available"};
  }
  const ConnectorDestination destination = *output->destination();
  const Node* const destination_node     = project.find_node(destination.node);
  if (destination_node == nullptr ||
      destination_node->find_input(destination.connector) == nullptr) {
    return {std::nullopt, "connection is no longer available"};
  }

  if (output->type() == ConnectorType::kSequential) {
    controller.queue_sequential_connector(request.connector);
  } else {
    controller.take_vertical_connector(request.connector);
  }
  return result;
}

std::optional<CanvasConnectorPlaybackActionRequest>
canvas_action_circle_playback_action_request(const CanvasNotationScene& scene,
                                             GraphPosition pointer) noexcept {
  if (!is_finite(pointer)) {
    return std::nullopt;
  }
  constexpr double kInteractionRadius =
      CanvasConnectorActionCircle::kInteractionDiameter / 2.0;
  for (auto connector = scene.connectors.rbegin();
       connector != scene.connectors.rend(); ++connector) {
    const GraphPosition center = connector->action_circle.center;
    if (is_finite(center) &&
        std::hypot(pointer.x - center.x, pointer.y - center.y) <=
            kInteractionRadius) {
      return CanvasConnectorPlaybackActionRequest{
          {connector->source_node, connector->source_connector}};
    }
  }
  return std::nullopt;
}

CanvasNodeDragController::CanvasNodeDragController(
    Project& project, CommandHistory& history,
    CanvasNotationScene& scene) noexcept
    : project_(project), history_(history), scene_(scene) {}

CanvasNodeDragController::~CanvasNodeDragController() {
  cancel();
}

bool CanvasNodeDragController::begin(NodeId        node_id,
                                     GraphPosition pointer) noexcept {
  if (active_ || !is_finite(pointer)) {
    return false;
  }
  Node* const project_node = project_.find_node(node_id);
  const auto  scene_node =
      std::ranges::find(scene_.nodes, node_id, &CanvasNodeNotation::node_id);
  if (project_node == nullptr || scene_node == scene_.nodes.end() ||
      scene_node->position != project_node->position() ||
      !is_finite(scene_node->position)) {
    return false;
  }
  try {
    connectors_start_ = scene_.connectors;
  } catch (...) {
    connectors_start_.clear();
    return false;
  }
  node_id_        = node_id;
  pointer_start_  = pointer;
  position_start_ = scene_node->position;
  active_         = true;
  return true;
}

bool CanvasNodeDragController::update(GraphPosition pointer) noexcept {
  if (!active_ || !is_finite(pointer)) {
    return false;
  }
  const GraphPosition delta{pointer.x - pointer_start_.x,
                            pointer.y - pointer_start_.y};
  const GraphPosition position{position_start_.x + delta.x,
                               position_start_.y + delta.y};
  if (!is_finite(delta) || !is_finite(position) ||
      (delta.x != 0.0 && position.x == position_start_.x) ||
      (delta.y != 0.0 && position.y == position_start_.y)) {
    return false;
  }
  return set_preview_position(position);
}

Result CanvasNodeDragController::finish() noexcept {
  if (!active_) {
    return Result(ResultCode::kInvalidArgument);
  }
  CanvasNodeNotation* const node         = dragged_node();
  Node* const               project_node = project_.find_node(node_id_);
  if (node == nullptr || project_node == nullptr ||
      project_node->position() != position_start_) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }
  const GraphPosition final_position = node->position;
  if (final_position == position_start_) {
    connectors_start_.clear();
    active_ = false;
    return Result();
  }
  std::unique_ptr<SetNodePositionCommand> command;
  try {
    command =
        std::make_unique<SetNodePositionCommand>(node_id_, final_position);
  } catch (const std::bad_alloc&) {
    cancel();
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    cancel();
    return Result(ResultCode::kOutOfMemory);
  }
  const Result result = history_.execute_new(std::move(command), project_);
  if (!result.ok()) {
    cancel();
    return result;
  }
  connectors_start_.clear();
  active_ = false;
  return Result();
}

void CanvasNodeDragController::cancel() noexcept {
  if (!active_) {
    return;
  }
  CanvasNodeNotation* const node = dragged_node();
  if (node != nullptr) {
    node->position               = position_start_;
    node->geometry.bounds.origin = position_start_;
  }
  static_assert(
      std::is_nothrow_move_assignable_v<std::vector<CanvasConnectorGeometry>>);
  scene_.connectors = std::move(connectors_start_);
  active_           = false;
}

CanvasNodeNotation* CanvasNodeDragController::dragged_node() noexcept {
  const auto found =
      std::ranges::find(scene_.nodes, node_id_, &CanvasNodeNotation::node_id);
  return found == scene_.nodes.end() ? nullptr : &*found;
}

bool CanvasNodeDragController::set_preview_position(
    GraphPosition position) noexcept {
  CanvasNodeNotation* const node = dragged_node();
  if (node == nullptr || !is_finite(position)) {
    return false;
  }
  const GraphPosition previous = node->position;
  node->position               = position;
  node->geometry.bounds.origin = position;
  if (!refresh_connector_routes(scene_, project_)) {
    node->position               = previous;
    node->geometry.bounds.origin = previous;
    return false;
  }
  return true;
}

CanvasNodeOperationsController::CanvasNodeOperationsController(
    Project& project, CommandHistory& history, CanvasNotationScene& scene,
    const GlyphMetrics& metrics, NotationLayoutOptions options) noexcept
    : project_(project),
      history_(history),
      scene_(scene),
      metrics_(metrics),
      options_(options) {}

CanvasNodeOperationsController::~CanvasNodeOperationsController() {
  cancel_move();
}

bool CanvasNodeOperationsController::select(NodeId node_id, bool additive) {
  if (move_active_ || project_.find_node(node_id) == nullptr ||
      find_scene_node(scene_, node_id) == nullptr) {
    return false;
  }
  try {
    std::vector<NodeId> next = additive ? selection_ : std::vector<NodeId>{};
    const auto          existing = std::ranges::find(next, node_id);
    if (additive && existing != next.end()) {
      next.erase(existing);
    } else if (existing == next.end()) {
      next.push_back(node_id);
    }
    std::vector<NodeId> ordered;
    ordered.reserve(next.size());
    for (const Node& node : project_.nodes()) {
      if (std::ranges::find(next, node.id()) != next.end())
        ordered.push_back(node.id());
    }
    selection_ = std::move(ordered);
  } catch (...) {
    return false;
  }
  return true;
}

void CanvasNodeOperationsController::clear_selection() noexcept {
  if (!move_active_)
    selection_.clear();
}

const std::vector<NodeId>& CanvasNodeOperationsController::selection()
    const noexcept {
  return selection_;
}

bool CanvasNodeOperationsController::selection_is_current() const noexcept {
  if (selection_.empty() || scene_.nodes.size() != project_.nodes().size())
    return false;
  if (!scene_nodes_match_project(scene_, project_) ||
      !scene_connectors_match_project(scene_, project_)) {
    return false;
  }
  for (const NodeId id : selection_) {
    const Node* const               node     = project_.find_node(id);
    const CanvasNodeNotation* const retained = find_scene_node(scene_, id);
    if (node == nullptr || retained == nullptr ||
        node->position() != retained->position) {
      return false;
    }
  }
  return true;
}

bool CanvasNodeOperationsController::begin_move(
    NodeId anchor, GraphPosition pointer) noexcept {
  if (move_active_ || !is_finite(pointer) ||
      std::ranges::find(selection_, anchor) == selection_.end() ||
      !selection_is_current()) {
    return false;
  }
  try {
    move_starts_.clear();
    move_starts_.reserve(selection_.size());
    for (const NodeId id : selection_)
      move_starts_.push_back(find_scene_node(scene_, id)->position);
    connectors_start_ = scene_.connectors;
  } catch (...) {
    move_starts_.clear();
    connectors_start_.clear();
    return false;
  }
  pointer_start_ = pointer;
  move_active_   = true;
  return true;
}

bool CanvasNodeOperationsController::update_move(
    GraphPosition pointer) noexcept {
  if (!move_active_ || !is_finite(pointer))
    return false;
  const GraphPosition delta{pointer.x - pointer_start_.x,
                            pointer.y - pointer_start_.y};
  if (!is_finite(delta))
    return false;

  std::vector<GraphPosition> positions;
  try {
    positions.reserve(selection_.size());
    for (const GraphPosition start : move_starts_) {
      const GraphPosition position{start.x + delta.x, start.y + delta.y};
      if (!is_finite(position) || (delta.x != 0.0 && position.x == start.x) ||
          (delta.y != 0.0 && position.y == start.y)) {
        return false;
      }
      positions.push_back(position);
    }
  } catch (...) {
    return false;
  }
  for (std::size_t index = 0; index < selection_.size(); ++index) {
    CanvasNodeNotation* const node = find_scene_node(scene_, selection_[index]);
    if (node == nullptr) {
      cancel_move();
      return false;
    }
    node->position               = positions[index];
    node->geometry.bounds.origin = positions[index];
  }
  if (!refresh_connector_routes(scene_, project_)) {
    cancel_move();
    return false;
  }
  return true;
}

Result CanvasNodeOperationsController::finish_move() noexcept {
  if (!move_active_)
    return Result(ResultCode::kInvalidArgument);
  std::unique_ptr<CommandTransaction> transaction;
  try {
    transaction = std::make_unique<CommandTransaction>();
    for (std::size_t index = 0; index < selection_.size(); ++index) {
      const CanvasNodeNotation* const node =
          find_scene_node(scene_, selection_[index]);
      const Node* const project_node = project_.find_node(selection_[index]);
      if (node == nullptr || project_node == nullptr ||
          project_node->position() != move_starts_[index]) {
        cancel_move();
        return Result(ResultCode::kInvalidArgument);
      }
      if (node->position != move_starts_[index]) {
        const Result add =
            transaction->add_command(std::make_unique<SetNodePositionCommand>(
                selection_[index], node->position));
        if (!add.ok()) {
          cancel_move();
          return add;
        }
      }
    }
  } catch (...) {
    cancel_move();
    return Result(ResultCode::kOutOfMemory);
  }
  if (transaction->child_count() == 0U) {
    connectors_start_.clear();
    move_starts_.clear();
    move_active_ = false;
    return Result();
  }
  const Result result = history_.execute_new(std::move(transaction), project_);
  if (!result.ok()) {
    cancel_move();
    return result;
  }
  connectors_start_.clear();
  move_starts_.clear();
  move_active_ = false;
  return Result();
}

void CanvasNodeOperationsController::cancel_move() noexcept {
  if (!move_active_)
    return;
  for (std::size_t index = 0; index < selection_.size(); ++index) {
    CanvasNodeNotation* const node = find_scene_node(scene_, selection_[index]);
    if (node != nullptr) {
      node->position               = move_starts_[index];
      node->geometry.bounds.origin = move_starts_[index];
    }
  }
  scene_.connectors = std::move(connectors_start_);
  move_starts_.clear();
  move_active_ = false;
}

Result CanvasNodeOperationsController::copy_selected() noexcept {
  if (move_active_ || !selection_is_current())
    return Result(ResultCode::kInvalidArgument);
  try {
    std::vector<Node> copied;
    copied.reserve(selection_.size());
    for (const NodeId id : selection_)
      copied.push_back(*project_.find_node(id));
    clipboard_ = std::move(copied);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result CanvasNodeOperationsController::duplicate_nodes(
    std::vector<Node> snapshots, GraphPosition offset) noexcept {
  if (move_active_ || snapshots.empty() || !is_finite(offset))
    return Result(ResultCode::kInvalidArgument);
  std::unique_ptr<DuplicateNodesCommand> command;
  try {
    command =
        std::make_unique<DuplicateNodesCommand>(std::move(snapshots), offset);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
  DuplicateNodesCommand* const command_ptr = command.get();
  CommandHistory::Transaction  transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active())
    return Result(history_.poisoned() ? ResultCode::kCommandFaulted
                                      : ResultCode::kInvalidArgument);

  CanvasNotationScene next;
  std::vector<NodeId> created;
  try {
    next    = Canvas{}.layout_nodes(project_, metrics_, options_);
    created = command_ptr->created_node_ids();
    if (!scene_nodes_match_project(next, project_) ||
        !scene_connectors_match_project(next, project_)) {
      const Result abort = transaction.abort();
      return abort.ok() ? Result(ResultCode::kInvalidArgument) : abort;
    }
  } catch (...) {
    const Result abort = transaction.abort();
    return abort.ok() ? Result(ResultCode::kOutOfMemory) : abort;
  }
  const Result commit = transaction.commit();
  if (!commit.ok())
    return commit;
  scene_     = std::move(next);
  selection_ = std::move(created);
  return Result();
}

Result CanvasNodeOperationsController::duplicate_selected(
    GraphPosition offset) noexcept {
  if (!selection_is_current())
    return Result(ResultCode::kInvalidArgument);
  std::vector<Node> snapshots;
  try {
    snapshots.reserve(selection_.size());
    for (const NodeId id : selection_)
      snapshots.push_back(*project_.find_node(id));
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
  return duplicate_nodes(std::move(snapshots), offset);
}

Result CanvasNodeOperationsController::paste(GraphPosition offset) noexcept {
  std::vector<Node> snapshots;
  try {
    snapshots = clipboard_;
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
  return duplicate_nodes(std::move(snapshots), offset);
}

Result CanvasNodeOperationsController::delete_selected() noexcept {
  if (move_active_ || !selection_is_current())
    return Result(ResultCode::kInvalidArgument);
  std::unique_ptr<CommandTransaction> command;
  try {
    command = std::make_unique<CommandTransaction>();
    for (const NodeId id : selection_) {
      const Result add =
          command->add_command(std::make_unique<RemoveNodeCommand>(id));
      if (!add.ok())
        return add;
    }
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
  CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active())
    return Result(history_.poisoned() ? ResultCode::kCommandFaulted
                                      : ResultCode::kInvalidArgument);
  CanvasNotationScene next;
  try {
    next = Canvas{}.layout_nodes(project_, metrics_, options_);
    if (!scene_nodes_match_project(next, project_) ||
        !scene_connectors_match_project(next, project_)) {
      const Result abort = transaction.abort();
      return abort.ok() ? Result(ResultCode::kInvalidArgument) : abort;
    }
  } catch (...) {
    const Result abort = transaction.abort();
    return abort.ok() ? Result(ResultCode::kOutOfMemory) : abort;
  }
  const Result commit = transaction.commit();
  if (!commit.ok())
    return commit;
  scene_ = std::move(next);
  selection_.clear();
  return Result();
}

std::vector<CanvasNodeSearchResult> canvas_search_nodes(
    const Project& project, std::string_view query) {
  std::vector<CanvasNodeSearchResult> results;
  results.reserve(project.nodes().size());
  for (const Node& node : project.nodes()) {
    std::string uuid = node.id().to_string();
    if (contains_ascii_case_insensitive(node.name(), query) ||
        contains_ascii_case_insensitive(uuid, query)) {
      results.push_back({node.id(), node.name(), std::move(uuid)});
    }
  }
  return results;
}

bool canvas_focus_node(const CanvasNotationScene& scene, NodeId node_id,
                       ViewportPosition   viewport_focus,
                       ViewportTransform& transform) noexcept {
  if (!is_finite(viewport_focus)) {
    return false;
  }
  const CanvasNodeNotation* const node = find_scene_node(scene, node_id);
  if (node == nullptr || !is_valid_world_bounds(node->geometry.bounds)) {
    return false;
  }
  const WorldBounds&  bounds = node->geometry.bounds;
  const GraphPosition center{bounds.origin.x + bounds.width / 2.0,
                             bounds.origin.y + bounds.height / 2.0};
  return is_finite(center) && transform.set_anchor(center, viewport_focus);
}

CanvasConnectorSegmentDragController::CanvasConnectorSegmentDragController(
    Project& project, CommandHistory& history,
    CanvasNotationScene& scene) noexcept
    : project_(project), history_(history), scene_(scene) {}

CanvasConnectorSegmentDragController::~CanvasConnectorSegmentDragController() {
  cancel();
}

bool CanvasConnectorSegmentDragController::begin(
    NodeId source_node, ConnectorId source_output, std::size_t segment_index,
    GraphPosition pointer) noexcept {
  if (active_ || !is_finite(pointer) ||
      !scene_connectors_match_project(scene_, project_) ||
      !scene_nodes_match_project(scene_, project_)) {
    return false;
  }
  const Node* const            node = project_.find_node(source_node);
  const OutputConnector* const output =
      node == nullptr ? nullptr : node->find_output(source_output);
  if (output == nullptr || !output->destination().has_value()) {
    return false;
  }
  const Node* const destination_node =
      project_.find_node(output->destination()->node);
  const CanvasNodeNotation* const source_scene =
      find_scene_node(scene_, source_node);
  const CanvasNodeNotation* const destination_scene =
      find_scene_node(scene_, output->destination()->node);
  if (destination_node == nullptr || source_scene == nullptr ||
      destination_scene == nullptr ||
      source_scene->position != node->position() ||
      destination_scene->position != destination_node->position()) {
    return false;
  }
  const auto found = std::ranges::find_if(
      scene_.connectors,
      [source_node, source_output](const CanvasConnectorGeometry& connector) {
        return connector.source_node == source_node &&
               connector.source_connector == source_output;
      });
  if (found == scene_.connectors.end() ||
      !canvas_connector_drag_segment(found->route_points, segment_index,
                                     pointer)
           .has_value()) {
    return false;
  }
  try {
    const auto current = connector_geometry(scene_, source_node, source_output,
                                            *output->destination(),
                                            output->type(), output->route());
    if (!current.has_value() || current->route_points != found->route_points ||
        current->source_leg != found->source_leg ||
        current->destination_leg != found->destination_leg ||
        current->render_path != found->render_path) {
      return false;
    }
  } catch (...) {
    return false;
  }

  try {
    connectors_start_       = scene_.connectors;
    route_start_            = found->route_points;
    render_start_           = found->render_path;
    domain_waypoints_start_ = output->route().waypoints();
    destination_start_      = output->destination();
    type_start_             = output->type();
    node_ids_start_.reserve(scene_.nodes.size());
    node_positions_start_.reserve(scene_.nodes.size());
    node_bounds_start_.reserve(scene_.nodes.size());
    for (const CanvasNodeNotation& scene_node : scene_.nodes) {
      node_ids_start_.push_back(scene_node.node_id);
      node_positions_start_.push_back(scene_node.position);
      node_bounds_start_.push_back(scene_node.geometry.bounds);
    }
  } catch (...) {
    connectors_start_.clear();
    route_start_.clear();
    render_start_.clear();
    domain_waypoints_start_.clear();
    node_ids_start_.clear();
    node_positions_start_.clear();
    node_bounds_start_.clear();
    destination_start_.reset();
    return false;
  }
  connector_index_ =
      static_cast<std::size_t>(found - scene_.connectors.begin());
  segment_index_              = segment_index;
  source_node_                = source_node;
  source_output_              = source_output;
  source_position_start_      = node->position();
  destination_position_start_ = destination_node->position();
  route_was_automatic_        = output->route().is_automatic();
  preview_changed_            = false;
  active_                     = true;
  return true;
}

bool CanvasConnectorSegmentDragController::update(
    GraphPosition pointer) noexcept {
  if (!active_ || !is_finite(pointer)) {
    return false;
  }
  const auto route =
      canvas_connector_drag_segment(route_start_, segment_index_, pointer);
  if (!route.has_value()) {
    return false;
  }
  return set_preview_route(std::move(*route));
}

Result CanvasConnectorSegmentDragController::finish() noexcept {
  if (!active_) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (connector_index_ >= scene_.connectors.size()) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }
  const CanvasConnectorGeometry& geometry = scene_.connectors[connector_index_];
  if (geometry.route_points.size() < 3U) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }
  if (!scene_connectors_match_project(scene_, project_) ||
      !scene_nodes_match_project(scene_, project_) ||
      scene_.nodes.size() != node_ids_start_.size() ||
      scene_.connectors.size() != connectors_start_.size()) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }
  for (std::size_t index = 0U; index < scene_.nodes.size(); ++index) {
    if (scene_.nodes[index].node_id != node_ids_start_[index] ||
        scene_.nodes[index].position != node_positions_start_[index] ||
        scene_.nodes[index].geometry.bounds != node_bounds_start_[index]) {
      cancel();
      return Result(ResultCode::kInvalidArgument);
    }
  }
  for (std::size_t index = 0U; index < scene_.connectors.size(); ++index) {
    const CanvasConnectorGeometry& current = scene_.connectors[index];
    const CanvasConnectorGeometry& start   = connectors_start_[index];
    const bool                     metadata_matches =
        current.source_node == start.source_node &&
        current.source_connector == start.source_connector &&
        current.destination_node == start.destination_node &&
        current.destination_connector == start.destination_connector &&
        current.source_leg == start.source_leg &&
        current.destination_leg == start.destination_leg &&
        current.type == start.type && current.style == start.style;
    if (!metadata_matches || (index != connector_index_ && current != start)) {
      cancel();
      return Result(ResultCode::kInvalidArgument);
    }
  }
  Node* const            node = project_.find_node(source_node_);
  OutputConnector* const output =
      node == nullptr ? nullptr : node->find_output(source_output_);
  const Node* const destination_node =
      output == nullptr || !output->destination().has_value()
          ? nullptr
          : project_.find_node(output->destination()->node);
  const CanvasNodeNotation* const source_scene =
      find_scene_node(scene_, source_node_);
  const CanvasNodeNotation* const destination_scene =
      destination_start_.has_value()
          ? find_scene_node(scene_, destination_start_->node)
          : nullptr;
  if (output == nullptr || !output->destination().has_value() ||
      destination_node == nullptr || source_scene == nullptr ||
      destination_scene == nullptr ||
      node->position() != source_position_start_ ||
      destination_node->position() != destination_position_start_ ||
      source_scene->position != source_position_start_ ||
      destination_scene->position != destination_position_start_ ||
      output->destination() != destination_start_ ||
      geometry.source_node != source_node_ ||
      geometry.source_connector != source_output_ ||
      geometry.destination_node != destination_start_->node ||
      geometry.destination_connector != destination_start_->connector ||
      output->type() != type_start_ || geometry.type != type_start_ ||
      output->route().is_automatic() != route_was_automatic_ ||
      output->route().waypoints() != domain_waypoints_start_) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }

  if (!preview_changed_ && geometry.route_points != route_start_) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }
  if (!preview_changed_ && geometry.render_path != render_start_) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }
  if (preview_changed_ && (geometry.route_points != preview_route_ ||
                           geometry.render_path != preview_render_path_)) {
    cancel();
    return Result(ResultCode::kInvalidArgument);
  }
  if (!preview_changed_) {
    connectors_start_.clear();
    route_start_.clear();
    render_start_.clear();
    domain_waypoints_start_.clear();
    node_ids_start_.clear();
    node_positions_start_.clear();
    node_bounds_start_.clear();
    preview_route_.clear();
    preview_render_path_.clear();
    destination_start_.reset();
    active_ = false;
    return Result();
  }

  std::vector<RoutePoint> waypoints;
  try {
    if (geometry.route_points.size() > 4U) {
      waypoints.reserve(geometry.route_points.size() - 4U);
    }
    for (std::size_t index = 2U; index + 2U < geometry.route_points.size();
         ++index) {
      waypoints.push_back(
          {geometry.route_points[index].x, geometry.route_points[index].y});
    }
    auto command = std::make_unique<SetCustomRouteCommand>(
        source_node_, source_output_, std::move(waypoints));
    const Result result = history_.execute_new(std::move(command), project_);
    if (!result.ok()) {
      cancel();
      return result;
    }
  } catch (...) {
    cancel();
    return Result(ResultCode::kOutOfMemory);
  }

  connectors_start_.clear();
  route_start_.clear();
  render_start_.clear();
  domain_waypoints_start_.clear();
  node_ids_start_.clear();
  node_positions_start_.clear();
  node_bounds_start_.clear();
  preview_route_.clear();
  preview_render_path_.clear();
  destination_start_.reset();
  active_ = false;
  return Result();
}

void CanvasConnectorSegmentDragController::cancel() noexcept {
  if (!active_) {
    return;
  }
  static_assert(
      std::is_nothrow_move_assignable_v<std::vector<CanvasConnectorGeometry>>);
  scene_.connectors = std::move(connectors_start_);
  route_start_.clear();
  render_start_.clear();
  domain_waypoints_start_.clear();
  node_ids_start_.clear();
  node_positions_start_.clear();
  node_bounds_start_.clear();
  preview_route_.clear();
  preview_render_path_.clear();
  destination_start_.reset();
  preview_changed_ = false;
  active_          = false;
}

CanvasConnectorSelectionController::CanvasConnectorSelectionController(
    Project& project, CommandHistory& history,
    CanvasNotationScene& scene) noexcept
    : project_(project), history_(history), scene_(scene) {}

bool CanvasConnectorSelectionController::select(
    NodeId source_node, ConnectorId source_connector) noexcept {
  if (!scene_connectors_match_project(scene_, project_) ||
      !scene_nodes_match_project(scene_, project_)) {
    return false;
  }
  const Node* const            node = project_.find_node(source_node);
  const OutputConnector* const output =
      node == nullptr ? nullptr : node->find_output(source_connector);
  if (output == nullptr || !output->destination().has_value()) {
    return false;
  }
  const auto found = std::ranges::find_if(
      scene_.connectors,
      [source_node, source_connector](const CanvasConnectorGeometry& geometry) {
        return geometry.source_node == source_node &&
               geometry.source_connector == source_connector;
      });
  if (found == scene_.connectors.end()) {
    return false;
  }
  selection_ = CanvasConnectorSelection{source_node, source_connector};
  return true;
}

void CanvasConnectorSelectionController::clear_selection() noexcept {
  selection_.reset();
}

Result CanvasConnectorSelectionController::reset_selected_route() noexcept {
  if (!selection_.has_value() ||
      !scene_connectors_match_project(scene_, project_) ||
      !scene_nodes_match_project(scene_, project_)) {
    return Result(ResultCode::kInvalidArgument);
  }

  const CanvasConnectorSelection selected = *selection_;
  Node* const            node = project_.find_node(selected.source_node);
  OutputConnector* const output =
      node == nullptr ? nullptr : node->find_output(selected.source_connector);
  if (output == nullptr || !output->destination().has_value()) {
    return Result(ResultCode::kInvalidArgument);
  }

  try {
    const auto found = std::ranges::find_if(
        scene_.connectors,
        [&selected](const CanvasConnectorGeometry& geometry) {
          return geometry.source_node == selected.source_node &&
                 geometry.source_connector == selected.source_connector;
        });
    if (found == scene_.connectors.end()) {
      return Result(ResultCode::kInvalidArgument);
    }

    const auto current = connector_geometry(
        scene_, selected.source_node, selected.source_connector,
        *output->destination(), output->type(), output->route());
    if (!current.has_value() || *current != *found) {
      return Result(ResultCode::kInvalidArgument);
    }
    if (output->route().is_automatic()) {
      return Result();
    }

    const RouteGeometry automatic_route_geometry;
    auto                automatic = connector_geometry(
        scene_, selected.source_node, selected.source_connector,
        *output->destination(), output->type(), automatic_route_geometry);
    if (!automatic.has_value()) {
      return Result(ResultCode::kInvalidArgument);
    }

    const Result result = history_.execute_new(
        std::make_unique<ResetRouteCommand>(selected.source_node,
                                            selected.source_connector),
        project_);
    if (!result.ok()) {
      return result;
    }
    static_assert(std::is_nothrow_move_assignable_v<CanvasConnectorGeometry>);
    *found = std::move(*automatic);
    return Result();
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result
CanvasConnectorSelectionController::delete_selected_connector() noexcept {
  if (!selection_.has_value() ||
      !scene_connectors_match_project(scene_, project_) ||
      !scene_nodes_match_project(scene_, project_)) {
    return Result(ResultCode::kInvalidArgument);
  }

  const CanvasConnectorSelection selected = *selection_;
  Node* const            node = project_.find_node(selected.source_node);
  OutputConnector* const output =
      node == nullptr ? nullptr : node->find_output(selected.source_connector);
  if (output == nullptr || !output->destination().has_value()) {
    return Result(ResultCode::kInvalidArgument);
  }

  try {
    const auto found = std::ranges::find_if(
        scene_.connectors,
        [&selected](const CanvasConnectorGeometry& geometry) {
          return geometry.source_node == selected.source_node &&
                 geometry.source_connector == selected.source_connector;
        });
    if (found == scene_.connectors.end()) {
      return Result(ResultCode::kInvalidArgument);
    }

    const auto current = connector_geometry(
        scene_, selected.source_node, selected.source_connector,
        *output->destination(), output->type(), output->route());
    if (!current.has_value() || *current != *found) {
      return Result(ResultCode::kInvalidArgument);
    }

    const Result result = history_.execute_new(
        std::make_unique<DisconnectCommand>(selected.source_node,
                                            selected.source_connector),
        project_);
    if (!result.ok()) {
      return result;
    }
    static_assert(std::is_nothrow_move_assignable_v<CanvasConnectorGeometry>);
    scene_.connectors.erase(found);
    selection_.reset();
    return Result();
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

bool CanvasConnectorSegmentDragController::set_preview_route(
    std::vector<GraphPosition> route_points) noexcept {
  if (connector_index_ >= scene_.connectors.size()) {
    return false;
  }
  try {
    preview_changed_ = route_points != route_start_;
    std::vector<CanvasConnectorPathElement> render_path =
        canvas_connector_render_path(route_points);
    preview_route_                    = route_points;
    preview_render_path_              = render_path;
    CanvasConnectorGeometry& geometry = scene_.connectors[connector_index_];
    geometry.route_points             = std::move(route_points);
    geometry.render_path              = std::move(render_path);
    return true;
  } catch (...) {
    return false;
  }
}

CanvasConnectorAttachmentController::CanvasConnectorAttachmentController(
    Project& project, CommandHistory& history,
    CanvasNotationScene& scene) noexcept
    : project_(project), history_(history), scene_(scene) {}

bool CanvasConnectorAttachmentController::begin(
    NodeId source_node, ConnectorId source_output) noexcept {
  if (active_ || !scene_connectors_match_project(scene_, project_)) {
    return false;
  }
  const Node* const node = project_.find_node(source_node);
  if (node == nullptr) {
    return false;
  }
  const OutputConnector* const    output = node->find_output(source_output);
  const CanvasNodeNotation* const scene_node =
      find_scene_node(scene_, source_node);
  if (output == nullptr || output->destination().has_value() ||
      scene_node == nullptr ||
      find_port(*scene_node, source_output, CanvasPortDirection::kOutput) ==
          nullptr) {
    return false;
  }
  source_node_   = source_node;
  source_output_ = source_output;
  active_        = true;
  return true;
}

Result CanvasConnectorAttachmentController::finish(
    NodeId destination_node, ConnectorId destination_input) noexcept {
  if (!active_) {
    return Result(ResultCode::kInvalidArgument);
  }
  active_ = false;

  const Node* const            source = project_.find_node(source_node_);
  const OutputConnector* const output =
      source == nullptr ? nullptr : source->find_output(source_output_);
  const auto insertion_index =
      connector_insertion_index(project_, source_node_, source_output_);
  if (output == nullptr || output->destination().has_value() ||
      !insertion_index.has_value() ||
      !scene_connectors_match_project(scene_, project_)) {
    return Result(ResultCode::kInvalidArgument);
  }

  std::optional<CanvasConnectorGeometry> geometry;
  std::unique_ptr<ConnectCommand>        command;
  try {
    const ConnectorDestination destination{destination_node, destination_input};
    geometry = connector_geometry(scene_, source_node_, source_output_,
                                  destination, output->type(), output->route());
    if (!geometry.has_value()) {
      return Result(ResultCode::kInvalidArgument);
    }
    scene_.connectors.reserve(scene_.connectors.size() + 1U);
    command = std::make_unique<ConnectCommand>(
        source_node_, source_output_, destination_node, destination_input);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }

  const Result result = history_.execute_new(std::move(command), project_);
  if (!result.ok()) {
    return result;
  }
  static_assert(std::is_nothrow_move_constructible_v<CanvasConnectorGeometry>);
  static_assert(std::is_nothrow_move_assignable_v<CanvasConnectorGeometry>);
  scene_.connectors.insert(
      scene_.connectors.begin() + static_cast<std::ptrdiff_t>(*insertion_index),
      std::move(*geometry));
  return Result();
}

void CanvasConnectorAttachmentController::cancel() noexcept {
  active_ = false;
}

CanvasConnectorTypeController::CanvasConnectorTypeController(
    Project& project, CommandHistory& history,
    CanvasNotationScene& scene) noexcept
    : project_(project), history_(history), scene_(scene) {}

Result CanvasConnectorTypeController::set_type(NodeId        node_id,
                                               ConnectorId   output_id,
                                               ConnectorType type) noexcept {
  if (type != ConnectorType::kSequential && type != ConnectorType::kVertical) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (!scene_connectors_match_project(scene_, project_)) {
    return Result(ResultCode::kInvalidArgument);
  }
  Node* const node = project_.find_node(node_id);
  if (node == nullptr) {
    return Result(ResultCode::kInvalidArgument);
  }
  const OutputConnector* const    output     = node->find_output(output_id);
  const CanvasNodeNotation* const scene_node = find_scene_node(scene_, node_id);
  if (output == nullptr || scene_node == nullptr ||
      find_port(*scene_node, output_id, CanvasPortDirection::kOutput) ==
          nullptr) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (output->type() == type) {
    return Result();
  }

  std::unique_ptr<SetOutputTypeCommand> command;
  try {
    command = std::make_unique<SetOutputTypeCommand>(node_id, output_id, type);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
  const Result result = history_.execute_new(std::move(command), project_);
  if (!result.ok()) {
    return result;
  }

  const auto geometry = std::ranges::find_if(
      scene_.connectors, [node_id, output_id](const auto& connector) {
        return connector.source_node == node_id &&
               connector.source_connector == output_id;
      });
  if (geometry != scene_.connectors.end()) {
    geometry->type  = type;
    geometry->style = canvas_connector_style(type);
  }
  return Result();
}

CanvasConnectorInspectorController::CanvasConnectorInspectorController(
    Project& project, CommandHistory& history,
    CanvasNotationScene& scene) noexcept
    : project_(project), history_(history), scene_(scene) {}

OutputConnector* CanvasConnectorInspectorController::find_output(
    NodeId node_id, ConnectorId output_id) noexcept {
  Node* const               node       = project_.find_node(node_id);
  CanvasNodeNotation* const scene_node = find_scene_node(scene_, node_id);
  if (node == nullptr || scene_node == nullptr) {
    return nullptr;
  }
  OutputConnector* const      output = node->find_output(output_id);
  const CanvasNodePort* const port =
      find_port(*scene_node, output_id, CanvasPortDirection::kOutput);
  if (output == nullptr || port == nullptr) {
    return nullptr;
  }
  try {
    if (port->name != output->name() ||
        port->accessibility_id !=
            connector_accessibility_id(
                node_id, output_id, AccessibilityConnectorDirection::kOutput) ||
        port->accessibility_label !=
            connector_accessibility_label(
                output->name(), AccessibilityConnectorDirection::kOutput)) {
      return nullptr;
    }
  } catch (...) {
    return nullptr;
  }
  return output;
}

Result CanvasConnectorInspectorController::set_event_binding(
    NodeId node_id, ConnectorId output_id,
    std::optional<EventId> event) noexcept {
  OutputConnector* const output = find_output(node_id, output_id);
  if (output == nullptr) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (output->event_binding() == event) {
    return Result();
  }
  try {
    return history_.execute_new(
        std::make_unique<BindOutputEventCommand>(node_id, output_id, event),
        project_);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result CanvasConnectorInspectorController::set_priority(NodeId      node_id,
                                                        ConnectorId output_id,
                                                        int priority) noexcept {
  OutputConnector* const output = find_output(node_id, output_id);
  if (output == nullptr) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (output->priority() == priority) {
    return Result();
  }
  try {
    return history_.execute_new(std::make_unique<SetOutputPriorityCommand>(
                                    node_id, output_id, priority),
                                project_);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result CanvasConnectorInspectorController::set_random_weight(
    NodeId node_id, ConnectorId output_id, Rational weight) noexcept {
  OutputConnector* const output = find_output(node_id, output_id);
  if (output == nullptr) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (output->weight() == weight) {
    return Result();
  }
  try {
    return history_.execute_new(
        std::make_unique<SetOutputWeightCommand>(node_id, output_id, weight),
        project_);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result CanvasConnectorInspectorController::set_name(NodeId      node_id,
                                                    ConnectorId output_id,
                                                    std::string name) noexcept {
  OutputConnector* const    output     = find_output(node_id, output_id);
  CanvasNodeNotation* const scene_node = find_scene_node(scene_, node_id);
  CanvasNodePort* const     port =
      scene_node == nullptr
              ? nullptr
              : find_port(*scene_node, output_id, CanvasPortDirection::kOutput);
  if (output == nullptr || port == nullptr) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (output->name() == name) {
    return Result();
  }

  std::string label;
  try {
    label = connector_accessibility_label(
        name, AccessibilityConnectorDirection::kOutput);
    auto command = std::make_unique<SetOutputConnectorNameCommand>(
        node_id, output_id, name);
    const Result result = history_.execute_new(std::move(command), project_);
    if (!result.ok()) {
      return result;
    }
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
  port->name                = std::move(name);
  port->accessibility_label = std::move(label);
  return Result();
}

Result CanvasConnectorInspectorController::set_listener(
    NodeId node_id, ConnectorId output_id, QueuePolicy policy,
    std::size_t capacity) noexcept {
  if (!valid_queue_policy(policy)) {
    return Result(ResultCode::kInvalidArgument);
  }
  OutputConnector* const output = find_output(node_id, output_id);
  if (output == nullptr || !output->event_binding().has_value()) {
    return Result(ResultCode::kInvalidArgument);
  }
  Node* const                node     = project_.find_node(node_id);
  const EventId              event    = *output->event_binding();
  const EventListener* const listener = node->find_listener(event);
  if (listener == nullptr) {
    return Result(ResultCode::kInvalidArgument);
  }
  if (listener->policy() == policy && listener->capacity() == capacity) {
    return Result();
  }
  try {
    return history_.execute_new(std::make_unique<SetListenerPolicyCommand>(
                                    node_id, event, policy, capacity),
                                project_);
  } catch (...) {
    return Result(ResultCode::kOutOfMemory);
  }
}

CanvasNotationScene Canvas::layout_nodes(
    const Project& project, const GlyphMetrics& metrics,
    const NotationLayoutOptions& options) const {
  CanvasNotationScene scene;
  scene.nodes.reserve(project.nodes().size());
  const ValidationReport validation =
      ValidationService{}.validate_complete(project);
  for (const Node& node : project.nodes()) {
    NotationLayoutResult result =
        layout_notation(project, node.id(), metrics, options);
    const CanvasNodeGeometry geometry =
        node_geometry(node.position(), result.layout);
    scene.nodes.push_back(CanvasNodeNotation{
        node.id(), node.position(),
        node_header(node, validation_state_for_node(validation, node.id()),
                    geometry.bounds.width),
        geometry, node_ports(node, geometry), result.error,
        std::move(result.layout)});
  }
  for (const Node& node : project.nodes()) {
    for (const OutputConnector& output : node.outputs()) {
      if (!output.destination().has_value()) {
        continue;
      }
      const Node* const destination_node =
          project.find_node(output.destination()->node);
      if (destination_node == nullptr ||
          destination_node->find_input(output.destination()->connector) ==
              nullptr) {
        continue;
      }
      auto geometry = connector_geometry(scene, node.id(), output.id(),
                                         *output.destination(), output.type(),
                                         output.route());
      if (geometry.has_value()) {
        scene.connectors.push_back(std::move(*geometry));
      }
    }
  }
  return scene;
}

std::optional<CanvasConnectorInspector> Canvas::inspect_connector(
    const Project& project, NodeId source_node, ConnectorId output_id) const {
  const Node* const node = project.find_node(source_node);
  if (node == nullptr) {
    return std::nullopt;
  }
  const OutputConnector* const output = node->find_output(output_id);
  if (output == nullptr) {
    return std::nullopt;
  }

  CanvasConnectorInspector inspector{source_node,
                                     node->name(),
                                     output_id,
                                     output->name(),
                                     output->type(),
                                     std::nullopt,
                                     output->priority(),
                                     output->weight(),
                                     std::nullopt,
                                     std::nullopt,
                                     {}};
  if (output->event_binding().has_value()) {
    const EventId              event_id = *output->event_binding();
    std::optional<std::string> event_name;
    if (const EventDefinition* const event =
            project.events().find_by_id(event_id);
        event != nullptr) {
      event_name = event->name;
    }
    inspector.event = CanvasConnectorEventFields{event_id, event_name};
    if (const EventListener* const listener = node->find_listener(event_id);
        listener != nullptr) {
      inspector.listener = CanvasConnectorListenerFields{listener->policy(),
                                                         listener->capacity()};
    }
  }
  if (output->destination().has_value()) {
    const ConnectorDestination destination = *output->destination();
    inspector.destination                  = CanvasConnectorDestinationFields{
        destination.node, std::nullopt, destination.connector, std::nullopt};
    if (const Node* const destination_node =
            project.find_node(destination.node);
        destination_node != nullptr) {
      inspector.destination->node_name = destination_node->name();
      if (const InputConnector* const input =
              destination_node->find_input(destination.connector);
          input != nullptr) {
        inspector.destination->input_name = input->name();
      }
    }
  }

  const ValidationReport report =
      ValidationService{}.validate_complete(project);
  std::ranges::copy_if(report.diagnostics,
                       std::back_inserter(inspector.diagnostics),
                       [source_node, output_id](const Diagnostic& diagnostic) {
                         return diagnostic_applies_to_connector(
                             diagnostic, source_node, output_id);
                       });
  return inspector;
}

int canvas_version() noexcept {
  return kCanvasVersion;
}
}  // namespace graphscore
