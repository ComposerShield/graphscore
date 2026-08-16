// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

using graphscore::FingerContact;
using graphscore::GraphPosition;
using graphscore::PinchUpdate;
using graphscore::ScrollDelta;
using graphscore::TrackpadGestureController;
using graphscore::ViewportPosition;
using graphscore::ViewportTransform;

TEST(TrackpadGestureControllerTest, PanChangesTranslationNotZoom) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  const double              zoom_before = transform.zoom();

  ASSERT_TRUE(controller.pan(ScrollDelta{15.25, -7.5}));

  EXPECT_EQ(transform.zoom(), zoom_before);
  EXPECT_EQ(transform.viewport_anchor(), (ViewportPosition{15.25, -7.5}));
  EXPECT_EQ(transform.world_anchor(), (GraphPosition{0.0, 0.0}));
}

TEST(TrackpadGestureControllerTest, PinchAppliesScaleExactlyOnce) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);

  // Window-center fallback (identity {0,0}) applies each update once, never
  // compounding a single update into more than one zoom step.
  ASSERT_TRUE(controller.pinch(PinchUpdate{1.5, std::nullopt}));
  EXPECT_EQ(transform.zoom(), 1.5);
  ASSERT_TRUE(controller.pinch(PinchUpdate{2.0, std::nullopt}));
  EXPECT_EQ(transform.zoom(), 3.0);
}

TEST(TrackpadGestureControllerTest, PinchFocalPreservesWorldPoint) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  ASSERT_TRUE(transform.set_anchor({10.0, 20.0}, {100.0, 200.0}));
  ASSERT_TRUE(transform.zoom_to(2.0, {150.0, 250.0}));

  const ViewportPosition focal{120.0, 180.0};
  const auto             focal_world = transform.to_world(focal);
  ASSERT_TRUE(focal_world);

  ASSERT_TRUE(controller.pinch(PinchUpdate{3.0, focal}));

  EXPECT_EQ(transform.zoom(), 6.0);
  const auto after = transform.to_world(focal);
  ASSERT_TRUE(after);
  EXPECT_EQ(*after, *focal_world);
}

TEST(TrackpadGestureControllerTest, PinchUsesActiveCentroidWhenNoEventFocal) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  controller.set_window_center({500.0, 300.0});

  ASSERT_TRUE(controller.finger_down(FingerContact{1, {100.0, 200.0}}));
  ASSERT_TRUE(controller.finger_down(FingerContact{2, {200.0, 300.0}}));
  const auto centroid = controller.active_centroid();
  ASSERT_TRUE(centroid);
  EXPECT_EQ(*centroid, (ViewportPosition{150.0, 250.0}));

  const auto focal_world = transform.to_world(*centroid);
  ASSERT_TRUE(focal_world);
  ASSERT_TRUE(controller.pinch(PinchUpdate{2.0, std::nullopt}));

  EXPECT_EQ(transform.zoom(), 2.0);
  const auto after = transform.to_world(*centroid);
  ASSERT_TRUE(after);
  EXPECT_EQ(*after, *focal_world);
}

TEST(TrackpadGestureControllerTest, PinchUsesWindowCenterFallback) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  controller.set_window_center({640.0, 360.0});

  const auto center_world = transform.to_world({640.0, 360.0});
  ASSERT_TRUE(center_world);
  ASSERT_TRUE(controller.pinch(PinchUpdate{2.0, std::nullopt}));

  EXPECT_EQ(transform.zoom(), 2.0);
  const auto after = transform.to_world({640.0, 360.0});
  ASSERT_TRUE(after);
  EXPECT_EQ(*after, *center_world);
}

TEST(TrackpadGestureControllerTest, EventFocalOverridesCentroid) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  ASSERT_TRUE(controller.finger_down(FingerContact{1, {100.0, 100.0}}));
  ASSERT_TRUE(controller.finger_down(FingerContact{2, {200.0, 200.0}}));

  const ViewportPosition focal{10.0, 20.0};
  const auto             focal_world = transform.to_world(focal);
  ASSERT_TRUE(focal_world);
  ASSERT_TRUE(controller.pinch(PinchUpdate{2.0, focal}));

  EXPECT_EQ(transform.zoom(), 2.0);
  const auto after = transform.to_world(focal);
  ASSERT_TRUE(after);
  EXPECT_EQ(*after, *focal_world);

  // The centroid point (which would have been used had the event focal been
  // absent) must NOT be preserved — the event focal won.
  const auto centroid_after = transform.to_world({150.0, 150.0});
  ASSERT_TRUE(centroid_after);
  EXPECT_NE(*centroid_after, (GraphPosition{150.0, 150.0}));
}

TEST(TrackpadGestureControllerTest, RejectsNonFiniteInputWithoutMutation) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  ASSERT_TRUE(transform.set_anchor({1.0, 2.0}, {10.0, 20.0}));
  const GraphPosition    world_anchor    = transform.world_anchor();
  const ViewportPosition viewport_anchor = transform.viewport_anchor();
  const double           zoom            = transform.zoom();
  const double           nan      = std::numeric_limits<double>::quiet_NaN();
  const double           infinity = std::numeric_limits<double>::infinity();

  EXPECT_FALSE(controller.pan(ScrollDelta{nan, 1.0}));
  EXPECT_FALSE(controller.pan(ScrollDelta{1.0, infinity}));
  EXPECT_FALSE(controller.pinch(PinchUpdate{nan, ViewportPosition{0.0, 0.0}}));
  EXPECT_FALSE(
      controller.pinch(PinchUpdate{infinity, ViewportPosition{0.0, 0.0}}));
  EXPECT_FALSE(controller.pinch(PinchUpdate{0.0, ViewportPosition{0.0, 0.0}}));
  EXPECT_FALSE(controller.pinch(PinchUpdate{-2.0, ViewportPosition{0.0, 0.0}}));
  EXPECT_FALSE(controller.pinch(PinchUpdate{2.0, ViewportPosition{nan, 0.0}}));
  EXPECT_FALSE(controller.finger_down(FingerContact{1, {nan, 0.0}}));
  EXPECT_FALSE(controller.finger_move(FingerContact{1, {0.0, infinity}}));

  EXPECT_EQ(transform.world_anchor(), world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_EQ(transform.zoom(), zoom);
  EXPECT_EQ(controller.tracked_finger_count(), 0);
}

TEST(TrackpadGestureControllerTest, FingerUpAndCancelEndTrackingWithoutMotion) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  ASSERT_TRUE(transform.set_anchor({5.0, 6.0}, {50.0, 60.0}));
  const GraphPosition    world_anchor    = transform.world_anchor();
  const ViewportPosition viewport_anchor = transform.viewport_anchor();
  const double           zoom            = transform.zoom();

  ASSERT_TRUE(controller.finger_down(FingerContact{1, {10.0, 10.0}}));
  ASSERT_TRUE(controller.finger_down(FingerContact{2, {20.0, 20.0}}));
  EXPECT_EQ(controller.tracked_finger_count(), 2);
  EXPECT_TRUE(controller.active_centroid().has_value());

  controller.finger_up(1);
  EXPECT_EQ(controller.tracked_finger_count(), 1);
  EXPECT_FALSE(controller.active_centroid().has_value());

  // Moving an untracked finger is a no-op, never a re-track.
  EXPECT_FALSE(controller.finger_move(FingerContact{1, {30.0, 30.0}}));

  controller.cancel_tracking();
  EXPECT_EQ(controller.tracked_finger_count(), 0);

  EXPECT_EQ(transform.world_anchor(), world_anchor);
  EXPECT_EQ(transform.viewport_anchor(), viewport_anchor);
  EXPECT_EQ(transform.zoom(), zoom);
}

TEST(TrackpadGestureControllerTest, ThirdFingerIsIgnored) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);

  ASSERT_TRUE(controller.finger_down(FingerContact{1, {10.0, 10.0}}));
  ASSERT_TRUE(controller.finger_down(FingerContact{2, {20.0, 20.0}}));
  EXPECT_FALSE(controller.finger_down(FingerContact{3, {30.0, 30.0}}));
  EXPECT_EQ(controller.tracked_finger_count(), 2);
  EXPECT_EQ(*controller.active_centroid(), (ViewportPosition{15.0, 15.0}));
}

TEST(TrackpadGestureControllerTest, DefaultWindowCenterIsOrigin) {
  ViewportTransform         transform;
  TrackpadGestureController controller(transform);
  EXPECT_EQ(controller.window_center(), (ViewportPosition{0.0, 0.0}));
}

}  // namespace
