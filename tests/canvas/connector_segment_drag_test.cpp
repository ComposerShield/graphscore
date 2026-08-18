// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <vector>

namespace {

class DragMetrics final : public graphscore::GlyphMetrics {
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

TEST(CanvasConnectorSegmentDragTest, StraightSegmentInsertsParallelBends) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 10.0},
      graphscore::GraphPosition{24.0, 10.0},
      graphscore::GraphPosition{200.0, 10.0},
      graphscore::GraphPosition{224.0, 10.0},
  };

  const auto dragged =
      graphscore::canvas_connector_drag_segment(route, 1U, {0.0, 50.0});

  ASSERT_TRUE(dragged.has_value());
  EXPECT_EQ(*dragged, (std::vector<graphscore::GraphPosition>{{0.0, 10.0},
                                                              {24.0, 10.0},
                                                              {24.0, 50.0},
                                                              {200.0, 50.0},
                                                              {200.0, 10.0},
                                                              {224.0, 10.0}}));
}

TEST(CanvasConnectorSegmentDragTest, EndpointClearancePointsNeverMove) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{24.0, 0.0},
      graphscore::GraphPosition{24.0, 80.0},
      graphscore::GraphPosition{100.0, 80.0},
      graphscore::GraphPosition{100.0, 0.0},
      graphscore::GraphPosition{124.0, 0.0},
  };

  const auto dragged =
      graphscore::canvas_connector_drag_segment(route, 1U, {0.0, 40.0});

  ASSERT_TRUE(dragged.has_value());
  EXPECT_EQ(dragged->at(1U), route[1U]);
  EXPECT_EQ(dragged->at(dragged->size() - 2U), route[route.size() - 2U]);
  for (std::size_t index = 1U; index < dragged->size(); ++index) {
    EXPECT_NE((*dragged)[index - 1U], (*dragged)[index]);
    EXPECT_TRUE((*dragged)[index - 1U].x == (*dragged)[index].x ||
                (*dragged)[index - 1U].y == (*dragged)[index].y);
  }

  const auto unchanged =
      graphscore::canvas_connector_drag_segment(route, 0U, {0.0, 0.0});
  ASSERT_TRUE(unchanged.has_value());
  EXPECT_EQ(*unchanged,
            std::vector<graphscore::GraphPosition>(route.begin(), route.end()));
}

TEST(CanvasConnectorSegmentDragTest, AlignedDragRemovesRedundantBends) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{24.0, 0.0},
      graphscore::GraphPosition{24.0, 40.0},
      graphscore::GraphPosition{100.0, 40.0},
      graphscore::GraphPosition{100.0, 80.0},
      graphscore::GraphPosition{200.0, 80.0},
      graphscore::GraphPosition{224.0, 80.0},
  };

  const auto dragged =
      graphscore::canvas_connector_drag_segment(route, 2U, {0.0, 80.0});

  ASSERT_TRUE(dragged.has_value());
  EXPECT_EQ(dragged->size(), 5U);
  EXPECT_EQ((*dragged)[1U], route[1U]);
  EXPECT_EQ((*dragged)[dragged->size() - 2U], route[route.size() - 2U]);
}

TEST(CanvasConnectorSegmentDragTest, EndpointSegmentPreservesTheFollowingBend) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{24.0, 0.0},
      graphscore::GraphPosition{24.0, 40.0},
      graphscore::GraphPosition{100.0, 40.0},
      graphscore::GraphPosition{124.0, 40.0},
  };

  const auto dragged =
      graphscore::canvas_connector_drag_segment(route, 0U, {0.0, 20.0});

  ASSERT_TRUE(dragged.has_value());
  EXPECT_NE(*dragged,
            std::vector<graphscore::GraphPosition>(route.begin(), route.end()));
  EXPECT_EQ(dragged->at(1U), route[1U]);
  EXPECT_EQ(dragged->back(), route.back());
  for (std::size_t index = 1U; index < dragged->size(); ++index) {
    EXPECT_NE((*dragged)[index - 1U], (*dragged)[index]);
    EXPECT_TRUE((*dragged)[index - 1U].x == (*dragged)[index].x ||
                (*dragged)[index - 1U].y == (*dragged)[index].y);
  }
}

TEST(CanvasConnectorSegmentDragTest, DestinationEndpointPreservesThePriorBend) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{24.0, 0.0},
      graphscore::GraphPosition{24.0, 40.0},
      graphscore::GraphPosition{100.0, 40.0},
      graphscore::GraphPosition{100.0, 0.0},
      graphscore::GraphPosition{124.0, 0.0},
  };

  const auto dragged =
      graphscore::canvas_connector_drag_segment(route, 4U, {120.0, 20.0});

  ASSERT_TRUE(dragged.has_value());
  EXPECT_NE(*dragged,
            std::vector<graphscore::GraphPosition>(route.begin(), route.end()));
  EXPECT_EQ(dragged->at(1U), route[1U]);
  EXPECT_EQ(dragged->at(dragged->size() - 2U), route[route.size() - 2U]);
  EXPECT_EQ(dragged->back(), route.back());
  for (std::size_t index = 1U; index < dragged->size(); ++index) {
    EXPECT_NE((*dragged)[index - 1U], (*dragged)[index]);
    EXPECT_TRUE((*dragged)[index - 1U].x == (*dragged)[index].x ||
                (*dragged)[index - 1U].y == (*dragged)[index].y);
  }
}

TEST(CanvasConnectorSegmentDragTest, CoincidentEndpointClearanceCanBeDragged) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{24.0, 0.0},
      graphscore::GraphPosition{100.0, 0.0},
  };

  const auto dragged =
      graphscore::canvas_connector_drag_segment(route, 0U, {0.0, 40.0});

  ASSERT_TRUE(dragged.has_value());
  ASSERT_GE(dragged->size(), 3U);
  EXPECT_EQ(dragged->front(), route.front());
  EXPECT_EQ((*dragged)[1U], route[1U]);
  EXPECT_EQ((*dragged)[dragged->size() - 2U], route[1U]);
  EXPECT_EQ(dragged->back(), route.back());
  for (std::size_t index = 1U; index < dragged->size(); ++index) {
    EXPECT_NE((*dragged)[index - 1U], (*dragged)[index]);
    EXPECT_TRUE((*dragged)[index - 1U].x == (*dragged)[index].x ||
                (*dragged)[index - 1U].y == (*dragged)[index].y);
  }
}

TEST(CanvasConnectorSegmentDragTest, RejectsMalformedAndNonFiniteInput) {
  constexpr std::array diagonal{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{24.0, 0.0},
      graphscore::GraphPosition{50.0, 20.0},
      graphscore::GraphPosition{74.0, 20.0},
  };
  constexpr std::array vertical{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{24.0, 0.0},
      graphscore::GraphPosition{24.0, 60.0},
      graphscore::GraphPosition{100.0, 60.0},
      graphscore::GraphPosition{100.0, 0.0},
      graphscore::GraphPosition{124.0, 0.0},
  };

  EXPECT_FALSE(
      graphscore::canvas_connector_drag_segment(diagonal, 1U, {30.0, 10.0})
          .has_value());
  EXPECT_FALSE(
      graphscore::canvas_connector_drag_segment(
          vertical, 1U, {std::numeric_limits<double>::infinity(), 20.0})
          .has_value());

  const auto moved =
      graphscore::canvas_connector_drag_segment(vertical, 1U, {50.0, 20.0});
  ASSERT_TRUE(moved.has_value());
  EXPECT_EQ(moved->at(1U), vertical[1U]);
  EXPECT_EQ(moved->at(2U).x, 50.0);
}

TEST(CanvasConnectorSegmentDragTest,
     CompleteGestureCommitsOnlyItsFinalRouteAsOneUndoableEdit) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Drag"};
  const graphscore::NodeId source           = project.add_node("Source");
  const graphscore::NodeId destination      = project.add_node("Destination");
  graphscore::Node* const  source_node      = project.find_node(source);
  graphscore::Node* const  destination_node = project.find_node(destination);
  source_node->set_position({0.0, 0.0});
  destination_node->set_position({600.0, 0.0});
  const graphscore::ConnectorId output = source_node->add_output("Out");
  const graphscore::ConnectorId input  = destination_node->add_input("In");
  ASSERT_TRUE(graphscore::Graph(project)
                  .connect(source, output, destination, input)
                  .ok());

  DragMetrics                     metrics;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(project, metrics);
  ASSERT_EQ(scene.connectors.size(), 1U);
  ASSERT_EQ(scene.connectors[0].route_points.size(), 4U);
  const double original_y = scene.connectors[0].route_points[1U].y;
  graphscore::CommandHistory                       history;
  graphscore::CanvasConnectorSegmentDragController drag(project, history,
                                                        scene);

  ASSERT_TRUE(drag.begin(source, output, 1U, {100.0, original_y}));
  ASSERT_TRUE(drag.update({100.0, original_y + 40.0}));
  EXPECT_TRUE(
      project.find_node(source)->find_output(output)->route().is_automatic());
  EXPECT_EQ(history.undo_stack_size(), 0U);
  ASSERT_TRUE(drag.update({100.0, original_y + 140.0}));
  EXPECT_TRUE(
      project.find_node(source)->find_output(output)->route().is_automatic());
  EXPECT_EQ(history.undo_stack_size(), 0U);
  ASSERT_TRUE(drag.update({100.0, original_y + 100.0}));
  const std::vector final_route_points = scene.connectors[0].route_points;
  ASSERT_TRUE(drag.finish().ok());
  const graphscore::RouteGeometry final_route =
      project.find_node(source)->find_output(output)->route();
  EXPECT_FALSE(final_route.is_automatic());
  EXPECT_EQ(history.undo_stack_size(), 1U);
  EXPECT_EQ(scene.connectors[0].route_points.size(), 6U);
  EXPECT_EQ(scene.connectors[0].route_points, final_route_points);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_TRUE(
      project.find_node(source)->find_output(output)->route().is_automatic());
  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(source)->find_output(output)->route(),
            final_route);

  const std::size_t history_size = history.undo_stack_size();
  graphscore::CanvasConnectorSegmentDragController no_op(project, history,
                                                         scene);
  ASSERT_TRUE(
      no_op.begin(source, output, 1U, scene.connectors[0].route_points[1U]));
  ASSERT_TRUE(no_op.finish().ok());
  EXPECT_EQ(history.undo_stack_size(), history_size);
}

TEST(CanvasConnectorSegmentDragTest, RejectsObstacleMoveDuringGesture) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Stale"};
  const graphscore::NodeId source           = project.add_node("Source");
  const graphscore::NodeId destination      = project.add_node("Destination");
  const graphscore::NodeId obstacle         = project.add_node("Obstacle");
  graphscore::Node* const  source_node      = project.find_node(source);
  graphscore::Node* const  destination_node = project.find_node(destination);
  source_node->set_position({0.0, 0.0});
  destination_node->set_position({750.0, 0.0});
  project.find_node(obstacle)->set_position({375.0, 0.0});
  const graphscore::ConnectorId output = source_node->add_output("Out");
  const graphscore::ConnectorId input  = destination_node->add_input("In");
  ASSERT_TRUE(graphscore::Graph(project)
                  .connect(source, output, destination, input)
                  .ok());
  DragMetrics                     metrics;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(project, metrics);
  ASSERT_EQ(scene.connectors.size(), 1U);
  const graphscore::CanvasConnectorGeometry        before = scene.connectors[0];
  graphscore::CommandHistory                       history;
  graphscore::CanvasConnectorSegmentDragController drag(project, history,
                                                        scene);

  ASSERT_TRUE(
      drag.begin(source, output, 1U, scene.connectors[0].route_points[1U]));
  ASSERT_TRUE(drag.update({scene.connectors[0].route_points[1U].x,
                           scene.connectors[0].route_points[1U].y + 40.0}));
  project.find_node(obstacle)->set_position({375.0, 300.0});

  EXPECT_EQ(drag.finish().code(), graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(scene.connectors[0], before);
  EXPECT_EQ(history.undo_stack_size(), 0U);
  EXPECT_TRUE(source_node->find_output(output)->route().is_automatic());
}

TEST(CanvasConnectorSegmentDragTest,
     RejectsEndpointLegMutationAndRestoresScene) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Stale leg"};
  const graphscore::NodeId source           = project.add_node("Source");
  const graphscore::NodeId destination      = project.add_node("Destination");
  graphscore::Node* const  source_node      = project.find_node(source);
  graphscore::Node* const  destination_node = project.find_node(destination);
  source_node->set_position({0.0, 0.0});
  destination_node->set_position({600.0, 0.0});
  const graphscore::ConnectorId output = source_node->add_output("Out");
  const graphscore::ConnectorId input  = destination_node->add_input("In");
  ASSERT_TRUE(graphscore::Graph(project)
                  .connect(source, output, destination, input)
                  .ok());
  DragMetrics                     metrics;
  graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(project, metrics);
  const graphscore::CanvasConnectorGeometry        before = scene.connectors[0];
  graphscore::CommandHistory                       history;
  graphscore::CanvasConnectorSegmentDragController drag(project, history,
                                                        scene);

  ASSERT_TRUE(
      drag.begin(source, output, 1U, scene.connectors[0].route_points[1U]));
  ASSERT_TRUE(drag.update({scene.connectors[0].route_points[1U].x,
                           scene.connectors[0].route_points[1U].y + 40.0}));
  scene.connectors[0].source_leg.outer.x += 1.0;

  EXPECT_EQ(drag.finish().code(), graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(scene.connectors[0], before);
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

}  // namespace
