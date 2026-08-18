// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
#include <vector>

namespace {

class ResetRouteMetrics final : public graphscore::GlyphMetrics {
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

struct ResetRouteFixture {
  graphscore::Project     project{graphscore::ProjectId::generate(), "Reset"};
  graphscore::NodeId      source_id = project.add_node("Source");
  graphscore::NodeId      target_id = project.add_node("Target");
  graphscore::Node*       source    = project.find_node(source_id);
  graphscore::Node*       target    = project.find_node(target_id);
  graphscore::ConnectorId output    = source->add_output("Out");
  graphscore::ConnectorId input     = target->add_input("In");
  ResetRouteMetrics       metrics;

  ResetRouteFixture() {
    source->set_position({0.0, 0.0});
    target->set_position({600.0, 0.0});
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(source_id, output, target_id, input)
                    .ok());
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

void expect_orthogonal_with_rounded_turns(
    const graphscore::CanvasConnectorGeometry& connector) {
  ASSERT_GE(connector.route_points.size(), 2U);
  for (std::size_t index = 1U; index < connector.route_points.size(); ++index) {
    const graphscore::GraphPosition first  = connector.route_points[index - 1U];
    const graphscore::GraphPosition second = connector.route_points[index];
    EXPECT_TRUE(first.x == second.x || first.y == second.y);
  }

  std::size_t turn_count = 0U;
  for (std::size_t index = 1U; index + 1U < connector.route_points.size();
       ++index) {
    const graphscore::GraphPosition before = connector.route_points[index - 1U];
    const graphscore::GraphPosition corner = connector.route_points[index];
    const graphscore::GraphPosition after  = connector.route_points[index + 1U];
    turn_count += static_cast<std::size_t>((before.y == corner.y) !=
                                           (corner.y == after.y));
  }
  ASSERT_GT(turn_count, 0U);
  EXPECT_EQ(std::ranges::count(connector.render_path,
                               graphscore::CanvasConnectorPathVerb::kQuadratic,
                               &graphscore::CanvasConnectorPathElement::verb),
            turn_count);
}

TEST(CanvasConnectorRouteResetTest,
     ResetsSelectedCustomRouteThroughOneUndoableCommand) {
  ResetRouteFixture                         fixture;
  const std::vector<graphscore::RoutePoint> waypoints = {{400.0, 300.0},
                                                         {500.0, 300.0}};
  ASSERT_TRUE(fixture.source->find_output(fixture.output)
                  ->route()
                  .set_custom_route(waypoints)
                  .ok());

  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const auto                 custom_geometry = scene.connectors.front();
  graphscore::CommandHistory history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};

  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  ASSERT_TRUE(controller.reset_selected_route().ok());

  EXPECT_TRUE(
      fixture.source->find_output(fixture.output)->route().is_automatic());
  EXPECT_EQ(history.undo_stack_size(), 1U);
  EXPECT_NE(scene.connectors.front().route_points,
            custom_geometry.route_points);

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_FALSE(
      fixture.source->find_output(fixture.output)->route().is_automatic());
  EXPECT_EQ(fixture.source->find_output(fixture.output)->route().waypoints(),
            waypoints);
  ASSERT_TRUE(history.redo(fixture.project).ok());
  EXPECT_TRUE(
      fixture.source->find_output(fixture.output)->route().is_automatic());
}

TEST(CanvasConnectorRouteResetTest, AutomaticRouteIsASuccessfulNoOp) {
  ResetRouteFixture               fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CommandHistory                     history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};

  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  EXPECT_TRUE(controller.reset_selected_route().ok());
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorRouteResetTest,
     ResetRestoresTheDeterministicObstacleAvoidingRoute) {
  ResetRouteFixture fixture;
  fixture.target->set_position({750.0, 0.0});
  const graphscore::NodeId obstacle_id = fixture.project.add_node("Obstacle");
  fixture.project.find_node(obstacle_id)->set_position({375.0, 0.0});

  const graphscore::CanvasNotationScene automatic_scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(automatic_scene.connectors.size(), 1U);
  ASSERT_GT(automatic_scene.connectors.front().route_points.size(), 4U);
  const auto obstacle =
      std::ranges::find(automatic_scene.nodes, obstacle_id,
                        &graphscore::CanvasNodeNotation::node_id);
  ASSERT_NE(obstacle, automatic_scene.nodes.end());
  for (std::size_t index = 1U;
       index < automatic_scene.connectors.front().route_points.size();
       ++index) {
    EXPECT_FALSE(segment_crosses_interior(
        automatic_scene.connectors.front().route_points[index - 1U],
        automatic_scene.connectors.front().route_points[index],
        obstacle->geometry.bounds));
  }

  ASSERT_TRUE(fixture.project.find_node(fixture.source_id)
                  ->find_output(fixture.output)
                  ->route()
                  .set_custom_route({{400.0, 300.0}, {500.0, 300.0}})
                  .ok());
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(scene.connectors.size(), 1U);
  expect_orthogonal_with_rounded_turns(scene.connectors.front());
  ASSERT_NE(scene.connectors.front().route_points,
            automatic_scene.connectors.front().route_points);
  graphscore::CommandHistory                     history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};

  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  ASSERT_TRUE(controller.reset_selected_route().ok());

  EXPECT_EQ(scene.connectors.front(), automatic_scene.connectors.front());
  ASSERT_TRUE(history.undo(fixture.project).ok());
  ASSERT_TRUE(history.redo(fixture.project).ok());
  const graphscore::CanvasNotationScene replayed_scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(replayed_scene.connectors.size(), 1U);
  EXPECT_EQ(replayed_scene.connectors.front(),
            automatic_scene.connectors.front());
}

TEST(CanvasConnectorRouteResetTest, RejectsMissingOrStaleSelection) {
  ResetRouteFixture               fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CommandHistory                     history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};

  EXPECT_FALSE(controller.select(fixture.source_id,
                                 graphscore::ConnectorId::generate()));
  EXPECT_EQ(controller.reset_selected_route().code(),
            graphscore::ResultCode::kInvalidArgument);

  fixture.target->set_position({700.0, 0.0});
  EXPECT_FALSE(controller.select(fixture.source_id, fixture.output));
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorRouteResetTest, RejectsStaleSelectedRouteGeometry) {
  ResetRouteFixture fixture;
  ASSERT_TRUE(fixture.source->find_output(fixture.output)
                  ->route()
                  .set_custom_route({{400.0, 300.0}, {500.0, 300.0}})
                  .ok());
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const auto                 scene_before = scene.connectors;
  graphscore::CommandHistory history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};
  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  fixture.source->find_output(fixture.output)->route().reset_to_automatic();

  EXPECT_EQ(controller.reset_selected_route().code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(scene.connectors, scene_before);
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorRouteResetTest, PreservesActiveHistoryRejectionCode) {
  ResetRouteFixture fixture;
  ASSERT_TRUE(fixture.source->find_output(fixture.output)
                  ->route()
                  .set_custom_route({{400.0, 300.0}, {500.0, 300.0}})
                  .ok());
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const auto                 scene_before = scene.connectors;
  graphscore::CommandHistory history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};
  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  auto transaction = history.begin_transaction(
      std::make_unique<graphscore::SetNodeNameCommand>(fixture.target_id,
                                                       "Renamed"),
      fixture.project);
  ASSERT_TRUE(transaction.active());

  EXPECT_EQ(controller.reset_selected_route().code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_FALSE(
      fixture.source->find_output(fixture.output)->route().is_automatic());
  EXPECT_EQ(scene.connectors, scene_before);
  EXPECT_EQ(history.undo_stack_size(), 0U);
  EXPECT_TRUE(transaction.abort().ok());
}

}  // namespace
