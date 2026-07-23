// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::Tempo;
using graphscore::TempoLane;
using graphscore::TempoPoint;
using graphscore::TempoSegmentKind;

namespace {

TempoPoint make_point(Rational position, std::int64_t bpm,
                      TempoSegmentKind kind = TempoSegmentKind::kStep) {
  return TempoPoint{position,
                    *Tempo::create(Rational(bpm), NoteValue::kQuarter), kind};
}

}  // namespace

TEST(TempoLaneTest, RejectsEmptyPoints) {
  EXPECT_FALSE(TempoLane::create({}, Rational(0), Rational(4)).has_value());
}

TEST(TempoLaneTest, RejectsFirstPointNotAtStart) {
  const auto lane = TempoLane::create({make_point(Rational(1), 120)},
                                      Rational(0), Rational(4));
  EXPECT_FALSE(lane.has_value());
}

TEST(TempoLaneTest, RejectsOutOfOrderPoints) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120),
      make_point(Rational(2), 100),
      make_point(Rational(1), 140),
  };
  EXPECT_FALSE(TempoLane::create(points, Rational(0), Rational(4)).has_value());
}

TEST(TempoLaneTest, RejectsDuplicatePositions) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120),
      make_point(Rational(0), 140),
  };
  EXPECT_FALSE(TempoLane::create(points, Rational(0), Rational(4)).has_value());
}

TEST(TempoLaneTest, RejectsPointAtOrPastEnd) {
  const std::vector<TempoPoint> points = {make_point(Rational(0), 120),
                                          make_point(Rational(4), 100)};
  EXPECT_FALSE(TempoLane::create(points, Rational(0), Rational(4)).has_value());
}

TEST(TempoLaneTest, AcceptsOrderedPointsCoveringMainRegionAndPickdown) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120, TempoSegmentKind::kStep),
      make_point(Rational(2), 100, TempoSegmentKind::kLinear),
      make_point(*Rational::create(15, 4), 140, TempoSegmentKind::kSmooth),
  };

  const auto lane = TempoLane::create(points, Rational(0), Rational(4));
  ASSERT_TRUE(lane.has_value());
  EXPECT_EQ(lane->points().size(), 3u);
  EXPECT_EQ(lane->start(), Rational(0));
  EXPECT_EQ(lane->end(), Rational(4));
  EXPECT_EQ(lane->points()[0].segment_kind, TempoSegmentKind::kStep);
  EXPECT_EQ(lane->points()[1].segment_kind, TempoSegmentKind::kLinear);
  EXPECT_EQ(lane->points()[2].segment_kind, TempoSegmentKind::kSmooth);
}

TEST(TempoLaneTest, SinglePointCoversTheWholeLane) {
  const auto lane = TempoLane::create({make_point(Rational(0), 120)},
                                      Rational(0), Rational(4));
  ASSERT_TRUE(lane.has_value());
  EXPECT_EQ(lane->points().size(), 1u);
}

// -- segment_index_at ------------------------------------------------------

TEST(TempoLaneTest, SegmentIndexAtFindsTheLastPointAtOrBeforePosition) {
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120),
      make_point(Rational(2), 100),
      make_point(*Rational::create(15, 4), 140),
  };
  const auto lane = TempoLane::create(points, Rational(0), Rational(4));
  ASSERT_TRUE(lane.has_value());

  EXPECT_EQ(lane->segment_index_at(Rational(0)), 0u);
  EXPECT_EQ(lane->segment_index_at(Rational(1)), 0u);
  EXPECT_EQ(lane->segment_index_at(Rational(2)), 1u);
  EXPECT_EQ(lane->segment_index_at(*Rational::create(5, 2)), 1u);
  EXPECT_EQ(lane->segment_index_at(*Rational::create(15, 4)), 2u);
  EXPECT_EQ(lane->segment_index_at(*Rational::create(63, 16)), 2u);
}

TEST(TempoLaneTest, SegmentIndexAtFailsOutsideTheLaneCoverage) {
  const auto lane = TempoLane::create({make_point(Rational(0), 120)},
                                      Rational(0), Rational(4));
  ASSERT_TRUE(lane.has_value());

  EXPECT_FALSE(lane->segment_index_at(Rational(-1)).has_value());
  EXPECT_FALSE(lane->segment_index_at(Rational(4)).has_value());
  EXPECT_FALSE(lane->segment_index_at(Rational(5)).has_value());
}
