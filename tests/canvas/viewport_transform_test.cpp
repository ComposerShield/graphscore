// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

using graphscore::GraphPosition;
using graphscore::ViewportPosition;
using graphscore::ViewportTransform;

TEST(ViewportTransformTest, DefaultsToIdentity) {
  const ViewportTransform transform;

  const auto viewport = transform.to_viewport({12.5, -9.25});
  ASSERT_TRUE(viewport);
  EXPECT_EQ(*viewport, (ViewportPosition{12.5, -9.25}));

  const auto world = transform.to_world(*viewport);
  ASSERT_TRUE(world);
  EXPECT_EQ(*world, (GraphPosition{12.5, -9.25}));
}

TEST(ViewportTransformTest, MapsVeryLargeFiniteCoordinatesWithoutPageLimit) {
  ViewportTransform transform;
  constexpr double  kLarge = 1.0e200;

  const auto positive = transform.to_viewport({kLarge, -kLarge});
  ASSERT_TRUE(positive);
  EXPECT_EQ(*positive, (ViewportPosition{kLarge, -kLarge}));

  const auto negative = transform.to_viewport({-kLarge, kLarge});
  ASSERT_TRUE(negative);
  EXPECT_EQ(*negative, (ViewportPosition{-kLarge, kLarge}));
}

TEST(ViewportTransformTest, AnchoringPreservesPrecisionNearExtremeCoordinates) {
  ViewportTransform   transform;
  constexpr double    kLarge = 1.0e150;
  const GraphPosition anchor{kLarge, -kLarge};
  ASSERT_TRUE(transform.set_anchor(anchor, {640.0, 360.0}));

  double nearby_x = anchor.x;
  double nearby_y = anchor.y;
  for (int step = 0; step < 32; ++step) {
    nearby_x =
        std::nextafter(nearby_x, std::numeric_limits<double>::infinity());
    nearby_y =
        std::nextafter(nearby_y, -std::numeric_limits<double>::infinity());
  }
  const GraphPosition nearby{nearby_x, nearby_y};
  const auto          viewport = transform.to_viewport(nearby);
  ASSERT_TRUE(viewport);
  const auto recovered = transform.to_world(*viewport);
  ASSERT_TRUE(recovered);

  const double x_tolerance =
      std::nextafter(nearby.x, std::numeric_limits<double>::infinity()) -
      nearby.x;
  const double y_tolerance =
      nearby.y -
      std::nextafter(nearby.y, -std::numeric_limits<double>::infinity());
  EXPECT_NEAR(recovered->x, nearby.x, x_tolerance);
  EXPECT_NEAR(recovered->y, nearby.y, y_tolerance);
}

TEST(ViewportTransformTest, PanTranslatesViewportAndInverseConsistently) {
  ViewportTransform transform;
  ASSERT_TRUE(transform.pan_by({125.5, -80.25}));

  const auto viewport = transform.to_viewport({10.0, 20.0});
  ASSERT_TRUE(viewport);
  EXPECT_EQ(*viewport, (ViewportPosition{135.5, -60.25}));
  const auto world = transform.to_world(*viewport);
  ASSERT_TRUE(world);
  EXPECT_EQ(*world, (GraphPosition{10.0, 20.0}));
}

TEST(ViewportTransformTest, ZoomPreservesFocalWorldPointAtExtremeCoordinate) {
  ViewportTransform       transform;
  constexpr GraphPosition kAnchor{1.0e150, -1.0e150};
  ASSERT_TRUE(transform.set_anchor(kAnchor, {500.0, 300.0}));

  const GraphPosition focal_world{
      std::nextafter(kAnchor.x, std::numeric_limits<double>::infinity()),
      std::nextafter(kAnchor.y, -std::numeric_limits<double>::infinity())};
  const auto focal_viewport = transform.to_viewport(focal_world);
  ASSERT_TRUE(focal_viewport);
  ASSERT_TRUE(transform.zoom_by(4.0, *focal_viewport));

  EXPECT_EQ(transform.zoom(), 4.0);
  const auto after_zoom = transform.to_viewport(focal_world);
  ASSERT_TRUE(after_zoom);
  EXPECT_EQ(*after_zoom, *focal_viewport);
  const auto inverse = transform.to_world(*focal_viewport);
  ASSERT_TRUE(inverse);
  EXPECT_EQ(*inverse, focal_world);
}

TEST(ViewportTransformTest, RejectsInvalidOperationsWithoutMutation) {
  ViewportTransform transform;
  ASSERT_TRUE(transform.set_anchor({20.0, -30.0}, {100.0, 200.0}));
  ASSERT_TRUE(transform.zoom_to(2.0, {125.0, 225.0}));
  const GraphPosition    world_anchor    = transform.world_anchor();
  const ViewportPosition viewport_anchor = transform.viewport_anchor();
  const double           zoom            = transform.zoom();
  const double           infinity = std::numeric_limits<double>::infinity();
  const double           nan      = std::numeric_limits<double>::quiet_NaN();

  EXPECT_FALSE(transform.set_anchor({infinity, 0.0}, {0.0, 0.0}));
  EXPECT_FALSE(transform.pan_by({infinity, 0.0}));
  EXPECT_FALSE(transform.pan_by({nan, 0.0}));
  EXPECT_FALSE(transform.zoom_to(0.0, {0.0, 0.0}));
  EXPECT_FALSE(transform.zoom_to(-1.0, {0.0, 0.0}));
  EXPECT_FALSE(transform.zoom_to(infinity, {0.0, 0.0}));
  EXPECT_FALSE(transform.zoom_by(0.0, {0.0, 0.0}));
  EXPECT_FALSE(transform.zoom_by(infinity, {0.0, 0.0}));
  EXPECT_FALSE(
      transform.zoom_by(std::numeric_limits<double>::max(), {0.0, 0.0}));

  EXPECT_EQ(transform.world_anchor(), world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_EQ(transform.zoom(), zoom);
  EXPECT_FALSE(transform.to_viewport({nan, 0.0}));
  EXPECT_FALSE(transform.to_world({infinity, 0.0}));
}

TEST(ViewportTransformTest, RejectsArithmeticOverflowWithoutMutation) {
  ViewportTransform transform;
  constexpr double  kMaximum = std::numeric_limits<double>::max();
  ASSERT_TRUE(transform.set_anchor({0.0, 0.0}, {kMaximum, -kMaximum}));
  const ViewportPosition viewport_anchor = transform.viewport_anchor();

  EXPECT_FALSE(transform.pan_by({kMaximum, -kMaximum}));
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_FALSE(transform.to_viewport({kMaximum, -kMaximum}));
}

TEST(ViewportTransformTest,
     RejectsResolutionCollapsingExtremeOperationsWithoutMutation) {
  ViewportTransform transform;
  constexpr double  kLarge = 1.0e200;
  ASSERT_TRUE(transform.set_anchor({kLarge, 0.0}, {0.0, 0.0}));
  const GraphPosition    world_anchor    = transform.world_anchor();
  const ViewportPosition viewport_anchor = transform.viewport_anchor();
  const double           zoom            = transform.zoom();

  EXPECT_FALSE(transform.to_world({1.0, 0.0}));
  EXPECT_FALSE(transform.zoom_by(2.0, {1.0, 0.0}));
  EXPECT_EQ(transform.world_anchor(), world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_EQ(transform.zoom(), zoom);

  ASSERT_TRUE(transform.set_anchor({0.0, 0.0}, {kLarge, 0.0}));
  const GraphPosition    pan_world_anchor    = transform.world_anchor();
  const ViewportPosition pan_viewport_anchor = transform.viewport_anchor();
  EXPECT_FALSE(transform.pan_by({1.0, 1.0}));
  EXPECT_EQ(transform.world_anchor(), pan_world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), pan_viewport_anchor);

  EXPECT_FALSE(transform.to_viewport({1.0, 0.0}));
}

TEST(ViewportTransformTest, RejectsResolutionDestroyingUnderflow) {
  ViewportTransform transform;
  constexpr double  kSmallest = std::numeric_limits<double>::denorm_min();
  constexpr double  kLargest  = std::numeric_limits<double>::max();

  ASSERT_TRUE(transform.zoom_to(kSmallest, {0.0, 0.0}));
  EXPECT_FALSE(transform.to_viewport({0.5, 0.0}));

  ASSERT_TRUE(transform.zoom_to(kLargest, {0.0, 0.0}));
  EXPECT_FALSE(transform.to_world({kSmallest, 0.0}));
  EXPECT_TRUE(transform.to_world({0.0, 0.0}));
}

TEST(ViewportTransformTest, RejectsAbsorbedZoomFactorWithoutReanchoring) {
  ViewportTransform transform;
  constexpr double  kSmallest = std::numeric_limits<double>::denorm_min();
  ASSERT_TRUE(transform.zoom_to(kSmallest, {0.0, 0.0}));
  const GraphPosition    world_anchor    = transform.world_anchor();
  const ViewportPosition viewport_anchor = transform.viewport_anchor();
  const double           zoom            = transform.zoom();
  const double           factor =
      std::nextafter(1.0, std::numeric_limits<double>::infinity());

  EXPECT_FALSE(transform.zoom_by(factor, {100.0, 200.0}));
  EXPECT_EQ(transform.world_anchor(), world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_EQ(transform.zoom(), zoom);

  EXPECT_TRUE(transform.zoom_by(1.0, {100.0, 200.0}));
  EXPECT_EQ(transform.world_anchor(), world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_EQ(transform.zoom(), zoom);

  EXPECT_TRUE(transform.zoom_to(zoom, {100.0, 200.0}));
  EXPECT_EQ(transform.world_anchor(), world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_EQ(transform.zoom(), zoom);
}

TEST(ViewportTransformTest, RetainsCanvasVersionCompatibility) {
  EXPECT_EQ(graphscore::canvas_version(), 1);
}

}  // namespace
