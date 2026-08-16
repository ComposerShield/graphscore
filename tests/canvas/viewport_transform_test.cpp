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

// The writer's render pass maps the notation surface (world rect (0,0)-(W,H))
// through to_viewport on every frame. A pinch gesture sweeps zoom through a
// continuum of ordinary, non-power-of-two scales, and a pan shifts both
// anchors. The round-trip exactness check in map_forward/map_inverse must not
// reject any of these ordinary states: a nullopt here made the render pass
// skip the notation surface entirely, which is the observed intermediate-zoom
// flicker. This test sweeps representative incremental zoom values and
// combined pan offsets and asserts every surface corner maps (and
// inverse-maps) without collapse across the whole sweep.
TEST(ViewportTransformTest, OrdinaryZoomAndPanSweepNeverCollapses) {
  constexpr double kSurfaceWidth  = 800.0;
  constexpr double kSurfaceHeight = 600.0;
  constexpr double kWindowWidth   = 1280.0;
  constexpr double kWindowHeight  = 800.0;

  for (int zoom_step = -600; zoom_step <= 600; ++zoom_step) {
    // exp(step*0.005) sweeps roughly 0.05x .. 20x, the ordinary range a
    // physical two-finger pinch produces (and beyond), without entering the
    // resolution-collapse extremes the transform is meant to reject.
    const double zoom = std::exp(static_cast<double>(zoom_step) * 0.005);
    for (int pan_x = -3; pan_x <= 3; ++pan_x) {
      for (int pan_y = -3; pan_y <= 3; ++pan_y) {
        ViewportTransform transform;
        // A pinch anchored at the window center: world_anchor and
        // viewport_anchor both sit at the focal point, then a pan offsets the
        // viewport anchor away from it.
        const ViewportPosition focal{kWindowWidth / 2.0 + 37.0 * pan_x,
                                     kWindowHeight / 2.0 + 53.0 * pan_y};
        ASSERT_TRUE(transform.set_anchor(
            {focal.x, focal.y},
            {focal.x + 100.0 * pan_x, focal.y + 100.0 * pan_y}));
        ASSERT_TRUE(transform.zoom_to(zoom, focal));

        const GraphPosition corners[] = {
            {0.0, 0.0},
            {kSurfaceWidth, 0.0},
            {0.0, kSurfaceHeight},
            {kSurfaceWidth, kSurfaceHeight},
        };
        for (const GraphPosition corner : corners) {
          const auto viewport = transform.to_viewport(corner);
          ASSERT_TRUE(viewport)
              << "to_viewport collapsed at zoom=" << zoom << " pan=(" << pan_x
              << ',' << pan_y << ") corner=(" << corner.x << ',' << corner.y
              << ')';
          const auto world = transform.to_world(*viewport);
          ASSERT_TRUE(world) << "to_world collapsed at zoom=" << zoom
                             << " pan=(" << pan_x << ',' << pan_y << ')';
        }
      }
    }
  }
}

// A world anchor of 1e200 at scale 1 forward-maps both 0 and 1 to -1e200:
// the subtraction `value - input_anchor` absorbs `value`'s bits, so the
// inverse recovers both as 0. Zero round-trips faithfully (0 -> -1e200 -> 0)
// and must be accepted, but 1 (and 2) collapse onto the same image *away from*
// the output anchor 0 and must be rejected. This is the many-to-one collapse
// the round-trip criterion must not let a giant anchor's tolerance swallow.
TEST(ViewportTransformTest, RejectsForwardCollapseAwayFromOutputAnchor) {
  ViewportTransform transform;
  constexpr double  kLarge = 1.0e200;
  ASSERT_TRUE(transform.set_anchor({kLarge, 0.0}, {0.0, 0.0}));

  EXPECT_TRUE(transform.to_viewport({0.0, 0.0}).has_value());
  EXPECT_FALSE(transform.to_viewport({1.0, 0.0}).has_value());
  EXPECT_FALSE(transform.to_viewport({2.0, 0.0}).has_value());
}

// The inverse twin: a viewport anchor of 1e200 at scale 1 inverse-maps both 0
// and 1 to -1e200 world, recovering both as 0. Zero is faithful; 1 and 2 are
// collapsed and must be rejected.
TEST(ViewportTransformTest, RejectsInverseCollapseAwayFromOutputAnchor) {
  ViewportTransform transform;
  constexpr double  kLarge = 1.0e200;
  ASSERT_TRUE(transform.set_anchor({0.0, 0.0}, {kLarge, 0.0}));

  EXPECT_TRUE(transform.to_world({0.0, 0.0}).has_value());
  EXPECT_FALSE(transform.to_world({1.0, 0.0}).has_value());
  EXPECT_FALSE(transform.to_world({2.0, 0.0}).has_value());
}

// The reviewer's large-value many-to-one counterexample (forward direction):
// at input_anchor 2^622, scale 1, output_anchor 0 the values 2^600 and
// 2^600 + 2^568 forward-map to the same image — the subtraction absorbs the
// 2^568 term, which is below the rounding resolution of the ~2^622 result —
// so the inverse recovers both as 2^600. The second loses 2^20 ULPs and must
// be rejected, not admitted by a percentage tolerance (its relative error
// ~2.33e-10 sits inside a 1e-9 bound).
TEST(ViewportTransformTest, RejectsForwardCollapseOfNearbyLargeValues) {
  ViewportTransform transform;
  constexpr double  kAnchor = 0x1.0p+622;  // 2^622
  constexpr double  kValue  = 0x1.0p+600;  // 2^600
  constexpr double  kDelta  = 0x1.0p+568;  // 2^568
  ASSERT_TRUE(transform.set_anchor({kAnchor, 0.0}, {0.0, 0.0}));

  EXPECT_TRUE(transform.to_viewport({kValue, 0.0}).has_value());
  EXPECT_FALSE(transform.to_viewport({kValue + kDelta, 0.0}).has_value());
}

// The inverse twin of the large-value collapse: a viewport anchor of 2^622 at
// scale 1 inverse-maps both 2^600 and 2^600 + 2^568 to the same world image
// and recovers both as 2^600. The second is collapsed and must be rejected.
TEST(ViewportTransformTest, RejectsInverseCollapseOfNearbyLargeValues) {
  ViewportTransform transform;
  constexpr double  kAnchor = 0x1.0p+622;  // 2^622
  constexpr double  kValue  = 0x1.0p+600;  // 2^600
  constexpr double  kDelta  = 0x1.0p+568;  // 2^568
  ASSERT_TRUE(transform.set_anchor({0.0, 0.0}, {kAnchor, 0.0}));

  EXPECT_TRUE(transform.to_world({kValue, 0.0}).has_value());
  EXPECT_FALSE(transform.to_world({kValue + kDelta, 0.0}).has_value());
}

// The reviewer's zero-collapse counterexample (forward direction): at
// input_anchor 3*2^1000, scale 2^-100, output_anchor 2^954 the values 0 and
// -2^1000 forward-map to the same image (the fma rounds the 0.25-ULP
// difference into the shared result) and inverse-recover as -2^1000. The
// zero's recovery is a meaningful nonzero value and must be rejected; a floor
// scaled by the anchors would reach infinity (2^954 / 2^-100 overflows) and
// admit it.
TEST(ViewportTransformTest, RejectsForwardZeroRecoveredAsCollapsedLargeValue) {
  ViewportTransform transform;
  constexpr double  kInputAnchor  = 3.0 * 0x1.0p+1000;  // 3*2^1000
  constexpr double  kScale        = 0x1.0p-100;         // 2^-100
  constexpr double  kOutputAnchor = 0x1.0p+954;         // 2^954
  constexpr double  kCollapsed    = -0x1.0p+1000;       // -2^1000
  ASSERT_TRUE(transform.set_anchor({kInputAnchor, 0.0}, {kOutputAnchor, 0.0}));
  ASSERT_TRUE(transform.zoom_to(kScale, {kOutputAnchor, 0.0}));

  EXPECT_FALSE(transform.to_viewport({0.0, 0.0}).has_value());
  EXPECT_TRUE(transform.to_viewport({kCollapsed, 0.0}).has_value());
}

// The inverse twin of the zero-collapse counterexample: a viewport anchor of
// 3*2^1000 at scale 2^100 inverse-maps both 0 and -2^1000 to the same world
// image and recovers both as -2^1000. The zero's recovery is rejected; the
// value it collapsed onto is its own faithful round-trip and is accepted.
TEST(ViewportTransformTest, RejectsInverseZeroRecoveredAsCollapsedLargeValue) {
  ViewportTransform transform;
  constexpr double  kInputAnchor  = 3.0 * 0x1.0p+1000;  // 3*2^1000
  constexpr double  kScale        = 0x1.0p+100;         // 2^100
  constexpr double  kOutputAnchor = 0x1.0p+954;         // 2^954
  constexpr double  kCollapsed    = -0x1.0p+1000;       // -2^1000
  ASSERT_TRUE(transform.set_anchor({kOutputAnchor, 0.0}, {kInputAnchor, 0.0}));
  ASSERT_TRUE(transform.zoom_to(kScale, {kInputAnchor, 0.0}));

  EXPECT_FALSE(transform.to_world({0.0, 0.0}).has_value());
  EXPECT_TRUE(transform.to_world({kCollapsed, 0.0}).has_value());
}

// The ordinary canvas origin round-trips in both directions under a
// window-sized anchored transform (the case the absolute zero floor must keep
// accepting), and a near-zero nonzero value round-trips exactly under an
// identity transform, exercising the nonzero ULP branch at the smallest
// magnitudes rather than the zero branch.
TEST(ViewportTransformTest, OrdinaryOriginAndNearZeroRoundTrip) {
  ViewportTransform transform;
  ASSERT_TRUE(transform.set_anchor({640.0, 360.0}, {640.0, 360.0}));
  ASSERT_TRUE(transform.zoom_to(1.5, {640.0, 360.0}));
  ASSERT_TRUE(transform.pan_by({120.0, -60.0}));

  const auto origin_viewport = transform.to_viewport({0.0, 0.0});
  ASSERT_TRUE(origin_viewport);
  const auto origin_world = transform.to_world(*origin_viewport);
  ASSERT_TRUE(origin_world);
  EXPECT_NEAR(origin_world->x, 0.0, 1e-9);
  EXPECT_NEAR(origin_world->y, 0.0, 1e-9);

  const auto viewport_origin = transform.to_world({0.0, 0.0});
  ASSERT_TRUE(viewport_origin);
  ASSERT_TRUE(transform.to_viewport(*viewport_origin).has_value());

  ViewportTransform identity;
  const double      smallest = std::numeric_limits<double>::denorm_min();
  const auto        tiny     = identity.to_viewport({smallest, -1e-12});
  ASSERT_TRUE(tiny);
  const auto recovered = identity.to_world(*tiny);
  ASSERT_TRUE(recovered);
  EXPECT_EQ(recovered->x, smallest);
  EXPECT_EQ(recovered->y, -1e-12);
}

// The maximum-magnitude many-to-one collapse (forward direction): a world
// coordinate of ±DBL_MAX, world anchor 0, zoom ≈1.4·2^-76, and a viewport
// anchor of ±2^1000 forward-map DBL_MAX to ~2^1000 + 2^948 — the value's
// bits are absorbed into the rounding of the fma — so the inverse recovers a
// value ~29% smaller rather than DBL_MAX. A ULP budget whose one-ULP step is
// computed as nextafter(DBL_MAX, +∞) − DBL_MAX would be infinity and admit the
// loss; the finite predecessor spacing must reject it (the recovery is
// ~2.6e15 ULPs off).
TEST(ViewportTransformTest, RejectsForwardCollapseAtMaximumFiniteMagnitude) {
  constexpr double kMaximum = std::numeric_limits<double>::max();
  constexpr double kScale   = 0x1.6666666666666p-76;  // ≈1.4·2^-76
  constexpr double kAnchor  = 0x1.0p+1000;            // 2^1000

  {
    ViewportTransform transform;
    ASSERT_TRUE(transform.set_anchor({0.0, 0.0}, {kAnchor, 0.0}));
    ASSERT_TRUE(transform.zoom_to(kScale, {kAnchor, 0.0}));
    EXPECT_FALSE(transform.to_viewport({kMaximum, 0.0}).has_value());
  }
  {
    ViewportTransform transform;
    ASSERT_TRUE(transform.set_anchor({0.0, 0.0}, {-kAnchor, 0.0}));
    ASSERT_TRUE(transform.zoom_to(kScale, {-kAnchor, 0.0}));
    EXPECT_FALSE(transform.to_viewport({-kMaximum, 0.0}).has_value());
  }
}

// The inverse twin: a viewport coordinate of ±DBL_MAX, viewport anchor 0,
// zoom ≈1.4·2^76, and a world anchor of ∓2^1000 inverse-map DBL_MAX to
// ~2^1000 ± 2^947, and the forward recovery loses the value's bits. The
// recovered value is ~30% smaller in magnitude and must be rejected by the
// finite ULP budget, not admitted by an infinite one-ULP step.
TEST(ViewportTransformTest, RejectsInverseCollapseAtMaximumFiniteMagnitude) {
  constexpr double kMaximum      = std::numeric_limits<double>::max();
  constexpr double kInverseScale = 0x1.6666666666666p+76;  // ≈1.4·2^76
  constexpr double kAnchor       = 0x1.0p+1000;            // 2^1000

  {
    ViewportTransform transform;
    ASSERT_TRUE(transform.set_anchor({-kAnchor, 0.0}, {0.0, 0.0}));
    ASSERT_TRUE(transform.zoom_to(kInverseScale, {0.0, 0.0}));
    EXPECT_FALSE(transform.to_world({kMaximum, 0.0}).has_value());
  }
  {
    ViewportTransform transform;
    ASSERT_TRUE(transform.set_anchor({kAnchor, 0.0}, {0.0, 0.0}));
    ASSERT_TRUE(transform.zoom_to(kInverseScale, {0.0, 0.0}));
    EXPECT_FALSE(transform.to_world({-kMaximum, 0.0}).has_value());
  }
}

TEST(ViewportTransformTest, RetainsCanvasVersionCompatibility) {
  EXPECT_EQ(graphscore::canvas_version(), 1);
}

}  // namespace
