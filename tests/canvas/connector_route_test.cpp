// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

}  // namespace
