// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::KeySignature;
using graphscore::map_vertical_position;
using graphscore::Measure;
using graphscore::NodeTimeline;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::Tempo;
using graphscore::TempoPoint;
using graphscore::TimeSignature;
using graphscore::vertical_regions_compatible;

namespace {

Measure make_measure(std::uint8_t numerator, std::uint16_t denominator,
                     std::int8_t key = 0) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 *KeySignature::create(key)};
}

TempoPoint make_point(Rational position, std::int64_t bpm) {
  return TempoPoint{position,
                    *Tempo::create(Rational(bpm), NoteValue::kQuarter)};
}

}  // namespace

// -- vertical_regions_compatible -----------------------------------------

TEST(VerticalTransitionTest, IdenticalMeasureCountsAndSignaturesAreCompatible) {
  const auto source =
      NodeTimeline::create({make_measure(4, 4), make_measure(3, 4)}, {});
  const auto destination =
      NodeTimeline::create({make_measure(4, 4), make_measure(3, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  EXPECT_TRUE(vertical_regions_compatible(*source, *destination));
}

TEST(VerticalTransitionTest, DifferingMeasureCountsAreIncompatible) {
  const auto source = NodeTimeline::create({make_measure(4, 4)}, {});
  const auto destination =
      NodeTimeline::create({make_measure(4, 4), make_measure(4, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  EXPECT_FALSE(vertical_regions_compatible(*source, *destination));
}

TEST(VerticalTransitionTest, DifferingTimeSignaturesAreIncompatible) {
  const auto source      = NodeTimeline::create({make_measure(4, 4)}, {});
  const auto destination = NodeTimeline::create({make_measure(3, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  EXPECT_FALSE(vertical_regions_compatible(*source, *destination));
}

TEST(VerticalTransitionTest, DifferingKeySignaturesDoNotAffectCompatibility) {
  const auto source = NodeTimeline::create({make_measure(4, 4, /*key=*/0)}, {});
  const auto destination =
      NodeTimeline::create({make_measure(4, 4, /*key=*/3)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  EXPECT_TRUE(vertical_regions_compatible(*source, *destination));
}

TEST(VerticalTransitionTest, PickdownDifferencesDoNotAffectCompatibility) {
  auto source      = NodeTimeline::create({make_measure(4, 4)}, {});
  auto destination = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  ASSERT_TRUE(source->set_pickdown(*Rational::create(1, 4)).ok());
  // destination has no pickdown at all.

  EXPECT_TRUE(vertical_regions_compatible(*source, *destination));
}

// -- map_vertical_position -------------------------------------------------

TEST(VerticalTransitionTest, MapsToTheSameMeasureAndExactRationalOffset) {
  auto source =
      NodeTimeline::create({make_measure(4, 4), make_measure(3, 4)}, {});
  auto destination =
      NodeTimeline::create({make_measure(4, 4), make_measure(3, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  // Second measure (index 1) starts at whole-note position 1 (one measure of
  // 4/4). An eighth note into that measure is 1 + 1/8.
  const Rational position = Rational(1) + *Rational::create(1, 8);
  const auto mapped = map_vertical_position(*source, *destination, position);
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(*mapped, position);
  EXPECT_EQ(destination->measures().measure_index_at(*mapped), 1u);
}

TEST(VerticalTransitionTest, MappingFailsWhenTimelinesAreIncompatible) {
  auto source      = NodeTimeline::create({make_measure(4, 4)}, {});
  auto destination = NodeTimeline::create({make_measure(3, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  EXPECT_FALSE(
      map_vertical_position(*source, *destination, Rational(0)).has_value());
}

TEST(VerticalTransitionTest,
     MappingFailsAtTheMainRegionBoundaryRegardlessOfPickdownPresence) {
  auto source      = NodeTimeline::create({make_measure(4, 4)}, {});
  auto destination = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());
  ASSERT_TRUE(source->set_pickdown(*Rational::create(1, 4)).ok());

  // Position 1 is the main-region boundary -- the start of source's
  // pickdown, not a main-region position. This failure comes from
  // MeasureMap::measure_index_at() simply having no measure containing
  // position 1 -- set_pickdown() above does not change that determination
  // at all (a source with no pickdown fails identically at this same
  // position), so this test also constructs a no-pickdown source and pins
  // that the same boundary position fails identically whether or not a
  // pickdown happens to exist there.
  EXPECT_FALSE(
      map_vertical_position(*source, *destination, Rational(1)).has_value());

  auto source_no_pickdown = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(source_no_pickdown.has_value());

  EXPECT_FALSE(
      map_vertical_position(*source_no_pickdown, *destination, Rational(1))
          .has_value());
}

TEST(VerticalTransitionTest, MappingFailsForAnOutOfRangePosition) {
  auto source      = NodeTimeline::create({make_measure(4, 4)}, {});
  auto destination = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  EXPECT_FALSE(
      map_vertical_position(*source, *destination, Rational(-1)).has_value());
}

TEST(VerticalTransitionTest,
     MappedPositionIsUnaffectedByTempoAndDestinationReadsItsOwnCurve) {
  auto source      = NodeTimeline::create({make_measure(4, 4)}, {});
  auto destination = NodeTimeline::create({make_measure(4, 4)}, {});
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(destination.has_value());

  ASSERT_TRUE(source->set_tempo({make_point(Rational(0), 60)}).ok());
  ASSERT_TRUE(destination->set_tempo({make_point(Rational(0), 180)}).ok());

  const auto mapped = map_vertical_position(*source, *destination, Rational(0));
  ASSERT_TRUE(mapped.has_value());
  // map_vertical_position operates purely on MeasureMap/Rational structure
  // -- source and destination carrying entirely different tempo curves has
  // no bearing on the position it computes.
  EXPECT_EQ(*mapped, Rational(0));

  // "The destination begins using its own tempo curve at the mapped
  // position" is realized entirely by NodeTimeline never sharing one
  // TempoLane between nodes: reading destination->tempo() at *mapped yields
  // destination's own, independent curve, unaffected by source's.
  ASSERT_NE(destination->tempo(), nullptr);
  ASSERT_NE(source->tempo(), nullptr);
  EXPECT_NE(destination->tempo()->points().front().tempo,
            source->tempo()->points().front().tempo);
  EXPECT_EQ(destination->tempo()->points().front().tempo.bpm(), Rational(180));
}
