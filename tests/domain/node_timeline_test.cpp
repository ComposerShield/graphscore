// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Clef;
using graphscore::KeySignature;
using graphscore::Measure;
using graphscore::MusicalSpan;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::Tempo;
using graphscore::TempoPoint;
using graphscore::TimelineRegion;
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

}  // namespace

TEST(NodeTimelineTest, RejectsEmptyMainRegion) {
  EXPECT_FALSE(NodeTimeline::create({}, {}).has_value());
}

TEST(NodeTimelineTest, SeedsOneClefLanePerStaveAtItsDefaultClef) {
  const std::vector<StaveDefinition> staves = {
      {StaveId::generate(), Clef::kTreble},
      {StaveId::generate(), Clef::kBass},
  };

  const auto timeline = NodeTimeline::create({make_measure(4, 4)}, staves);
  ASSERT_TRUE(timeline.has_value());

  ASSERT_TRUE(timeline->has_clef_lane(staves[0].id));
  ASSERT_TRUE(timeline->has_clef_lane(staves[1].id));
  EXPECT_EQ(timeline->clef_lane(staves[0].id)->clef_at(Rational(0)),
            Clef::kTreble);
  EXPECT_EQ(timeline->clef_lane(staves[1].id)->clef_at(Rational(0)),
            Clef::kBass);
}

TEST(NodeTimelineTest, GrandStaffStaveClefLanesAreIndependent) {
  const std::vector<StaveDefinition> staves = {
      {StaveId::generate(), Clef::kTreble},
      {StaveId::generate(), Clef::kBass},
  };
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, staves);
  ASSERT_TRUE(timeline.has_value());

  ASSERT_TRUE(timeline->clef_lane(staves[0].id)
                  ->add_change(Rational(2), Clef::kAlto)
                  .ok());

  EXPECT_EQ(timeline->clef_lane(staves[0].id)->clef_at(Rational(2)),
            Clef::kAlto);
  EXPECT_EQ(timeline->clef_lane(staves[1].id)->clef_at(Rational(2)),
            Clef::kBass);
}

class PickdownTest : public ::testing::Test {
 protected:
  std::optional<NodeTimeline> make_timeline() {
    return NodeTimeline::create({make_measure(4, 4), make_measure(3, 4)}, {});
  }
};

TEST_F(PickdownTest, AcceptsValidPickdown) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());

  // Boundary measure is 3/4; a 1/4 pickdown is strictly within (0, 3/4).
  // The main region is 4/4 + 3/4 == 7/4, so the boundary is at 7/4.
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 4));
  EXPECT_EQ(timeline->boundary_position(), *Rational::create(7, 4));
  EXPECT_EQ(timeline->node_end(), Rational(2));
}

TEST_F(PickdownTest, RejectsZeroDuration) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  EXPECT_FALSE(timeline->set_pickdown(Rational(0)).ok());
  EXPECT_FALSE(timeline->pickdown_duration().has_value());
}

TEST_F(PickdownTest, RejectsNegativeDuration) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  EXPECT_FALSE(timeline->set_pickdown(Rational(-1)).ok());
}

TEST_F(PickdownTest, RejectsFullBoundaryMeasureDuration) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  // Boundary measure (3/4) is not a valid pickdown: must be strictly less.
  EXPECT_FALSE(timeline->set_pickdown(*Rational::create(3, 4)).ok());
}

TEST_F(PickdownTest, RejectsDurationLongerThanBoundaryMeasure) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  EXPECT_FALSE(timeline->set_pickdown(Rational(1)).ok());
}

TEST_F(PickdownTest, NoPickdownNodeEndEqualsMainRegionBoundary) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  EXPECT_FALSE(timeline->pickdown_duration().has_value());
  EXPECT_EQ(timeline->node_end(), timeline->boundary_position());
}

class BoundaryClassificationTest : public ::testing::Test {
 protected:
  std::optional<NodeTimeline> make_timeline_with_pickdown() {
    auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
    if (!timeline.has_value())
      return timeline;
    if (!timeline->set_pickdown(*Rational::create(1, 4)).ok())
      return std::nullopt;
    return timeline;
  }
};

TEST_F(BoundaryClassificationTest, PositionBeforeBoundaryIsMain) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  EXPECT_EQ(timeline->classify(Rational(0)), TimelineRegion::kMain);
  EXPECT_EQ(timeline->classify(*Rational::create(3, 4)), TimelineRegion::kMain);
}

TEST_F(BoundaryClassificationTest, PositionAtOrAfterBoundaryIsPickdown) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  EXPECT_EQ(timeline->classify(Rational(1)), TimelineRegion::kPickdown);
  EXPECT_EQ(timeline->classify(*Rational::create(9, 8)),
            TimelineRegion::kPickdown);
}

TEST_F(BoundaryClassificationTest, SpanEntirelyInMainRegionStaysWhole) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());

  const MusicalSpan span{Rational(0), *Rational::create(3, 4)};
  const auto        result = timeline->classify(span);
  ASSERT_TRUE(result.main_part.has_value());
  EXPECT_EQ(*result.main_part, span);
  EXPECT_FALSE(result.pickdown_part.has_value());
}

TEST_F(BoundaryClassificationTest, SpanEntirelyInPickdownStaysWhole) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());

  const MusicalSpan span{Rational(1), *Rational::create(5, 4)};
  const auto        result = timeline->classify(span);
  EXPECT_FALSE(result.main_part.has_value());
  ASSERT_TRUE(result.pickdown_part.has_value());
  EXPECT_EQ(*result.pickdown_part, span);
}

TEST_F(BoundaryClassificationTest, SpanStraddlingBoundarySplitsExactlyAtIt) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());

  const MusicalSpan span{*Rational::create(3, 4), *Rational::create(5, 4)};
  const auto        result = timeline->classify(span);
  ASSERT_TRUE(result.main_part.has_value());
  ASSERT_TRUE(result.pickdown_part.has_value());
  EXPECT_EQ(result.main_part->start, *Rational::create(3, 4));
  EXPECT_EQ(result.main_part->end, Rational(1));
  EXPECT_EQ(result.pickdown_part->start, Rational(1));
  EXPECT_EQ(result.pickdown_part->end, *Rational::create(5, 4));
}

TEST_F(BoundaryClassificationTest, NoPickdownClassifiesEverythingAsMain) {
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());

  EXPECT_EQ(timeline->classify(*Rational::create(1, 2)), TimelineRegion::kMain);

  const MusicalSpan span{Rational(0), Rational(1)};
  const auto        result = timeline->classify(span);
  ASSERT_TRUE(result.main_part.has_value());
  EXPECT_EQ(*result.main_part, span);
  EXPECT_FALSE(result.pickdown_part.has_value());
}

TEST(NodeTimelineTempoTest, SetTempoRequiresCoverageThroughPickdown) {
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());

  // A single point at position 0 formally "covers" the lane (its segment
  // runs to end()), so this succeeds; the boundary-crossing rejection case
  // is a point placed at or past node_end(), covered separately below.
  EXPECT_TRUE(timeline->set_tempo({make_point(Rational(0), 120)}).ok());
}

TEST(NodeTimelineTempoTest, RejectsTempoPointPastNodeEnd) {
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());

  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120), make_point(*Rational::create(5, 4), 100)};
  EXPECT_FALSE(timeline->set_tempo(points).ok());
}

TEST(NodeTimelineTempoTest, AcceptsTempoCoveringMainRegionAndPickdown) {
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());

  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120),
      make_point(Rational(1), 100),
  };
  ASSERT_TRUE(timeline->set_tempo(points).ok());
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->end(), *Rational::create(5, 4));
}

class TempoRevalidationTest : public ::testing::Test {
 protected:
  std::optional<NodeTimeline> make_timeline() {
    return NodeTimeline::create({make_measure(4, 4)}, {});
  }
};

TEST_F(TempoRevalidationTest, ClearPickdownWithoutTempoRemovesPickdown) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());

  EXPECT_TRUE(timeline->clear_pickdown().ok());
  EXPECT_FALSE(timeline->pickdown_duration().has_value());
  EXPECT_EQ(timeline->node_end(), timeline->boundary_position());
}

TEST_F(TempoRevalidationTest,
       SetPickdownShrinkKeepsStillValidTempoAndRebuildsEnd) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 4)).ok());
  ASSERT_TRUE(timeline->set_tempo({make_point(Rational(0), 120)}).ok());
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->end(), *Rational::create(7, 4));

  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 4));
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->end(), *Rational::create(5, 4));
}

TEST_F(TempoRevalidationTest,
       SetPickdownExtendKeepsStillValidTempoAndRebuildsEnd) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_tempo({make_point(Rational(0), 120)}).ok());
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->end(), Rational(1));

  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->end(), *Rational::create(5, 4));
}

TEST_F(TempoRevalidationTest, ClearPickdownKeepsStillValidTempoAndRebuildsEnd) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 4)).ok());
  ASSERT_TRUE(timeline->set_tempo({make_point(Rational(0), 120)}).ok());

  ASSERT_TRUE(timeline->clear_pickdown().ok());
  EXPECT_FALSE(timeline->pickdown_duration().has_value());
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->end(), Rational(1));
}

TEST_F(TempoRevalidationTest,
       SetPickdownRejectedWhenShrinkingWouldInvalidateTempo) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 4)).ok());

  // node_end == 7/4; a point at 3/2 is covered.
  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120),
      make_point(*Rational::create(3, 2), 100),
  };
  ASSERT_TRUE(timeline->set_tempo(points).ok());

  // Shrinking the pickdown to 1/4 moves node_end to 5/4, which no longer
  // covers the tempo point at 3/2; the change must be rejected and the
  // timeline left unchanged.
  EXPECT_FALSE(timeline->set_pickdown(*Rational::create(1, 4)).ok());

  EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(3, 4));
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->points(), points);
  EXPECT_EQ(timeline->tempo()->end(), *Rational::create(7, 4));
}

TEST_F(TempoRevalidationTest, ClearPickdownRejectedWhenItWouldInvalidateTempo) {
  auto timeline = make_timeline();
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 4)).ok());

  const std::vector<TempoPoint> points = {
      make_point(Rational(0), 120),
      make_point(*Rational::create(3, 2), 100),
  };
  ASSERT_TRUE(timeline->set_tempo(points).ok());

  // Clearing the pickdown drops node_end to 1, which no longer covers the
  // tempo point at 3/2; the change must be rejected and the timeline left
  // unchanged.
  EXPECT_FALSE(timeline->clear_pickdown().ok());

  ASSERT_TRUE(timeline->pickdown_duration().has_value());
  EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(3, 4));
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->points(), points);
  EXPECT_EQ(timeline->tempo()->end(), *Rational::create(7, 4));
}

TEST(NodeTest, TimelineIsOptionalUntilExplicitlySet) {
  Node node(NodeId::generate(), "Node");
  EXPECT_FALSE(node.has_timeline());
  EXPECT_EQ(node.timeline(), nullptr);

  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  node.set_timeline(*timeline);

  EXPECT_TRUE(node.has_timeline());
  ASSERT_NE(node.timeline(), nullptr);
  EXPECT_EQ(node.timeline()->measures().measure_count(), 1u);

  node.clear_timeline();
  EXPECT_FALSE(node.has_timeline());
}
