// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <limits>
#include <memory>

namespace {

class DragMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return {{0.0, -staff_space, staff_space, staff_space * 2.0}, staff_space};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

struct DragFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Drag"};
  graphscore::NodeId  source_id           = project.add_node("Source");
  graphscore::NodeId  destination_id      = project.add_node("Destination");
  graphscore::NodeId  unrelated_source_id = project.add_node("Other source");
  graphscore::NodeId  unrelated_destination_id =
      project.add_node("Other destination");
  graphscore::CommandHistory history;
  DragMetrics                metrics;

  DragFixture() {
    graphscore::Node* const source      = project.find_node(source_id);
    graphscore::Node* const destination = project.find_node(destination_id);
    graphscore::Node* const unrelated_source =
        project.find_node(unrelated_source_id);
    graphscore::Node* const unrelated_destination =
        project.find_node(unrelated_destination_id);
    source->set_position({0.0, 0.0});
    destination->set_position({500.0, 100.0});
    unrelated_source->set_position({0.0, 500.0});
    unrelated_destination->set_position({500.0, 500.0});

    const graphscore::ConnectorId output = source->add_output("Out");
    const graphscore::ConnectorId input  = destination->add_input("In");
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(source_id, output, destination_id, input)
                    .ok());

    const graphscore::ConnectorId unrelated_output =
        unrelated_source->add_output("Other out");
    const graphscore::ConnectorId unrelated_input =
        unrelated_destination->add_input("Other in");
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(unrelated_source_id, unrelated_output,
                             unrelated_destination_id, unrelated_input)
                    .ok());
  }
};

TEST(CanvasNodeDragTest, UpdatesAttachedEndpointLegOnEveryPointerMove) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(scene.connectors.size(), 2U);
  const graphscore::CanvasConnectorGeometry unrelated_before =
      scene.connectors[1];
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {10.0, 20.0}));
  ASSERT_TRUE(drag.update({30.0, 50.0}));
  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{20.0, 30.0}));
  EXPECT_EQ(scene.connectors[0].source_leg.attachment,
            (graphscore::GraphPosition{340.0, 142.0}));
  EXPECT_EQ(scene.connectors[0].source_leg.outer,
            (graphscore::GraphPosition{364.0, 142.0}));
  EXPECT_EQ(scene.connectors[0].destination_leg.attachment,
            (graphscore::GraphPosition{500.0, 212.0}));
  EXPECT_EQ(scene.connectors[1], unrelated_before);
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));

  ASSERT_TRUE(drag.update({50.0, 70.0}));
  EXPECT_EQ(scene.connectors[0].source_leg.attachment,
            (graphscore::GraphPosition{360.0, 162.0}));
}

TEST(CanvasNodeDragTest, DestinationDragUpdatesOnlyItsAttachedLeg) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(scene.connectors.size(), 2U);
  const graphscore::CanvasConnectorEndpointLeg source_before =
      scene.connectors[0].source_leg;
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.destination_id, {500.0, 100.0}));
  ASSERT_TRUE(drag.update({550.0, 125.0}));

  EXPECT_EQ(scene.connectors[0].source_leg, source_before);
  EXPECT_EQ(scene.connectors[0].destination_leg.attachment,
            (graphscore::GraphPosition{550.0, 237.0}));
  EXPECT_EQ(scene.connectors[0].destination_leg.outer,
            (graphscore::GraphPosition{526.0, 237.0}));
}

TEST(CanvasNodeDragTest, FinishCommitsOneUndoablePositionChange) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  ASSERT_TRUE(drag.update({25.0, -40.0}));
  ASSERT_TRUE(drag.finish().ok());
  EXPECT_FALSE(drag.active());
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{25.0, -40.0}));

  ASSERT_TRUE(fixture.history.undo(fixture.project).ok());
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));
  ASSERT_TRUE(fixture.history.undo(fixture.project).ok());
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));
}

TEST(CanvasNodeDragTest, CancelRestoresNodeAndEndpointGeometry) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const graphscore::CanvasConnectorGeometry connector_before =
      scene.connectors[0];
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  ASSERT_TRUE(drag.update({90.0, 75.0}));
  drag.cancel();

  EXPECT_FALSE(drag.active());
  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(scene.connectors[0], connector_before);
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));
}

TEST(CanvasNodeDragTest, RejectsInvalidUpdatesWithoutChangingPreview) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const graphscore::CanvasConnectorGeometry connector_before =
      scene.connectors[0];
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  EXPECT_FALSE(drag.update({std::numeric_limits<double>::infinity(), 0.0}));
  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(scene.connectors[0], connector_before);
}

TEST(CanvasNodeDragTest, RejectsFiniteArithmeticOverflowAtomically) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const graphscore::CanvasConnectorGeometry connector_before =
      scene.connectors[0];
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id,
                         {-std::numeric_limits<double>::max(), 0.0}));
  EXPECT_FALSE(drag.update({std::numeric_limits<double>::max(), 0.0}));
  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(scene.connectors[0], connector_before);
}

TEST(CanvasNodeDragTest, RollsBackWhenEndpointArithmeticOverflows) {
  DragFixture      fixture;
  constexpr double start_x = 1.0e10;
  fixture.project.find_node(fixture.source_id)->set_position({start_x, 0.0});
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(scene.connectors.size(), 2U);
  const graphscore::CanvasConnectorGeometry connector_before =
      scene.connectors[0];
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  EXPECT_FALSE(drag.update({std::numeric_limits<double>::max(), 0.0}));
  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{start_x, 0.0}));
  EXPECT_EQ(scene.connectors[0], connector_before);
}

TEST(CanvasNodeDragTest, NoOpFinishDoesNotAddAHistoryEntry) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {15.0, 25.0}));
  ASSERT_TRUE(drag.update({15.0, 25.0}));
  ASSERT_TRUE(drag.finish().ok());
  ASSERT_TRUE(fixture.history.undo(fixture.project).ok());
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));
}

TEST(CanvasNodeDragTest, UpdatesMultipleAttachmentsIncludingASelfLoop) {
  DragFixture             fixture;
  graphscore::Node* const source = fixture.project.find_node(fixture.source_id);
  graphscore::Node* const destination =
      fixture.project.find_node(fixture.destination_id);
  const graphscore::ConnectorId second_output = source->add_output("Second");
  const graphscore::ConnectorId second_input = destination->add_input("Second");
  ASSERT_TRUE(graphscore::Graph(fixture.project)
                  .connect(fixture.source_id, second_output,
                           fixture.destination_id, second_input)
                  .ok());
  const graphscore::ConnectorId loop_output = source->add_output("Loop out");
  const graphscore::ConnectorId loop_input  = source->add_input("Loop in");
  ASSERT_TRUE(graphscore::Graph(fixture.project)
                  .connect(fixture.source_id, loop_output, fixture.source_id,
                           loop_input)
                  .ok());
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  ASSERT_EQ(scene.connectors.size(), 4U);
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  ASSERT_TRUE(drag.update({30.0, 40.0}));

  EXPECT_EQ(scene.connectors[0].source_leg.attachment,
            (graphscore::GraphPosition{350.0, 152.0}));
  EXPECT_EQ(scene.connectors[1].source_leg.attachment,
            scene.connectors[0].source_leg.attachment);
  EXPECT_EQ(scene.connectors[2].source_leg.attachment,
            scene.connectors[0].source_leg.attachment);
  EXPECT_EQ(scene.connectors[2].destination_leg.attachment,
            (graphscore::GraphPosition{30.0, 152.0}));
}

TEST(CanvasNodeDragTest, DestructorCancelsAnUnfinishedDrag) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  const graphscore::CanvasConnectorGeometry connector_before =
      scene.connectors[0];
  {
    graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                              scene);
    ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
    ASSERT_TRUE(drag.update({10.0, 15.0}));
  }

  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(scene.connectors[0], connector_before);
}

TEST(CanvasNodeDragTest, RejectsStaleProjectStateAndRestoresPreview) {
  DragFixture                     fixture;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);
  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  ASSERT_TRUE(drag.update({10.0, 15.0}));
  fixture.project.find_node(fixture.source_id)->set_position({200.0, 300.0});

  EXPECT_EQ(drag.finish().code(), graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{200.0, 300.0}));
}

TEST(CanvasNodeDragTest, HistoryRejectionRestoresPreview) {
  DragFixture                             fixture;
  graphscore::CommandHistory::Transaction transaction =
      fixture.history.begin_transaction(
          std::make_unique<graphscore::SetNodePositionCommand>(
              fixture.unrelated_source_id,
              graphscore::GraphPosition{50.0, 500.0}),
          fixture.project);
  ASSERT_TRUE(transaction.active());
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);
  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  ASSERT_TRUE(drag.update({10.0, 15.0}));

  EXPECT_EQ(drag.finish().code(), graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(scene.nodes[0].position, (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_TRUE(transaction.abort().ok());
}

TEST(CanvasNodeDragTest, OmitsMalformedDestinationReferences) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Malformed"};
  const graphscore::NodeId source_id      = project.add_node("Source");
  const graphscore::NodeId destination_id = project.add_node("Destination");
  graphscore::Node* const  source         = project.find_node(source_id);
  graphscore::Node* const  destination    = project.find_node(destination_id);
  const graphscore::ConnectorId missing_input =
      graphscore::ConnectorId::generate();
  const graphscore::ConnectorId bad_input_output =
      source->add_output("Bad input");
  source->find_output(bad_input_output)
      ->set_destination(
          graphscore::ConnectorDestination{destination_id, missing_input});
  const graphscore::ConnectorId missing_node_output =
      source->add_output("Missing node");
  source->find_output(missing_node_output)
      ->set_destination(graphscore::ConnectorDestination{
          graphscore::NodeId::generate(), destination->add_input("Unused")});
  const DragMetrics metrics;

  const graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(project, metrics);

  EXPECT_TRUE(scene.connectors.empty());
}

}  // namespace
