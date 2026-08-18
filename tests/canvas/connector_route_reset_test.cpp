// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <memory>
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
