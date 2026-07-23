// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::KeySignature;
using graphscore::Measure;
using graphscore::NodeTimeline;
using graphscore::NoteValue;
using graphscore::pickdown_coordinate_at_offset;
using graphscore::pickdown_coordinate_at_position;
using graphscore::Rational;
using graphscore::Tempo;
using graphscore::TempoPoint;
using graphscore::TimeSignature;

namespace {

Measure make_measure(std::uint8_t numerator, std::uint16_t denominator) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 *KeySignature::create(0)};
}

TempoPoint make_point(Rational position, std::int64_t bpm) {
  return TempoPoint{position,
                    *Tempo::create(Rational(bpm), NoteValue::kQuarter)};
}

// Boundary measure is 3/4 (index 1); main region totals 4/4 + 3/4 == 7/4.
std::optional<NodeTimeline> make_timeline_with_pickdown(Rational duration) {
  auto timeline =
      NodeTimeline::create({make_measure(4, 4), make_measure(3, 4)}, {});
  if (!timeline.has_value())
    return std::nullopt;
  if (!timeline->set_pickdown(duration).ok())
    return std::nullopt;
  return timeline;
}

}  // namespace

// -- pickdown_coordinate_at_position -----------------------------------

TEST(PickdownCoordinatesTest, ResolvesMeterAndOffsetAtTheBoundary) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 4));
  ASSERT_TRUE(timeline.has_value());

  const auto coordinate =
      pickdown_coordinate_at_position(*timeline, *Rational::create(7, 4));
  ASSERT_TRUE(coordinate.has_value());
  EXPECT_EQ(coordinate->position, *Rational::create(7, 4));
  EXPECT_EQ(coordinate->offset, Rational(0));
  EXPECT_EQ(coordinate->boundary_measure_index, 1u);
  EXPECT_EQ(coordinate->governing_time_signature, *TimeSignature::create(3, 4));
  EXPECT_FALSE(coordinate->tempo_segment_index.has_value());
}

TEST(PickdownCoordinatesTest, ResolvesAnOffsetPartwayThroughThePickdown) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 2));
  ASSERT_TRUE(timeline.has_value());

  // Boundary is 7/4; an eighth note into the pickdown is 7/4 + 1/8.
  const Rational position = *Rational::create(7, 4) + *Rational::create(1, 8);
  const auto coordinate = pickdown_coordinate_at_position(*timeline, position);
  ASSERT_TRUE(coordinate.has_value());
  EXPECT_EQ(coordinate->offset, *Rational::create(1, 8));
  EXPECT_EQ(coordinate->governing_time_signature, *TimeSignature::create(3, 4));
}

TEST(PickdownCoordinatesTest, FailsForAPositionBeforeTheBoundary) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 4));
  ASSERT_TRUE(timeline.has_value());

  EXPECT_FALSE(
      pickdown_coordinate_at_position(*timeline, Rational(0)).has_value());
  EXPECT_FALSE(
      pickdown_coordinate_at_position(*timeline, *Rational::create(3, 2))
          .has_value());
}

TEST(PickdownCoordinatesTest, FailsForAPositionAtOrPastNodeEnd) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 4));
  ASSERT_TRUE(timeline.has_value());
  ASSERT_EQ(timeline->node_end(), Rational(2));

  EXPECT_FALSE(
      pickdown_coordinate_at_position(*timeline, Rational(2)).has_value());
  EXPECT_FALSE(
      pickdown_coordinate_at_position(*timeline, Rational(3)).has_value());
}

TEST(PickdownCoordinatesTest, FailsWhenTheTimelineHasNoPickdownAtAll) {
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());

  EXPECT_FALSE(
      pickdown_coordinate_at_position(*timeline, Rational(0)).has_value());
}

// -- pickdown_coordinate_at_offset --------------------------------------

TEST(PickdownCoordinatesTest, OffsetEntryPointAgreesWithPositionEntryPoint) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 2));
  ASSERT_TRUE(timeline.has_value());

  const auto from_offset =
      pickdown_coordinate_at_offset(*timeline, *Rational::create(1, 8));
  const auto from_position = pickdown_coordinate_at_position(
      *timeline, *Rational::create(7, 4) + *Rational::create(1, 8));
  ASSERT_TRUE(from_offset.has_value());
  ASSERT_TRUE(from_position.has_value());
  EXPECT_EQ(*from_offset, *from_position);
}

TEST(PickdownCoordinatesTest, OffsetEntryPointRejectsNegativeOffset) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 4));
  ASSERT_TRUE(timeline.has_value());

  EXPECT_FALSE(
      pickdown_coordinate_at_offset(*timeline, Rational(-1)).has_value());
}

TEST(PickdownCoordinatesTest, OffsetEntryPointRejectsOffsetAtOrPastDuration) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 4));
  ASSERT_TRUE(timeline.has_value());

  EXPECT_FALSE(pickdown_coordinate_at_offset(*timeline, *Rational::create(1, 4))
                   .has_value());
}

// -- Governing tempo segment ---------------------------------------------

TEST(PickdownCoordinatesTest, IdentifiesTheSegmentGoverningAPickdownPosition) {
  auto timeline = make_timeline_with_pickdown(*Rational::create(1, 2));
  ASSERT_TRUE(timeline.has_value());

  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120),
      make_point(*Rational::create(7, 4), 100),
      make_point(*Rational::create(15, 8), 140),
  };
  ASSERT_TRUE(timeline->set_tempo(points).ok());

  // At the boundary itself, the segment starting exactly there governs.
  const auto at_boundary =
      pickdown_coordinate_at_position(*timeline, *Rational::create(7, 4));
  ASSERT_TRUE(at_boundary.has_value());
  ASSERT_TRUE(at_boundary->tempo_segment_index.has_value());
  EXPECT_EQ(*at_boundary->tempo_segment_index, 1u);

  // Between the second and third points, the second point's segment
  // still governs.
  const auto mid_segment = pickdown_coordinate_at_position(
      *timeline, *Rational::create(15, 8) - *Rational::create(1, 16));
  ASSERT_TRUE(mid_segment.has_value());
  ASSERT_TRUE(mid_segment->tempo_segment_index.has_value());
  EXPECT_EQ(*mid_segment->tempo_segment_index, 1u);

  // At or past the third point, the third point's segment governs.
  const auto last_segment =
      pickdown_coordinate_at_position(*timeline, *Rational::create(15, 8));
  ASSERT_TRUE(last_segment.has_value());
  ASSERT_TRUE(last_segment->tempo_segment_index.has_value());
  EXPECT_EQ(*last_segment->tempo_segment_index, 2u);
}

// -- Minimum main-region duration invariant -------------------------------

TEST(PickdownCoordinatesTest, NodeTimelineCreateRejectsAnEmptyMainRegion) {
  EXPECT_FALSE(NodeTimeline::create({}, {}).has_value());
}

TEST(PickdownCoordinatesTest,
     PickdownMutationNeverChangesTheMainRegionMeasureCount) {
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  const std::size_t original_count = timeline->measures().measure_count();
  ASSERT_EQ(original_count, 1u);

  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  EXPECT_EQ(timeline->measures().measure_count(), original_count);

  ASSERT_TRUE(timeline->clear_pickdown().ok());
  EXPECT_EQ(timeline->measures().measure_count(), original_count);
}
