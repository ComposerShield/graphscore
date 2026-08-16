// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

using graphscore::CanvasNavigationController;
using graphscore::GraphPosition;
using graphscore::ViewportPosition;
using graphscore::ViewportTransform;

TEST(CanvasNavigationControllerTest, WheelPanPreservesBothHighResolutionAxes) {
  ViewportTransform          transform;
  CanvasNavigationController navigation(transform);

  ASSERT_TRUE(navigation.wheel_pan({1.25, -2.75}));
  EXPECT_EQ(transform.viewport_anchor(), (ViewportPosition{1.25, -2.75}));
  EXPECT_EQ(transform.zoom(), 1.0);
}

TEST(CanvasNavigationControllerTest, WheelZoomPreservesPointerFocalPoint) {
  ViewportTransform          transform;
  CanvasNavigationController navigation(transform);
  constexpr ViewportPosition kPointer{123.5, 87.25};
  const auto                 before = transform.to_world(kPointer);
  ASSERT_TRUE(before);

  ASSERT_TRUE(navigation.wheel_zoom(2.5, kPointer));

  EXPECT_DOUBLE_EQ(transform.zoom(),
                   std::pow(graphscore::kWheelZoomStepPerUnit, 2.5));
  EXPECT_EQ(transform.to_world(kPointer), before);
}

TEST(CanvasNavigationControllerTest, KeyboardStepsAreFixedAndMultiplicative) {
  ViewportTransform          transform;
  CanvasNavigationController navigation(transform);
  constexpr ViewportPosition kFocal{200.0, 100.0};

  ASSERT_TRUE(navigation.pan(
      {graphscore::kKeyboardPanStep, -graphscore::kKeyboardPanStep}));
  EXPECT_EQ(transform.viewport_anchor(),
            (ViewportPosition{graphscore::kKeyboardPanStep,
                              -graphscore::kKeyboardPanStep}));
  const auto before = transform.to_world(kFocal);
  ASSERT_TRUE(before);
  ASSERT_TRUE(navigation.zoom_in(kFocal));
  EXPECT_DOUBLE_EQ(transform.zoom(), graphscore::kKeyboardZoomStep);
  EXPECT_EQ(transform.to_world(kFocal), before);
  ASSERT_TRUE(navigation.zoom_out(kFocal));
  EXPECT_DOUBLE_EQ(transform.zoom(), 1.0);
  EXPECT_EQ(transform.to_world(kFocal), before);
}

TEST(CanvasNavigationControllerTest, RejectsInvalidAndExtremeInputAtomically) {
  ViewportTransform          transform;
  CanvasNavigationController navigation(transform);
  ASSERT_TRUE(transform.set_anchor({0.0, 0.0}, {10.0, 20.0}));
  const GraphPosition    world_before    = transform.world_anchor();
  const ViewportPosition viewport_before = transform.viewport_anchor();
  const double           zoom_before     = transform.zoom();
  const double           infinity = std::numeric_limits<double>::infinity();

  EXPECT_FALSE(navigation.wheel_pan({infinity, 0.0}));
  EXPECT_FALSE(navigation.wheel_zoom(infinity, {0.0, 0.0}));
  EXPECT_FALSE(navigation.wheel_zoom(1.0, {infinity, 0.0}));
  EXPECT_EQ(transform.world_anchor(), world_before);
  EXPECT_EQ(transform.viewport_anchor(), viewport_before);
  EXPECT_EQ(transform.zoom(), zoom_before);
}

}  // namespace
