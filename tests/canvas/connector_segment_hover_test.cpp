// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace {

constexpr std::array kRoute{
    graphscore::GraphPosition{0.0, 10.0},
    graphscore::GraphPosition{40.0, 10.0},
    graphscore::GraphPosition{40.0, 50.0},
};

TEST(CanvasConnectorSegmentHoverTest,
     HorizontalSegmentPresentsNorthSouthResizeCursor) {
  EXPECT_EQ(
      graphscore::canvas_connector_segment_hover(kRoute, {20.0, 12.0}, 3.0),
      (graphscore::CanvasConnectorSegmentHover{
          0U, graphscore::CanvasCursorShape::kResizeNorthSouth}));
}

TEST(CanvasConnectorSegmentHoverTest,
     VerticalSegmentPresentsEastWestResizeCursor) {
  EXPECT_EQ(
      graphscore::canvas_connector_segment_hover(kRoute, {38.0, 30.0}, 3.0),
      (graphscore::CanvasConnectorSegmentHover{
          1U, graphscore::CanvasCursorShape::kResizeEastWest}));
}

TEST(CanvasConnectorSegmentHoverTest, UsesInclusiveToleranceAndSegmentBounds) {
  EXPECT_TRUE(
      graphscore::canvas_connector_segment_hover(kRoute, {20.0, 13.0}, 3.0)
          .has_value());
  EXPECT_FALSE(
      graphscore::canvas_connector_segment_hover(kRoute, {20.0, 13.01}, 3.0)
          .has_value());
  EXPECT_FALSE(
      graphscore::canvas_connector_segment_hover(kRoute, {-0.01, 10.0}, 3.0)
          .has_value());
}

TEST(CanvasConnectorSegmentHoverTest, ChoosesNearestThenRouteOrder) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{20.0, 0.0},
      graphscore::GraphPosition{20.0, 20.0},
  };

  EXPECT_EQ(graphscore::canvas_connector_segment_hover(route, {18.0, 1.0}, 3.0),
            (graphscore::CanvasConnectorSegmentHover{
                0U, graphscore::CanvasCursorShape::kResizeNorthSouth}));
  EXPECT_EQ(graphscore::canvas_connector_segment_hover(route, {19.0, 1.0}, 3.0),
            (graphscore::CanvasConnectorSegmentHover{
                0U, graphscore::CanvasCursorShape::kResizeNorthSouth}));
}

TEST(CanvasConnectorSegmentHoverTest, IgnoresMalformedSegments) {
  constexpr std::array route{
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{0.0, 0.0},
      graphscore::GraphPosition{10.0, 10.0},
  };

  EXPECT_FALSE(
      graphscore::canvas_connector_segment_hover(route, {0.0, 0.0}, 2.0)
          .has_value());
  EXPECT_FALSE(graphscore::canvas_connector_segment_hover(
                   kRoute, {std::numeric_limits<double>::infinity(), 10.0}, 2.0)
                   .has_value());
  EXPECT_FALSE(
      graphscore::canvas_connector_segment_hover(
          kRoute, {20.0, 10.0}, std::numeric_limits<double>::quiet_NaN())
          .has_value());
  EXPECT_FALSE(
      graphscore::canvas_connector_segment_hover(kRoute, {20.0, 10.0}, -1.0)
          .has_value());
}

TEST(CanvasConnectorSegmentHoverTest, EmptyAndSinglePointRoutesHaveNoHover) {
  constexpr std::array<graphscore::GraphPosition, 0> empty{};
  constexpr std::array one_point{graphscore::GraphPosition{0.0, 0.0}};

  EXPECT_FALSE(
      graphscore::canvas_connector_segment_hover(empty, {}, 2.0).has_value());
  EXPECT_FALSE(graphscore::canvas_connector_segment_hover(one_point, {}, 2.0)
                   .has_value());
}

}  // namespace
