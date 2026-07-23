// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::Tempo;
using graphscore::TempoPoint;
using graphscore::TempoSegmentKind;

namespace {

TempoPoint make_point(Rational position, std::int64_t bpm, NoteValue beat_unit,
                      TempoSegmentKind kind) {
  return TempoPoint{position, *Tempo::create(Rational(bpm), beat_unit), kind};
}

TempoPoint make_point(Rational position, std::int64_t bpm,
                      TempoSegmentKind kind = TempoSegmentKind::kStep) {
  return make_point(position, bpm, NoteValue::kQuarter, kind);
}

constexpr double kTolerance = 1e-9;

}  // namespace

// -- TempoPoint (relocated from graphscore/domain/tempo_lane.hpp) ----------

TEST(TempoPointTest, DefaultSegmentKindIsStep) {
  const TempoPoint point{Rational(0),
                         *Tempo::create(Rational(120), NoteValue::kQuarter)};
  EXPECT_EQ(point.segment_kind, TempoSegmentKind::kStep);
}

TEST(TempoPointTest, EqualityComparesEveryField) {
  const TempoPoint a = make_point(Rational(0), 120, TempoSegmentKind::kLinear);
  const TempoPoint b = make_point(Rational(0), 120, TempoSegmentKind::kLinear);
  const TempoPoint c = make_point(Rational(0), 100, TempoSegmentKind::kLinear);
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// -- tempo_rate_at: kStep ----------------------------------------------------

TEST(TempoCurveTest, StepIsConstantAcrossTheWholeSegment) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120, TempoSegmentKind::kStep),
      make_point(Rational(4), 60, TempoSegmentKind::kStep),
  };
  const double expected = 120.0 * (1.0 / 4.0) / 60.0;  // whole notes/second.
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(0)), expected,
              kTolerance);
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(2)), expected,
              kTolerance);
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, *Rational::create(15, 4)),
              expected, kTolerance);
}

TEST(TempoCurveTest, StepOnTrailingPointHoldsForeverPastTheLastPoint) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120, TempoSegmentKind::kLinear),
      make_point(Rational(4), 240, TempoSegmentKind::kSmooth),
  };
  const double expected = 240.0 * (1.0 / 4.0) / 60.0;
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(4)), expected,
              kTolerance);
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(1000)), expected,
              kTolerance);
}

TEST(TempoCurveTest, RateAtFailsBeforeTheFirstPoint) {
  const std::vector<TempoPoint> points = {make_point(Rational(1), 120)};
  EXPECT_FALSE(graphscore::tempo_rate_at(points, Rational(0)).has_value());
}

TEST(TempoCurveTest, RateAtFailsOnEmptyPoints) {
  const std::vector<TempoPoint> points;
  EXPECT_FALSE(graphscore::tempo_rate_at(points, Rational(0)).has_value());
}

// -- tempo_rate_at: kLinear --------------------------------------------------

TEST(TempoCurveTest, LinearInterpolatesMonotonicallyBetweenEndpoints) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 60, TempoSegmentKind::kLinear),
      make_point(Rational(4), 180, TempoSegmentKind::kLinear),
  };
  const double r0  = 60.0 * (1.0 / 4.0) / 60.0;
  const double r1  = 180.0 * (1.0 / 4.0) / 60.0;
  const double mid = (r0 + r1) / 2.0;

  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(0)), r0, kTolerance);
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(2)), mid, kTolerance);
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(4)), r1, kTolerance);
}

// -- tempo_rate_at: kSmooth ---------------------------------------------------

TEST(TempoCurveTest, SmoothPassesExactlyThroughEveryControlPoint) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 60, TempoSegmentKind::kSmooth),
      make_point(Rational(2), 120, TempoSegmentKind::kSmooth),
      make_point(Rational(4), 90, TempoSegmentKind::kSmooth),
      make_point(Rational(6), 150, TempoSegmentKind::kSmooth),
  };
  for (const TempoPoint& point : points) {
    const double expected = point.tempo.bpm().to_double() * (1.0 / 4.0) / 60.0;
    EXPECT_NEAR(*graphscore::tempo_rate_at(points, point.position), expected,
                kTolerance)
        << "at position " << point.position.to_double();
  }
}

TEST(TempoCurveTest, SmoothStaysBoundedNearControlPointsForWellSpacedData) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 60, TempoSegmentKind::kSmooth),
      make_point(Rational(2), 120, TempoSegmentKind::kSmooth),
      make_point(Rational(4), 90, TempoSegmentKind::kSmooth),
  };
  const std::optional<double> mid =
      graphscore::tempo_rate_at(points, Rational(1));
  ASSERT_TRUE(mid.has_value());
  EXPECT_GT(*mid, 0.0);
  EXPECT_LT(*mid, 400.0 * (1.0 / 4.0) / 60.0 * 2.0);
}

// -- Multi-beat-unit normalization -------------------------------------------

TEST(TempoCurveTest, EquivalentBpmBeatUnitPairsInterpolateIdentically) {
  const std::vector<TempoPoint> quarter_points = {
      make_point(Rational(0), 120, NoteValue::kQuarter,
                 TempoSegmentKind::kLinear),
      make_point(Rational(4), 60, NoteValue::kQuarter,
                 TempoSegmentKind::kLinear),
  };
  const std::vector<TempoPoint> half_points = {
      make_point(Rational(0), 60, NoteValue::kHalf, TempoSegmentKind::kLinear),
      make_point(Rational(4), 30, NoteValue::kHalf, TempoSegmentKind::kLinear),
  };

  for (const std::int64_t numerator : {0, 1, 2, 3}) {
    const Rational position(numerator);
    EXPECT_NEAR(*graphscore::tempo_rate_at(quarter_points, position),
                *graphscore::tempo_rate_at(half_points, position), kTolerance);
  }
}

TEST(TempoCurveTest, MixedBeatUnitsWithinOneLaneNormalizeBeforeInterpolating) {
  // 120 BPM quarter and 60 BPM half are the identical rate; a lane mixing
  // both beat units across its points must interpolate as if both were
  // expressed in the same unit, not blend raw BPM numbers.
  const std::vector<TempoPoint> mixed = {
      make_point(Rational(0), 120, NoteValue::kQuarter,
                 TempoSegmentKind::kLinear),
      make_point(Rational(4), 60, NoteValue::kHalf, TempoSegmentKind::kLinear),
  };
  const double r0 = 120.0 * (1.0 / 4.0) / 60.0;
  const double r1 = 60.0 * (1.0 / 2.0) / 60.0;
  EXPECT_NEAR(r0, r1, kTolerance);  // Sanity: the two points are equal rates.
  EXPECT_NEAR(*graphscore::tempo_rate_at(mixed, Rational(2)), r0, kTolerance);
}

TEST(TempoCurveTest, LinearInterpolationUsesNormalizedRatesNotRawBpm) {
  // 120 BPM/quarter (0.5 wn/sec) to 30 BPM/half (0.25 wn/sec): a bug that
  // linearly interpolated raw BPM (mid == (120 + 30) / 2 == 75) instead of
  // the normalized rate (mid == (0.5 + 0.25) / 2 == 0.375) would fail this.
  const std::vector<TempoPoint> mixed = {
      make_point(Rational(0), 120, NoteValue::kQuarter,
                 TempoSegmentKind::kLinear),
      make_point(Rational(4), 30, NoteValue::kHalf, TempoSegmentKind::kLinear),
  };
  EXPECT_NEAR(*graphscore::tempo_rate_at(mixed, Rational(2)), 0.375,
              kTolerance);
}

// -- integrate_elapsed_seconds -----------------------------------------------

TEST(TempoCurveTest, IntegrateFailsBeforeTheFirstPoint) {
  const std::vector<TempoPoint> points = {make_point(Rational(1), 120)};
  EXPECT_FALSE(
      graphscore::integrate_elapsed_seconds(points, Rational(0), Rational(2))
          .has_value());
}

TEST(TempoCurveTest, IntegrateFailsWhenFromIsAfterTo) {
  const std::vector<TempoPoint> points = {make_point(Rational(0), 120)};
  EXPECT_FALSE(
      graphscore::integrate_elapsed_seconds(points, Rational(2), Rational(0))
          .has_value());
}

TEST(TempoCurveTest, IntegrateOfAnEmptyRangeIsZero) {
  const std::vector<TempoPoint> points = {make_point(Rational(0), 120)};
  EXPECT_NEAR(
      *graphscore::integrate_elapsed_seconds(points, Rational(1), Rational(1)),
      0.0, kTolerance);
}

TEST(TempoCurveTest, StepIntegrationMatchesDistanceOverRate) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120, TempoSegmentKind::kStep)};
  const double rate     = 120.0 * (1.0 / 4.0) / 60.0;
  const double expected = 4.0 / rate;  // 4 whole notes at 0.5 wn/sec = 8s.
  EXPECT_NEAR(
      *graphscore::integrate_elapsed_seconds(points, Rational(0), Rational(4)),
      expected, kTolerance);
}

TEST(TempoCurveTest, IntegrationIsMonotonicallyIncreasingWithPosition) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 60, TempoSegmentKind::kSmooth),
      make_point(Rational(2), 200, TempoSegmentKind::kLinear),
      make_point(Rational(4), 90, TempoSegmentKind::kStep),
      make_point(Rational(6), 300, TempoSegmentKind::kSmooth),
  };

  double previous = 0.0;
  for (int tenths = 1; tenths <= 80; ++tenths) {
    const Rational to = *Rational::create(tenths, 10);
    const double   elapsed =
        *graphscore::integrate_elapsed_seconds(points, Rational(0), to);
    EXPECT_GT(elapsed, previous);
    previous = elapsed;
  }
}

TEST(TempoCurveTest, IntegrationSpansMultipleSegmentsConsistently) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120, TempoSegmentKind::kStep),
      make_point(Rational(2), 60, TempoSegmentKind::kLinear),
      make_point(Rational(4), 180, TempoSegmentKind::kSmooth),
      make_point(Rational(6), 90, TempoSegmentKind::kStep),
  };
  const double whole =
      *graphscore::integrate_elapsed_seconds(points, Rational(0), Rational(6));
  const double first_half =
      *graphscore::integrate_elapsed_seconds(points, Rational(0), Rational(3));
  const double second_half =
      *graphscore::integrate_elapsed_seconds(points, Rational(3), Rational(6));
  EXPECT_NEAR(whole, first_half + second_half, kTolerance);
}

TEST(TempoCurveTest, SmoothIntegrationOfConstantTempoMatchesDistanceOverRate) {
  // Four kSmooth points sharing the identical tempo make every tangent zero
  // (r_i == r_(i+1) == r_prev == r_next), so the Hermite cubic degenerates
  // exactly to a constant -- an independent reference this header's own
  // formulas do not need to be re-derived to check against.
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120, TempoSegmentKind::kSmooth),
      make_point(Rational(1), 120, TempoSegmentKind::kSmooth),
      make_point(Rational(9), 120, TempoSegmentKind::kSmooth),
      make_point(Rational(10), 120, TempoSegmentKind::kSmooth),
  };
  const double rate = 120.0 * (1.0 / 4.0) / 60.0;  // 0.5 whole notes/second.
  const double expected =
      (Rational(10) - Rational(0)).to_double() / rate;  // 20 seconds.
  EXPECT_NEAR(
      *graphscore::integrate_elapsed_seconds(points, Rational(0), Rational(10)),
      expected, kTolerance);

  // The same degenerate-constant reference holds within the unevenly
  // spaced middle segment [1, 9] alone.
  const double middle_expected = 8.0 / rate;  // 4 seconds.
  EXPECT_NEAR(
      *graphscore::integrate_elapsed_seconds(points, Rational(1), Rational(9)),
      middle_expected, kTolerance);
}

TEST(TempoCurveTest,
     SmoothIntegrationIsAdditiveAcrossAnInteriorSplitOfOneSegment) {
  // Regression for the anchored-antiderivative quadrature fix: a fixed
  // Simpson step COUNT applied directly to whatever sub-range is requested
  // is not exactly additive across a split strictly inside one kSmooth
  // segment; anchoring the quadrature at the segment's own start makes it
  // additive again. Uses varied (non-degenerate) tempos so the tangents are
  // non-zero and the check is not vacuous.
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 60, TempoSegmentKind::kSmooth),
      make_point(Rational(2), 120, TempoSegmentKind::kSmooth),
      make_point(Rational(4), 90, TempoSegmentKind::kSmooth),
      make_point(Rational(6), 150, TempoSegmentKind::kSmooth),
  };
  // Split at position 3, strictly inside the [2, 4] kSmooth segment.
  const double whole =
      *graphscore::integrate_elapsed_seconds(points, Rational(2), Rational(4));
  const double first_half =
      *graphscore::integrate_elapsed_seconds(points, Rational(2), Rational(3));
  const double second_half =
      *graphscore::integrate_elapsed_seconds(points, Rational(3), Rational(4));
  EXPECT_NEAR(whole, first_half + second_half, kTolerance);

  // A second split point, closer to one end, exercises an asymmetric
  // narrow/wide sub-range pair -- exactly the case that exposed the
  // original non-additivity.
  const std::optional<double> near_start =
      graphscore::integrate_elapsed_seconds(points, Rational(2),
                                            *Rational::create(21, 10));
  const std::optional<double> rest = graphscore::integrate_elapsed_seconds(
      points, *Rational::create(21, 10), Rational(4));
  EXPECT_NEAR(whole, *near_start + *rest, kTolerance);
}

TEST(TempoCurveTest, SmoothReflectedEndpointTangentMatchesHandComputation) {
  // Pins the documented reflected/mirrored endpoint convention
  // (src/core/tempo_curve.cpp, smooth_rate()) against values computed by
  // hand from the header's own cardinal-spline formulas, independently of
  // the implementation under test, for a lane with no left neighbor at
  // points[0] and no right neighbor at points[2].
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 60, TempoSegmentKind::kSmooth),   // r0 = 0.25.
      make_point(Rational(2), 120, TempoSegmentKind::kSmooth),  // r1 = 0.5.
      make_point(Rational(4), 90, TempoSegmentKind::kSmooth),   // r2 = 0.375.
  };
  // Segment [0, 2]: r_prev reflected as 2*r0 - r1 = 0.0; r_next is the real
  // points[2] rate (0.375) since index + 2 == 2 < points.size(). At tau ==
  // 0.5 (position 1): m_i = 0.5*(0.5 - 0.0) = 0.25,
  // m_(i+1) = 0.5*(0.375 - 0.25) = 0.0625, giving rate == 0.3984375.
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(1)), 0.3984375,
              kTolerance);

  // Segment [2, 4]: r_prev is the real points[0] rate (0.25); r_next
  // reflected as 2*r2 - r1 = 0.25 since index + 2 == 3 >= points.size(). At
  // tau == 0.5 (position 3): m_i = 0.5*(0.375 - 0.25) = 0.0625,
  // m_(i+1) = 0.5*(0.25 - 0.5) = -0.125, giving rate == 0.4609375.
  EXPECT_NEAR(*graphscore::tempo_rate_at(points, Rational(3)), 0.4609375,
              kTolerance);
}

// -- invert_elapsed_seconds ---------------------------------------------------

TEST(TempoCurveTest, InvertFailsOnAnEmptyOrInvertedSearchRange) {
  const std::vector<TempoPoint> points = {make_point(Rational(0), 120)};
  EXPECT_FALSE(
      graphscore::invert_elapsed_seconds(points, Rational(0), Rational(0), 1.0)
          .has_value());
  EXPECT_FALSE(
      graphscore::invert_elapsed_seconds(points, Rational(2), Rational(0), 1.0)
          .has_value());
}

TEST(TempoCurveTest, InvertOfZeroElapsedSecondsReturnsFromExactly) {
  const std::vector<TempoPoint> points = {make_point(Rational(0), 120)};
  const std::optional<Rational> position =
      graphscore::invert_elapsed_seconds(points, Rational(1), Rational(8), 0.0);
  ASSERT_TRUE(position.has_value());
  EXPECT_EQ(*position, Rational(1));
}

TEST(TempoCurveTest, InvertClampsToCurveEndWhenElapsedExceedsTheWholeRange) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120, TempoSegmentKind::kStep)};
  const std::optional<Rational> position = graphscore::invert_elapsed_seconds(
      points, Rational(0), Rational(4), 1.0e9);
  ASSERT_TRUE(position.has_value());
  EXPECT_EQ(*position, Rational(4));
}

TEST(TempoCurveTest, RoundTripThroughIntegrateAndInvertRecoversThePosition) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 60, TempoSegmentKind::kSmooth),
      make_point(Rational(2), 200, TempoSegmentKind::kLinear),
      make_point(Rational(4), 90, TempoSegmentKind::kSmooth),
      make_point(Rational(6), 150, TempoSegmentKind::kStep),
  };
  const Rational from      = Rational(0);
  const Rational curve_end = Rational(8);

  for (const std::int64_t numerator : {1, 4, 7, 11, 15}) {
    const Rational target = *Rational::create(numerator, 2);
    const double   elapsed =
        *graphscore::integrate_elapsed_seconds(points, from, target);
    const std::optional<Rational> recovered =
        graphscore::invert_elapsed_seconds(points, from, curve_end, elapsed);
    ASSERT_TRUE(recovered.has_value());
    EXPECT_NEAR(recovered->to_double(), target.to_double(), 1e-6)
        << "target position " << target.to_double();
  }
}

// -- round_to_sample_count ----------------------------------------------------

TEST(TempoCurveTest, RoundToSampleCountFloorsBelowTheHalfwayPoint) {
  EXPECT_EQ(graphscore::round_to_sample_count(1.4 / 48000.0, 48000), 1);
}

TEST(TempoCurveTest, RoundToSampleCountCeilsAboveTheHalfwayPoint) {
  EXPECT_EQ(graphscore::round_to_sample_count(1.6 / 48000.0, 48000), 2);
}

TEST(TempoCurveTest, RoundToSampleCountBreaksExactTiesToEven) {
  // sample_rate_hz == 2 and elapsed_seconds a quarter-second multiple keep
  // `elapsed_seconds * sample_rate_hz` an exact binary fraction (no
  // floating rounding noise), so these land exactly on a .5 tie.
  EXPECT_EQ(graphscore::round_to_sample_count(0.25, 2), 0);  // scaled 0.5.
  EXPECT_EQ(graphscore::round_to_sample_count(0.75, 2), 2);  // scaled 1.5.
  EXPECT_EQ(graphscore::round_to_sample_count(1.25, 2), 2);  // scaled 2.5.
  EXPECT_EQ(graphscore::round_to_sample_count(1.75, 2), 4);  // scaled 3.5.
}

TEST(TempoCurveTest, RoundToSampleCountHandlesZeroElapsedTime) {
  EXPECT_EQ(graphscore::round_to_sample_count(0.0, 48000), 0);
}

TEST(TempoCurveTest, RoundToSampleCountRejectsNonPositiveSampleRate) {
  EXPECT_EQ(graphscore::round_to_sample_count(1.0, 0), 0);
  EXPECT_EQ(graphscore::round_to_sample_count(1.0, -48000), 0);
}

TEST(TempoCurveTest, RoundToSampleCountIsAPureFunctionOfItsArguments) {
  EXPECT_EQ(graphscore::round_to_sample_count(2.5, 44100),
            graphscore::round_to_sample_count(2.5, 44100));
}

TEST(TempoCurveTest, RoundToSampleCountScalesWithASlowerSampleRate) {
  EXPECT_EQ(graphscore::round_to_sample_count(1.0, 8000), 8000);
}

TEST(TempoCurveTest, RoundToSampleCountRejectsNaN) {
  EXPECT_EQ(graphscore::round_to_sample_count(
                std::numeric_limits<double>::quiet_NaN(), 48000),
            0);
}

TEST(TempoCurveTest, RoundToSampleCountRejectsPositiveInfinity) {
  EXPECT_EQ(graphscore::round_to_sample_count(
                std::numeric_limits<double>::infinity(), 48000),
            0);
}

TEST(TempoCurveTest, RoundToSampleCountRejectsNegativeInfinity) {
  EXPECT_EQ(graphscore::round_to_sample_count(
                -std::numeric_limits<double>::infinity(), 48000),
            0);
}

TEST(TempoCurveTest, RoundToSampleCountSaturatesOnHugeFiniteInput) {
  // 1e30 seconds at 48 kHz scales far outside std::int64_t's range; this
  // must saturate rather than invoke undefined behavior on the cast.
  EXPECT_EQ(graphscore::round_to_sample_count(1e30, 48000),
            std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(graphscore::round_to_sample_count(-1e30, 48000),
            std::numeric_limits<std::int64_t>::min());
}

TEST(TempoCurveTest, RoundToSampleCountHandlesNegativeElapsedTime) {
  // Nearest-integer rounding away from an exact tie.
  EXPECT_EQ(graphscore::round_to_sample_count(-1.4 / 48000.0, 48000), -1);
  EXPECT_EQ(graphscore::round_to_sample_count(-1.6 / 48000.0, 48000), -2);
}

TEST(TempoCurveTest, RoundToSampleCountBreaksNegativeExactTiesToEven) {
  // Mirrors RoundToSampleCountBreaksExactTiesToEven with negative elapsed
  // time, pinning the current round-half-to-even behavior for negative
  // scaled values (floor()/fractional() based, not symmetric rounding).
  EXPECT_EQ(graphscore::round_to_sample_count(-0.25, 2), 0);   // scaled -0.5.
  EXPECT_EQ(graphscore::round_to_sample_count(-0.75, 2), -2);  // scaled -1.5.
  EXPECT_EQ(graphscore::round_to_sample_count(-1.25, 2), -2);  // scaled -2.5.
  EXPECT_EQ(graphscore::round_to_sample_count(-1.75, 2), -4);  // scaled -3.5.
}
