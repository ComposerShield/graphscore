// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::KeySignature;
using graphscore::Measure;
using graphscore::MeasureMap;
using graphscore::Rational;
using graphscore::TimeSignature;

namespace {

Measure make_measure(std::uint8_t numerator, std::uint16_t denominator,
                     std::int8_t fifths = 0) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 *KeySignature::create(fifths)};
}

}  // namespace

TEST(MeasureMapTest, RejectsEmptyMainRegion) {
  EXPECT_FALSE(MeasureMap::create({}).has_value());
}

TEST(MeasureMapTest, AcceptsSingleMeasure) {
  const auto map = MeasureMap::create({make_measure(4, 4)});
  ASSERT_TRUE(map.has_value());
  EXPECT_EQ(map->measure_count(), 1u);
  EXPECT_EQ(map->measure_start(0), Rational(0));
  EXPECT_EQ(map->total_length(), Rational(1));
}

TEST(MeasureMapTest, AcceptsSeveralMeasuresWithPerMeasureSignatureChanges) {
  const auto map = MeasureMap::create({
      make_measure(4, 4, 0),
      make_measure(3, 4, 2),
      make_measure(6, 8, -3),
  });
  ASSERT_TRUE(map.has_value());
  EXPECT_EQ(map->measure_count(), 3u);

  EXPECT_EQ(map->measure(0).time_signature, *TimeSignature::create(4, 4));
  EXPECT_EQ(map->measure(1).time_signature, *TimeSignature::create(3, 4));
  EXPECT_EQ(map->measure(2).time_signature, *TimeSignature::create(6, 8));

  EXPECT_EQ(map->measure(0).key_signature, *KeySignature::create(0));
  EXPECT_EQ(map->measure(1).key_signature, *KeySignature::create(2));
  EXPECT_EQ(map->measure(2).key_signature, *KeySignature::create(-3));

  EXPECT_EQ(map->measure_start(0), Rational(0));
  EXPECT_EQ(map->measure_start(1), Rational(1));
  EXPECT_EQ(map->measure_start(2), *Rational::create(7, 4));
  EXPECT_EQ(map->total_length(),
            *Rational::create(7, 4) + *Rational::create(3, 4));
}

TEST(MeasureMapTest, MeasureIndexAtMapsPositionsBackToMeasures) {
  const auto map = MeasureMap::create({
      make_measure(4, 4),
      make_measure(3, 4),
      make_measure(4, 4),
  });
  ASSERT_TRUE(map.has_value());

  EXPECT_EQ(map->measure_index_at(Rational(0)), 0u);
  EXPECT_EQ(map->measure_index_at(*Rational::create(1, 2)), 0u);
  EXPECT_EQ(map->measure_index_at(Rational(1)), 1u);
  EXPECT_EQ(map->measure_index_at(*Rational::create(7, 4)), 2u);
  EXPECT_FALSE(map->measure_index_at(map->total_length()).has_value());
  EXPECT_FALSE(map->measure_index_at(Rational(-1)).has_value());
}

TEST(MeasureMapTest, RepresentativeSixtyFourMeasureNode) {
  std::vector<Measure> measures;
  measures.reserve(64);
  for (int i = 0; i < 64; ++i) {
    measures.push_back(make_measure(4, 4));
  }

  const auto map = MeasureMap::create(measures);
  ASSERT_TRUE(map.has_value());
  EXPECT_EQ(map->measure_count(), 64u);
  EXPECT_EQ(map->measure_start(63), Rational(63));
  EXPECT_EQ(map->total_length(), Rational(64));
  EXPECT_EQ(map->measure_index_at(Rational(63)), 63u);
}

struct MeterCase {
  std::uint8_t  numerator;
  std::uint16_t denominator;
  Rational      expected_length;
};

class MeterLengthTest : public ::testing::TestWithParam<MeterCase> {};

TEST_P(MeterLengthTest, MeasureLengthIsExactWholeNoteFraction) {
  const MeterCase& test_case = GetParam();
  const auto       map       = MeasureMap::create(
      {make_measure(test_case.numerator, test_case.denominator)});
  ASSERT_TRUE(map.has_value());
  EXPECT_EQ(map->measure_length(0), test_case.expected_length);
}

INSTANTIATE_TEST_SUITE_P(
    StandardMeters, MeterLengthTest,
    ::testing::Values(MeterCase{4, 4, Rational(1)},
                      MeterCase{3, 4, *Rational::create(3, 4)},
                      MeterCase{6, 8, *Rational::create(3, 4)},
                      MeterCase{7, 8, *Rational::create(7, 8)},
                      MeterCase{2, 2, Rational(1)}));
