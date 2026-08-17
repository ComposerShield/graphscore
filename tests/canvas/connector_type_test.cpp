// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

namespace {

class ConnectorTypeMetrics final : public graphscore::GlyphMetrics {
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

struct ConnectorTypeFixture {
  graphscore::Project     project{graphscore::ProjectId::generate(), "Types"};
  graphscore::NodeId      source_id = project.add_node("Source");
  graphscore::NodeId      target_id = project.add_node("Target");
  graphscore::Node*       source    = project.find_node(source_id);
  graphscore::Node*       target    = project.find_node(target_id);
  graphscore::ConnectorId sequential_output =
      source->add_output("Sequential", graphscore::ConnectorType::kSequential);
  graphscore::ConnectorId vertical_output =
      source->add_output("Vertical", graphscore::ConnectorType::kVertical);
  graphscore::ConnectorId first_input  = target->add_input("First");
  graphscore::ConnectorId second_input = target->add_input("Second");
  ConnectorTypeMetrics    metrics;

  ConnectorTypeFixture() {
    EXPECT_TRUE(
        graphscore::Graph(project)
            .connect(source_id, sequential_output, target_id, first_input)
            .ok());
    EXPECT_TRUE(
        graphscore::Graph(project)
            .connect(source_id, vertical_output, target_id, second_input)
            .ok());
  }
};

TEST(CanvasConnectorTypeTest, DerivesRedundantColorAndPatternFromSemanticType) {
  ConnectorTypeFixture fixture;

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_EQ(scene.connectors.size(), 2U);
  const auto& sequential = scene.connectors[0];
  const auto& vertical   = scene.connectors[1];
  EXPECT_EQ(sequential.type, graphscore::ConnectorType::kSequential);
  EXPECT_EQ(vertical.type, graphscore::ConnectorType::kVertical);
  EXPECT_NE(sequential.style.color_rgba, vertical.style.color_rgba);
  EXPECT_NE(sequential.style.line_pattern, vertical.style.line_pattern);
  EXPECT_EQ(sequential.style.color_rgba, 0x2F80EDFFU);
  EXPECT_EQ(vertical.style.color_rgba, 0xD35400FFU);
  EXPECT_EQ(sequential.style.line_pattern,
            graphscore::CanvasConnectorLinePattern::kSolid);
  EXPECT_EQ(vertical.style.line_pattern,
            graphscore::CanvasConnectorLinePattern::kDashed);
}

TEST(CanvasConnectorTypeTest,
     AuthorsTypeThroughUndoableCommandAndUpdatesScene) {
  ConnectorTypeFixture       fixture;
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasConnectorTypeController controller{fixture.project, history,
                                                       scene};

  ASSERT_TRUE(controller
                  .set_type(fixture.source_id, fixture.sequential_output,
                            graphscore::ConnectorType::kVertical)
                  .ok());

  EXPECT_EQ(fixture.source->find_output(fixture.sequential_output)->type(),
            graphscore::ConnectorType::kVertical);
  EXPECT_EQ(scene.connectors[0].type, graphscore::ConnectorType::kVertical);
  EXPECT_EQ(
      scene.connectors[0].style,
      graphscore::canvas_connector_style(graphscore::ConnectorType::kVertical));
  EXPECT_EQ(history.undo_stack_size(), 1U);

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_EQ(fixture.source->find_output(fixture.sequential_output)->type(),
            graphscore::ConnectorType::kSequential);
}

TEST(CanvasConnectorTypeTest, AuthorsTypeBeforeAnOutputIsAttached) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Unattached"};
  const graphscore::NodeId      node_id   = project.add_node("Node");
  const graphscore::NodeId      target_id = project.add_node("Target");
  const graphscore::ConnectorId output =
      project.find_node(node_id)->add_output("Output");
  const graphscore::ConnectorId input =
      project.find_node(target_id)->add_input("Input");
  ConnectorTypeMetrics       metrics;
  graphscore::CommandHistory history;
  auto scene = graphscore::Canvas{}.layout_nodes(project, metrics);
  graphscore::CanvasConnectorTypeController controller{project, history, scene};

  ASSERT_TRUE(
      controller.set_type(node_id, output, graphscore::ConnectorType::kVertical)
          .ok());

  EXPECT_EQ(project.find_node(node_id)->find_output(output)->type(),
            graphscore::ConnectorType::kVertical);
  EXPECT_TRUE(scene.connectors.empty());
  EXPECT_EQ(history.undo_stack_size(), 1U);

  graphscore::CanvasConnectorAttachmentController attachment{project, history,
                                                             scene};
  ASSERT_TRUE(attachment.begin(node_id, output));
  ASSERT_TRUE(attachment.finish(target_id, input).ok());
  ASSERT_EQ(scene.connectors.size(), 1U);
  EXPECT_EQ(scene.connectors[0].type, graphscore::ConnectorType::kVertical);
  EXPECT_EQ(
      scene.connectors[0].style,
      graphscore::canvas_connector_style(graphscore::ConnectorType::kVertical));
}

TEST(CanvasConnectorTypeTest, RejectsInvalidTypesForConnectedAndFreeOutputs) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Invalid"};
  const graphscore::NodeId      source_id = project.add_node("Source");
  const graphscore::NodeId      target_id = project.add_node("Target");
  graphscore::Node* const       source    = project.find_node(source_id);
  const graphscore::ConnectorId connected = source->add_output("Connected");
  const graphscore::ConnectorId free      = source->add_output("Free");
  const graphscore::ConnectorId input =
      project.find_node(target_id)->add_input("Input");
  ASSERT_TRUE(graphscore::Graph(project)
                  .connect(source_id, connected, target_id, input)
                  .ok());
  ConnectorTypeMetrics       metrics;
  graphscore::CommandHistory history;
  auto       scene        = graphscore::Canvas{}.layout_nodes(project, metrics);
  const auto scene_before = scene.connectors;
  graphscore::CanvasConnectorTypeController controller{project, history, scene};
  const auto invalid_type = static_cast<graphscore::ConnectorType>(0xFFU);

  EXPECT_EQ(controller.set_type(source_id, connected, invalid_type).code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(controller.set_type(source_id, free, invalid_type).code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(source->find_output(connected)->type(),
            graphscore::ConnectorType::kSequential);
  EXPECT_EQ(source->find_output(free)->type(),
            graphscore::ConnectorType::kSequential);
  EXPECT_EQ(scene.connectors, scene_before);
  EXPECT_EQ(history.undo_stack_size(), 0U);
  EXPECT_EQ(graphscore::canvas_connector_style(invalid_type),
            graphscore::CanvasConnectorStyle{});
}

TEST(CanvasConnectorTypeTest, PreservesVerticalStyleDuringEndpointDrag) {
  ConnectorTypeFixture       fixture;
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const auto                           style_before = scene.connectors[1].style;
  graphscore::CanvasNodeDragController drag{fixture.project, history, scene};

  ASSERT_TRUE(drag.begin(fixture.target_id, {0.0, 0.0}));
  ASSERT_TRUE(drag.update({40.0, 20.0}));

  EXPECT_EQ(scene.connectors[1].type, graphscore::ConnectorType::kVertical);
  EXPECT_EQ(scene.connectors[1].style, style_before);
}

TEST(CanvasConnectorTypeTest, CommandRejectionLeavesSceneUnchanged) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Conflict"};
  const graphscore::NodeId      node_id = project.add_node("Node");
  graphscore::Node* const       node    = project.find_node(node_id);
  const graphscore::ConnectorId first   = node->add_output("First");
  const graphscore::ConnectorId second  = node->add_output("Second");
  const graphscore::EventId     event   = graphscore::EventId::generate();
  ASSERT_TRUE(node->bind_output_event(first, event).ok());
  ASSERT_TRUE(node->bind_output_event(second, event).ok());
  ConnectorTypeMetrics       metrics;
  graphscore::CommandHistory history;
  auto       scene        = graphscore::Canvas{}.layout_nodes(project, metrics);
  const auto scene_before = scene.connectors;
  graphscore::CanvasConnectorTypeController controller{project, history, scene};

  EXPECT_EQ(
      controller.set_type(node_id, first, graphscore::ConnectorType::kVertical)
          .code(),
      graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(node->find_output(first)->type(),
            graphscore::ConnectorType::kSequential);
  EXPECT_EQ(scene.connectors, scene_before);
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorTypeTest, RejectsStaleStyleWithoutChangingTheProject) {
  ConnectorTypeFixture       fixture;
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  scene.connectors[0].style =
      graphscore::canvas_connector_style(graphscore::ConnectorType::kVertical);
  graphscore::CanvasConnectorTypeController controller{fixture.project, history,
                                                       scene};

  EXPECT_EQ(controller
                .set_type(fixture.source_id, fixture.sequential_output,
                          graphscore::ConnectorType::kVertical)
                .code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(fixture.source->find_output(fixture.sequential_output)->type(),
            graphscore::ConnectorType::kSequential);
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorTypeTest, ReauthoringTheCurrentTypeIsANoOp) {
  ConnectorTypeFixture       fixture;
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasConnectorTypeController controller{fixture.project, history,
                                                       scene};

  EXPECT_TRUE(controller
                  .set_type(fixture.source_id, fixture.sequential_output,
                            graphscore::ConnectorType::kSequential)
                  .ok());
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

}  // namespace
