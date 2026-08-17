// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

namespace {

class AttachmentMetrics final : public graphscore::GlyphMetrics {
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

struct AttachmentFixture {
  graphscore::Project     project{graphscore::ProjectId::generate(), "Attach"};
  graphscore::NodeId      source_id        = project.add_node("Source");
  graphscore::NodeId      first_target_id  = project.add_node("First target");
  graphscore::NodeId      second_target_id = project.add_node("Second target");
  graphscore::ConnectorId first_output =
      project.find_node(source_id)->add_output("First output");
  graphscore::ConnectorId second_output =
      project.find_node(source_id)->add_output("Second output");
  graphscore::ConnectorId first_input =
      project.find_node(first_target_id)->add_input("First input");
  graphscore::ConnectorId second_input =
      project.find_node(second_target_id)->add_input("Second input");
  graphscore::CommandHistory      history;
  AttachmentMetrics               metrics;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(project, metrics);
  graphscore::CanvasConnectorAttachmentController controller{project, history,
                                                             scene};
};

TEST(CanvasConnectorAttachmentTest, CommitsOneUndoableAttachment) {
  AttachmentFixture fixture;

  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  ASSERT_TRUE(
      fixture.controller.finish(fixture.first_target_id, fixture.first_input)
          .ok());

  EXPECT_FALSE(fixture.controller.active());
  ASSERT_EQ(fixture.scene.connectors.size(), 1U);
  EXPECT_EQ(fixture.scene.connectors[0].source_connector, fixture.first_output);
  EXPECT_EQ(fixture.scene.connectors[0].destination_connector,
            fixture.first_input);
  const graphscore::OutputConnector* const output =
      fixture.project.find_node(fixture.source_id)
          ->find_output(fixture.first_output);
  ASSERT_TRUE(output->destination().has_value());
  EXPECT_EQ(output->destination()->node, fixture.first_target_id);
  EXPECT_EQ(fixture.history.undo_stack_size(), 1U);

  ASSERT_TRUE(fixture.history.undo(fixture.project).ok());
  EXPECT_FALSE(output->destination().has_value());
}

TEST(CanvasConnectorAttachmentTest, RejectsAnOccupiedOutputWithoutRetargeting) {
  AttachmentFixture fixture;
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  ASSERT_TRUE(
      fixture.controller.finish(fixture.first_target_id, fixture.first_input)
          .ok());

  EXPECT_FALSE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  const graphscore::OutputConnector* const output =
      fixture.project.find_node(fixture.source_id)
          ->find_output(fixture.first_output);
  ASSERT_TRUE(output->destination().has_value());
  EXPECT_EQ(output->destination()->node, fixture.first_target_id);
  EXPECT_EQ(output->destination()->connector, fixture.first_input);
  EXPECT_EQ(fixture.scene.connectors.size(), 1U);
  EXPECT_EQ(fixture.history.undo_stack_size(), 1U);
}

TEST(CanvasConnectorAttachmentTest, AllowsDistinctOutputsToShareAnInput) {
  AttachmentFixture fixture;
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  ASSERT_TRUE(
      fixture.controller.finish(fixture.first_target_id, fixture.first_input)
          .ok());
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.second_output));
  ASSERT_TRUE(
      fixture.controller.finish(fixture.first_target_id, fixture.first_input)
          .ok());

  EXPECT_EQ(fixture.scene.connectors.size(), 2U);
  EXPECT_EQ(fixture.history.undo_stack_size(), 2U);
}

TEST(CanvasConnectorAttachmentTest, RetainsProjectOrderWhenAttachedInReverse) {
  AttachmentFixture fixture;
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.second_output));
  ASSERT_TRUE(
      fixture.controller.finish(fixture.second_target_id, fixture.second_input)
          .ok());
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  ASSERT_TRUE(
      fixture.controller.finish(fixture.first_target_id, fixture.first_input)
          .ok());

  ASSERT_EQ(fixture.scene.connectors.size(), 2U);
  EXPECT_EQ(fixture.scene.connectors[0].source_connector, fixture.first_output);
  EXPECT_EQ(fixture.scene.connectors[1].source_connector,
            fixture.second_output);
}

TEST(CanvasConnectorAttachmentTest, RejectsWrongPortDirections) {
  AttachmentFixture fixture;

  EXPECT_FALSE(
      fixture.controller.begin(fixture.first_target_id, fixture.first_input));
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  EXPECT_EQ(fixture.controller.finish(fixture.source_id, fixture.second_output)
                .code(),
            graphscore::ResultCode::kInvalidArgument);

  EXPECT_TRUE(fixture.scene.connectors.empty());
  EXPECT_EQ(fixture.history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorAttachmentTest, CancelLeavesTheGraphAndSceneUnchanged) {
  AttachmentFixture fixture;
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));

  fixture.controller.cancel();

  EXPECT_FALSE(fixture.controller.active());
  EXPECT_FALSE(fixture.project.find_node(fixture.source_id)
                   ->find_output(fixture.first_output)
                   ->destination()
                   .has_value());
  EXPECT_TRUE(fixture.scene.connectors.empty());
  EXPECT_EQ(fixture.history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorAttachmentTest,
     StaleAttachmentFailsWithoutDuplicateGeometry) {
  AttachmentFixture fixture;
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  ASSERT_TRUE(graphscore::Graph(fixture.project)
                  .connect(fixture.source_id, fixture.first_output,
                           fixture.first_target_id, fixture.first_input)
                  .ok());

  EXPECT_EQ(
      fixture.controller.finish(fixture.second_target_id, fixture.second_input)
          .code(),
      graphscore::ResultCode::kInvalidArgument);

  const graphscore::OutputConnector* const output =
      fixture.project.find_node(fixture.source_id)
          ->find_output(fixture.first_output);
  ASSERT_TRUE(output->destination().has_value());
  EXPECT_EQ(output->destination()->node, fixture.first_target_id);
  EXPECT_TRUE(fixture.scene.connectors.empty());
  EXPECT_EQ(fixture.history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorAttachmentTest, RejectsGeometryLeftStaleByUndo) {
  AttachmentFixture fixture;
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  ASSERT_TRUE(
      fixture.controller.finish(fixture.first_target_id, fixture.first_input)
          .ok());
  ASSERT_TRUE(fixture.history.undo(fixture.project).ok());

  EXPECT_FALSE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  EXPECT_EQ(fixture.scene.connectors.size(), 1U);
  EXPECT_EQ(fixture.history.undo_stack_size(), 0U);
  EXPECT_EQ(fixture.history.redo_stack_size(), 1U);
}

TEST(CanvasConnectorAttachmentTest, RejectsSourceGeometryThatStalesMidGesture) {
  AttachmentFixture fixture;
  ASSERT_TRUE(
      fixture.controller.begin(fixture.source_id, fixture.first_output));
  ASSERT_TRUE(graphscore::Graph(fixture.project)
                  .connect(fixture.source_id, fixture.first_output,
                           fixture.first_target_id, fixture.first_input)
                  .ok());
  const auto connected_scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_TRUE(graphscore::Graph(fixture.project)
                  .disconnect(fixture.source_id, fixture.first_output)
                  .ok());
  fixture.scene.connectors = connected_scene.connectors;

  EXPECT_EQ(
      fixture.controller.finish(fixture.second_target_id, fixture.second_input)
          .code(),
      graphscore::ResultCode::kInvalidArgument);
  EXPECT_FALSE(fixture.project.find_node(fixture.source_id)
                   ->find_output(fixture.first_output)
                   ->destination()
                   .has_value());
  EXPECT_EQ(fixture.scene.connectors.size(), 1U);
  EXPECT_EQ(fixture.history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorAttachmentTest, PermitsASelfLoop) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Loop"};
  const graphscore::NodeId node_id     = project.add_node("Loop node");
  graphscore::Node* const  node        = project.find_node(node_id);
  const graphscore::ConnectorId output = node->add_output("Loop output");
  const graphscore::ConnectorId input  = node->add_input("Loop input");
  AttachmentMetrics             metrics;
  graphscore::CommandHistory    history;
  auto scene = graphscore::Canvas{}.layout_nodes(project, metrics);
  graphscore::CanvasConnectorAttachmentController controller{project, history,
                                                             scene};

  ASSERT_TRUE(controller.begin(node_id, output));
  ASSERT_TRUE(controller.finish(node_id, input).ok());

  ASSERT_EQ(scene.connectors.size(), 1U);
  EXPECT_EQ(scene.connectors[0].source_node, node_id);
  EXPECT_EQ(scene.connectors[0].destination_node, node_id);
  EXPECT_EQ(history.undo_stack_size(), 1U);
}

TEST(CanvasConnectorAttachmentTest,
     PermitsAnOrdinaryCycleThroughTheUndoableAuthoringPath) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Cycle"};
  const graphscore::NodeId first_id           = project.add_node("First");
  const graphscore::NodeId second_id          = project.add_node("Second");
  graphscore::Node* const  first              = project.find_node(first_id);
  graphscore::Node* const  second             = project.find_node(second_id);
  const graphscore::ConnectorId first_output  = first->add_output("To second");
  const graphscore::ConnectorId first_input   = first->add_input("From second");
  const graphscore::ConnectorId second_output = second->add_output("To first");
  const graphscore::ConnectorId second_input  = second->add_input("From first");
  AttachmentMetrics             metrics;
  graphscore::CommandHistory    history;
  auto scene = graphscore::Canvas{}.layout_nodes(project, metrics);
  graphscore::CanvasConnectorAttachmentController controller{project, history,
                                                             scene};

  ASSERT_TRUE(controller.begin(first_id, first_output));
  ASSERT_TRUE(controller.finish(second_id, second_input).ok());
  ASSERT_TRUE(controller.begin(second_id, second_output));
  ASSERT_TRUE(controller.finish(first_id, first_input).ok());

  ASSERT_EQ(scene.connectors.size(), 2U);
  EXPECT_EQ(scene.connectors[0].source_node, first_id);
  EXPECT_EQ(scene.connectors[0].destination_node, second_id);
  EXPECT_EQ(scene.connectors[1].source_node, second_id);
  EXPECT_EQ(scene.connectors[1].destination_node, first_id);
  EXPECT_EQ(history.undo_stack_size(), 2U);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_FALSE(second->find_output(second_output)->destination().has_value());
  ASSERT_TRUE(history.redo(project).ok());
  ASSERT_TRUE(second->find_output(second_output)->destination().has_value());
  EXPECT_EQ(second->find_output(second_output)->destination()->node, first_id);
}

}  // namespace
