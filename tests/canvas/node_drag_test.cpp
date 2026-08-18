// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

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

[[nodiscard]] bool segment_crosses_interior(
    graphscore::GraphPosition first, graphscore::GraphPosition second,
    const graphscore::WorldBounds& bounds) {
  const double right  = bounds.origin.x + bounds.width;
  const double bottom = bounds.origin.y + bounds.height;
  return (first.x == second.x && first.x > bounds.origin.x && first.x < right &&
          std::min(first.y, second.y) < bottom &&
          std::max(first.y, second.y) > bounds.origin.y) ||
         (first.y == second.y && first.y > bounds.origin.y &&
          first.y < bottom && std::min(first.x, second.x) < right &&
          std::max(first.x, second.x) > bounds.origin.x);
}

struct DragFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Drag"};
  graphscore::NodeId  source_id           = project.add_node("Source");
  graphscore::NodeId  destination_id      = project.add_node("Destination");
  graphscore::NodeId  unrelated_source_id = project.add_node("Other source");
  graphscore::NodeId  unrelated_destination_id =
      project.add_node("Other destination");
  graphscore::ConnectorId output =
      project.find_node(source_id)->add_output("Out");
  graphscore::ConnectorId input =
      project.find_node(destination_id)->add_input("In");
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
  EXPECT_EQ(scene.connectors[0].action_circle.center,
            scene.connectors[0].destination_leg.outer);
}

TEST(CanvasNodeDragTest, EndpointMovesPreserveValidCustomizedInteriorSegments) {
  DragFixture          fixture;
  constexpr std::array custom_points{
      graphscore::RoutePoint{400.0, 112.0},
      graphscore::RoutePoint{400.0, 350.0},
      graphscore::RoutePoint{450.0, 350.0},
      graphscore::RoutePoint{450.0, 212.0},
  };
  ASSERT_TRUE(fixture.project.find_node(fixture.source_id)
                  ->find_output(fixture.output)
                  ->route()
                  .set_custom_route(std::vector<graphscore::RoutePoint>(
                      custom_points.begin(), custom_points.end()))
                  .ok());
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.source_id, {0.0, 0.0}));
  ASSERT_TRUE(drag.update({0.0, 50.0}));
  const std::array expected_source_move{
      graphscore::GraphPosition{400.0, 112.0},
      graphscore::GraphPosition{400.0, 350.0},
      graphscore::GraphPosition{450.0, 350.0},
      graphscore::GraphPosition{450.0, 212.0},
  };
  EXPECT_FALSE(std::ranges::search(scene.connectors[0].route_points,
                                   expected_source_move)
                   .empty());
  drag.cancel();

  ASSERT_TRUE(drag.begin(fixture.destination_id, {500.0, 100.0}));
  ASSERT_TRUE(drag.update({500.0, 150.0}));
  EXPECT_FALSE(std::ranges::search(scene.connectors[0].route_points,
                                   expected_source_move)
                   .empty());
}

TEST(CanvasNodeDragTest, EndpointMoveRepairsOnlyCollidingCustomizedSegments) {
  DragFixture fixture;
  fixture.project.find_node(fixture.unrelated_source_id)
      ->set_position({0.0, 2000.0});
  fixture.project.find_node(fixture.unrelated_destination_id)
      ->set_position({500.0, 2000.0});
  constexpr std::array custom_points{
      graphscore::RoutePoint{250.0, 250.0},
      graphscore::RoutePoint{400.0, 250.0},
      graphscore::RoutePoint{400.0, 1000.0},
      graphscore::RoutePoint{700.0, 1000.0},
      graphscore::RoutePoint{700.0, 212.0},
  };
  const std::vector stored_route(custom_points.begin(), custom_points.end());
  ASSERT_TRUE(fixture.project.find_node(fixture.source_id)
                  ->find_output(fixture.output)
                  ->route()
                  .set_custom_route(stored_route)
                  .ok());
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasNodeDragController drag(fixture.project, fixture.history,
                                            scene);

  ASSERT_TRUE(drag.begin(fixture.destination_id, {500.0, 100.0}));
  ASSERT_TRUE(drag.update({300.0, 300.0}));

  constexpr std::array preserved_source_segment{
      graphscore::GraphPosition{250.0, 250.0},
      graphscore::GraphPosition{400.0, 250.0},
  };
  constexpr std::array preserved_destination_segment{
      graphscore::GraphPosition{400.0, 1000.0},
      graphscore::GraphPosition{700.0, 1000.0},
      graphscore::GraphPosition{700.0, 212.0},
  };
  EXPECT_FALSE(std::ranges::search(scene.connectors[0].route_points,
                                   preserved_source_segment)
                   .empty());
  EXPECT_FALSE(std::ranges::search(scene.connectors[0].route_points,
                                   preserved_destination_segment)
                   .empty());
  EXPECT_EQ(fixture.project.find_node(fixture.source_id)
                ->find_output(fixture.output)
                ->route()
                .waypoints(),
            stored_route);
  const graphscore::WorldBounds moved_bounds = scene.nodes[1].geometry.bounds;
  const double clearance = graphscore::CanvasConnectorGeometry::kCornerRadius;
  const graphscore::WorldBounds expanded_bounds{
      {moved_bounds.origin.x - clearance, moved_bounds.origin.y - clearance},
      moved_bounds.width + clearance * 2.0,
      moved_bounds.height + clearance * 2.0,
  };
  for (std::size_t index = 2U;
       index + 1U < scene.connectors[0].route_points.size(); ++index) {
    const graphscore::GraphPosition first =
        scene.connectors[0].route_points[index - 1U];
    const graphscore::GraphPosition second =
        scene.connectors[0].route_points[index];
    EXPECT_TRUE(std::isfinite(first.x));
    EXPECT_TRUE(std::isfinite(first.y));
    EXPECT_NE(first, second);
    EXPECT_TRUE(first.x == second.x || first.y == second.y);
    EXPECT_FALSE(segment_crosses_interior(first, second, expanded_bounds));
  }
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
            (graphscore::GraphPosition{350.0, 96.0}));
  EXPECT_EQ(scene.connectors[1].source_leg.attachment,
            (graphscore::GraphPosition{350.0, 152.0}));
  EXPECT_EQ(scene.connectors[2].source_leg.attachment,
            (graphscore::GraphPosition{350.0, 208.0}));
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
