// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ranges>

namespace {

class RouteMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return {{0.0, 0.0, staff_space, staff_space}, staff_space};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

[[nodiscard]] bool segment_crosses_interior(
    graphscore::GraphPosition first, graphscore::GraphPosition second,
    const graphscore::WorldBounds& bounds) {
  const double right  = bounds.origin.x + bounds.width;
  const double bottom = bounds.origin.y + bounds.height;
  if (first.x == second.x) {
    return first.x > bounds.origin.x && first.x < right &&
           std::min(first.y, second.y) < bottom &&
           std::max(first.y, second.y) > bounds.origin.y;
  }
  return first.y == second.y && first.y > bounds.origin.y && first.y < bottom &&
         std::min(first.x, second.x) < right &&
         std::max(first.x, second.x) > bounds.origin.x;
}

[[nodiscard]] bool point_is_interior(graphscore::GraphPosition      point,
                                     const graphscore::WorldBounds& bounds) {
  return point.x > bounds.origin.x &&
         point.x < bounds.origin.x + bounds.width &&
         point.y > bounds.origin.y && point.y < bounds.origin.y + bounds.height;
}

void expect_well_formed(const graphscore::CanvasConnectorGeometry& connector) {
  ASSERT_GE(connector.route_points.size(), 2U);
  EXPECT_EQ(connector.route_points.front(), connector.source_leg.attachment);
  EXPECT_EQ(connector.route_points[1], connector.source_leg.outer);
  EXPECT_EQ(connector.route_points[connector.route_points.size() - 2U],
            connector.destination_leg.outer);
  EXPECT_EQ(connector.route_points.back(),
            connector.destination_leg.attachment);
  for (std::size_t index = 1; index < connector.route_points.size(); ++index) {
    const auto first  = connector.route_points[index - 1U];
    const auto second = connector.route_points[index];
    EXPECT_TRUE(std::isfinite(first.x));
    EXPECT_TRUE(std::isfinite(first.y));
    EXPECT_NE(first, second);
    EXPECT_TRUE(first.x == second.x || first.y == second.y);
  }
  ASSERT_FALSE(connector.render_path.empty());
  EXPECT_EQ(connector.render_path.front().verb,
            graphscore::CanvasConnectorPathVerb::kMove);
  EXPECT_EQ(connector.render_path.front().end, connector.route_points.front());
  EXPECT_EQ(connector.render_path.back().end, connector.route_points.back());
  std::size_t turn_count = 0U;
  for (std::size_t index = 1U; index + 1U < connector.route_points.size();
       ++index) {
    const auto before = connector.route_points[index - 1U];
    const auto corner = connector.route_points[index];
    const auto after  = connector.route_points[index + 1U];
    turn_count += static_cast<std::size_t>((before.y == corner.y) !=
                                           (corner.y == after.y));
  }
  const auto rounded_count = std::ranges::count(
      connector.render_path, graphscore::CanvasConnectorPathVerb::kQuadratic,
      &graphscore::CanvasConnectorPathElement::verb);
  EXPECT_EQ(rounded_count, turn_count);
  auto current = connector.render_path.front().end;
  for (const auto& element : connector.render_path | std::views::drop(1U)) {
    EXPECT_NE(element.end, current);
    current = element.end;
  }
}

struct RouteFixture {
  graphscore::Project     project{graphscore::ProjectId::generate(), "Routes"};
  graphscore::NodeId      source_id      = project.add_node("Source");
  graphscore::NodeId      destination_id = project.add_node("Destination");
  graphscore::ConnectorId output =
      project.find_node(source_id)->add_output("Out");
  graphscore::ConnectorId input =
      project.find_node(destination_id)->add_input("In");
  RouteMetrics metrics;

  RouteFixture() {
    project.find_node(source_id)->set_position({0.0, 0.0});
    project.find_node(destination_id)->set_position({500.0, 0.0});
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(source_id, output, destination_id, input)
                    .ok());
  }
};

TEST(CanvasConnectorRouteTest, UsesStraightRouteWhenNodeBoundsAreClear) {
  RouteFixture fixture;

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(scene.connectors.size(), 1U);
  expect_well_formed(scene.connectors[0]);
  ASSERT_EQ(scene.connectors[0].route_points.size(), 4U);
  EXPECT_EQ(scene.connectors[0].source_leg.outer.y,
            scene.connectors[0].destination_leg.outer.y);
}

TEST(CanvasConnectorRouteTest, CoalescesCoincidentEndpointClearancePoints) {
  RouteFixture fixture;
  fixture.project.find_node(fixture.destination_id)->set_position({368.0, 0.0});

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(scene.connectors.size(), 1U);
  expect_well_formed(scene.connectors[0]);
  EXPECT_EQ(scene.connectors[0].route_points.size(), 3U);
  EXPECT_EQ(scene.connectors[0].source_leg.outer,
            scene.connectors[0].destination_leg.outer);
}

TEST(CanvasConnectorRouteTest, DetoursAroundInterveningNodeBounds) {
  RouteFixture             fixture;
  const graphscore::NodeId obstacle_id = fixture.project.add_node("Obstacle");
  fixture.project.find_node(obstacle_id)->set_position({375.0, 0.0});
  fixture.project.find_node(fixture.destination_id)->set_position({750.0, 0.0});

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(scene.connectors.size(), 1U);
  expect_well_formed(scene.connectors[0]);
  const auto obstacle = std::ranges::find(
      scene.nodes, obstacle_id, &graphscore::CanvasNodeNotation::node_id);
  ASSERT_NE(obstacle, scene.nodes.end());
  ASSERT_GT(scene.connectors[0].route_points.size(), 4U);
  for (std::size_t index = 1; index < scene.connectors[0].route_points.size();
       ++index) {
    EXPECT_FALSE(segment_crosses_interior(
        scene.connectors[0].route_points[index - 1U],
        scene.connectors[0].route_points[index], obstacle->geometry.bounds));
  }
  auto current = scene.connectors[0].render_path.front().end;
  for (const auto& element :
       scene.connectors[0].render_path | std::views::drop(1U)) {
    if (element.verb == graphscore::CanvasConnectorPathVerb::kQuadratic) {
      for (std::size_t step = 1U; step < 10U; ++step) {
        const double amount  = static_cast<double>(step) / 10.0;
        const double inverse = 1.0 - amount;
        const graphscore::GraphPosition sample{
            inverse * inverse * current.x +
                2.0 * inverse * amount * element.control.x +
                amount * amount * element.end.x,
            inverse * inverse * current.y +
                2.0 * inverse * amount * element.control.y +
                amount * amount * element.end.y};
        EXPECT_FALSE(point_is_interior(sample, obstacle->geometry.bounds));
      }
    }
    current = element.end;
  }
}

TEST(CanvasConnectorRouteTest, CustomizedEmptyRouteRetainsStraightPath) {
  RouteFixture             fixture;
  const graphscore::NodeId obstacle_id = fixture.project.add_node("Obstacle");
  fixture.project.find_node(obstacle_id)->set_position({375.0, 0.0});
  fixture.project.find_node(fixture.destination_id)->set_position({750.0, 0.0});
  ASSERT_TRUE(fixture.project.find_node(fixture.source_id)
                  ->find_output(fixture.output)
                  ->route()
                  .set_custom_route({})
                  .ok());

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(scene.connectors.size(), 1U);
  ASSERT_EQ(scene.connectors[0].route_points.size(), 4U);
  EXPECT_EQ(scene.connectors[0].route_points[1U].y,
            scene.connectors[0].route_points[2U].y);
}

TEST(CanvasConnectorRouteTest, CustomizedEmptyOffsetRouteIgnoresObstacles) {
  RouteFixture             fixture;
  const graphscore::NodeId obstacle_id = fixture.project.add_node("Obstacle");
  fixture.project.find_node(obstacle_id)->set_position({375.0, 100.0});
  fixture.project.find_node(fixture.destination_id)
      ->set_position({750.0, 200.0});
  ASSERT_TRUE(fixture.project.find_node(fixture.source_id)
                  ->find_output(fixture.output)
                  ->route()
                  .set_custom_route({})
                  .ok());

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(scene.connectors.size(), 1U);
  ASSERT_EQ(scene.connectors[0].route_points.size(), 5U);
  EXPECT_EQ(
      scene.connectors[0].route_points[2],
      (graphscore::GraphPosition{scene.connectors[0].destination_leg.outer.x,
                                 scene.connectors[0].source_leg.outer.y}));
}

TEST(CanvasConnectorRouteTest, AvoidsMultipleNodeObstaclesDeterministically) {
  RouteFixture             fixture;
  const graphscore::NodeId first  = fixture.project.add_node("First obstacle");
  const graphscore::NodeId second = fixture.project.add_node("Second obstacle");
  fixture.project.find_node(first)->set_position({375.0, -100.0});
  fixture.project.find_node(second)->set_position({700.0, 100.0});
  fixture.project.find_node(fixture.destination_id)
      ->set_position({1100.0, 0.0});

  const auto first_scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const auto second_scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(first_scene.connectors.size(), 1U);
  ASSERT_EQ(second_scene.connectors.size(), 1U);
  EXPECT_EQ(first_scene.connectors[0].route_points,
            second_scene.connectors[0].route_points);
  expect_well_formed(first_scene.connectors[0]);
  for (const auto& node : first_scene.nodes) {
    if (node.node_id == fixture.source_id ||
        node.node_id == fixture.destination_id) {
      continue;
    }
    for (std::size_t index = 1;
         index < first_scene.connectors[0].route_points.size(); ++index) {
      EXPECT_FALSE(segment_crosses_interior(
          first_scene.connectors[0].route_points[index - 1U],
          first_scene.connectors[0].route_points[index], node.geometry.bounds));
    }
  }
}

TEST(CanvasConnectorRouteTest, MovingObstacleRecomputesAutomaticRoute) {
  RouteFixture             fixture;
  const graphscore::NodeId obstacle_id = fixture.project.add_node("Obstacle");
  fixture.project.find_node(obstacle_id)->set_position({375.0, 0.0});
  fixture.project.find_node(fixture.destination_id)->set_position({750.0, 0.0});
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(scene.connectors.size(), 1U);
  ASSERT_GT(scene.connectors[0].route_points.size(), 4U);
  graphscore::CanvasNodeDragController drag(fixture.project, history, scene);

  ASSERT_TRUE(drag.begin(obstacle_id, {375.0, 0.0}));
  ASSERT_TRUE(drag.update({375.0, 400.0}));

  expect_well_formed(scene.connectors[0]);
  EXPECT_EQ(scene.connectors[0].route_points.size(), 4U);
}

TEST(CanvasConnectorRouteTest, RoutesSelfLoopAroundItsNode) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Self loop"};
  const graphscore::NodeId      node_id = project.add_node("Node");
  graphscore::Node* const       node    = project.find_node(node_id);
  const graphscore::ConnectorId output  = node->add_output("Out");
  const graphscore::ConnectorId input   = node->add_input("In");
  ASSERT_TRUE(
      graphscore::Graph(project).connect(node_id, output, node_id, input).ok());
  const RouteMetrics metrics;

  const auto scene = graphscore::Canvas{}.layout_nodes(project, metrics);

  ASSERT_EQ(scene.connectors.size(), 1U);
  expect_well_formed(scene.connectors[0]);
  ASSERT_GT(scene.connectors[0].route_points.size(), 4U);
  for (std::size_t index = 1; index < scene.connectors[0].route_points.size();
       ++index) {
    EXPECT_FALSE(
        segment_crosses_interior(scene.connectors[0].route_points[index - 1U],
                                 scene.connectors[0].route_points[index],
                                 scene.nodes[0].geometry.bounds));
  }
}

TEST(CanvasConnectorRouteTest, IgnoresUnusableUnrelatedObstacleBounds) {
  RouteFixture             fixture;
  const graphscore::NodeId malformed = fixture.project.add_node("Malformed");
  fixture.project.find_node(malformed)->set_position(
      {std::numeric_limits<double>::max(), 0.0});

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(scene.connectors.size(), 1U);
  expect_well_formed(scene.connectors[0]);
  EXPECT_EQ(scene.connectors[0].source_node, fixture.source_id);
  EXPECT_EQ(scene.connectors[0].destination_node, fixture.destination_id);
}

TEST(CanvasConnectorRouteTest, RoundsEveryNinetyDegreeTurnConsistently) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{40.0, 0.0},
      graphscore::GraphPosition{40.0, 40.0},
      graphscore::GraphPosition{80.0, 40.0},
  };

  const auto path = graphscore::canvas_connector_render_path(route);

  ASSERT_EQ(path.size(), 6U);
  EXPECT_EQ(path[0],
            (graphscore::CanvasConnectorPathElement{
                graphscore::CanvasConnectorPathVerb::kMove, {}, {0.0, 0.0}}));
  EXPECT_EQ(path[1].end, (graphscore::GraphPosition{28.0, 0.0}));
  EXPECT_EQ(path[2], (graphscore::CanvasConnectorPathElement{
                         graphscore::CanvasConnectorPathVerb::kQuadratic,
                         {40.0, 0.0},
                         {40.0, 12.0}}));
  EXPECT_EQ(path[3].end, (graphscore::GraphPosition{40.0, 28.0}));
  EXPECT_EQ(path[4], (graphscore::CanvasConnectorPathElement{
                         graphscore::CanvasConnectorPathVerb::kQuadratic,
                         {40.0, 40.0},
                         {52.0, 40.0}}));
  EXPECT_EQ(path[5].end, (graphscore::GraphPosition{80.0, 40.0}));
}

TEST(CanvasConnectorRouteTest, ClampsCornersOnShortSharedSegments) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{10.0, 0.0},
      graphscore::GraphPosition{10.0, 8.0},
      graphscore::GraphPosition{20.0, 8.0},
  };

  const auto path = graphscore::canvas_connector_render_path(route);

  ASSERT_EQ(path.size(), 5U);
  EXPECT_EQ(path[1].end, (graphscore::GraphPosition{6.0, 0.0}));
  EXPECT_EQ(path[2].end, (graphscore::GraphPosition{10.0, 4.0}));
  EXPECT_EQ(path[3].end, (graphscore::GraphPosition{14.0, 8.0}));
  EXPECT_EQ(path[4].end, (graphscore::GraphPosition{20.0, 8.0}));
}

TEST(CanvasConnectorRouteTest, LeavesStraightAndReversingPointsSharp) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{20.0, 0.0},
      graphscore::GraphPosition{10.0, 0.0},
  };

  const auto path = graphscore::canvas_connector_render_path(route);

  ASSERT_EQ(path.size(), 3U);
  EXPECT_EQ(path[1],
            (graphscore::CanvasConnectorPathElement{
                graphscore::CanvasConnectorPathVerb::kLine, {}, {20.0, 0.0}}));
  EXPECT_EQ(path[2].end, (graphscore::GraphPosition{10.0, 0.0}));
}

}  // namespace
