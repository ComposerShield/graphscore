// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace {

class DeleteConnectorMetrics final : public graphscore::GlyphMetrics {
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

struct DeleteConnectorFixture {
  graphscore::Project     project{graphscore::ProjectId::generate(), "Delete"};
  graphscore::NodeId      source_id = project.add_node("Source");
  graphscore::NodeId      target_id = project.add_node("Target");
  graphscore::Node*       source    = project.find_node(source_id);
  graphscore::Node*       target    = project.find_node(target_id);
  graphscore::ConnectorId output    = source->add_output("Reusable output");
  graphscore::ConnectorId input     = target->add_input("Input");
  DeleteConnectorMetrics  metrics;

  DeleteConnectorFixture() {
    source->set_position({0.0, 0.0});
    target->set_position({600.0, 0.0});
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(source_id, output, target_id, input)
                    .ok());
  }
};

TEST(CanvasConnectorDeleteTest,
     DeletesSelectedConnectionThroughOneUndoableCommand) {
  DeleteConnectorFixture fixture;
  ASSERT_TRUE(fixture.source->find_output(fixture.output)
                  ->route()
                  .set_custom_route({{400.0, 300.0}, {500.0, 300.0}})
                  .ok());
  const graphscore::RouteGeometry route =
      fixture.source->find_output(fixture.output)->route();
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CommandHistory                     history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};

  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  ASSERT_TRUE(controller.delete_selected_connector().ok());

  EXPECT_FALSE(
      fixture.source->find_output(fixture.output)->destination().has_value());
  EXPECT_NE(fixture.source->find_output(fixture.output), nullptr);
  EXPECT_TRUE(scene.connectors.empty());
  EXPECT_FALSE(controller.selection().has_value());
  EXPECT_EQ(history.undo_stack_size(), 1U);

  ASSERT_TRUE(history.undo(fixture.project).ok());
  const graphscore::OutputConnector* const restored =
      fixture.source->find_output(fixture.output);
  ASSERT_TRUE(restored->destination().has_value());
  EXPECT_EQ(restored->destination()->node, fixture.target_id);
  EXPECT_EQ(restored->destination()->connector, fixture.input);
  EXPECT_EQ(restored->route(), route);
  ASSERT_TRUE(history.redo(fixture.project).ok());
  EXPECT_FALSE(restored->destination().has_value());
}

TEST(CanvasConnectorDeleteTest, RejectsMissingOrStaleSelection) {
  DeleteConnectorFixture          fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CommandHistory                     history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};

  EXPECT_EQ(controller.delete_selected_connector().code(),
            graphscore::ResultCode::kInvalidArgument);
  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  ASSERT_TRUE(graphscore::Graph(fixture.project)
                  .disconnect(fixture.source_id, fixture.output)
                  .ok());
  EXPECT_EQ(controller.delete_selected_connector().code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(scene.connectors.size(), 1U);
  EXPECT_TRUE(controller.selection().has_value());
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorDeleteTest, RejectsStaleSelectedRouteGeometry) {
  DeleteConnectorFixture          fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const auto                 scene_before = scene.connectors;
  graphscore::CommandHistory history;
  graphscore::CanvasConnectorSelectionController controller{fixture.project,
                                                            history, scene};
  ASSERT_TRUE(controller.select(fixture.source_id, fixture.output));
  ASSERT_TRUE(fixture.source->find_output(fixture.output)
                  ->route()
                  .set_custom_route({{400.0, 300.0}, {500.0, 300.0}})
                  .ok());

  EXPECT_EQ(controller.delete_selected_connector().code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(scene.connectors, scene_before);
  EXPECT_TRUE(controller.selection().has_value());
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorDeleteTest, PreservesActiveHistoryRejectionCode) {
  DeleteConnectorFixture          fixture;
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

  EXPECT_EQ(controller.delete_selected_connector().code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_TRUE(
      fixture.source->find_output(fixture.output)->destination().has_value());
  EXPECT_EQ(scene.connectors, scene_before);
  EXPECT_TRUE(controller.selection().has_value());
  EXPECT_EQ(history.undo_stack_size(), 0U);
  EXPECT_TRUE(transaction.abort().ok());
}

}  // namespace
