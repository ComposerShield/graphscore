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

// Rebuilds a fresh MeasureMap from `map`'s own measures and asserts every
// derived accessor -- and the map itself -- exactly matches what create()
// would produce from that same measure vector. Used after every mutator
// under test to prove starts_/total_length_ never drift from a
// freshly-built map (Phase 8h-i requirement M5).
void ExpectMatchesFreshBuild(const MeasureMap& map) {
  std::vector<Measure> expected;
  expected.reserve(map.measure_count());
  for (std::size_t i = 0; i < map.measure_count(); ++i)
    expected.push_back(map.measure(i));

  const auto fresh = MeasureMap::create(expected);
  ASSERT_TRUE(fresh.has_value());
  EXPECT_EQ(map, *fresh);

  for (std::size_t i = 0; i < map.measure_count(); ++i) {
    EXPECT_EQ(map.measure_start(i), fresh->measure_start(i));
    EXPECT_EQ(map.measure_length(i), fresh->measure_length(i));
  }
  EXPECT_EQ(map.total_length(), fresh->total_length());

  const Rational total = map.total_length();
  const Rational step  = total / Rational(8);
  for (int i = -2; i <= 10; ++i) {
    const Rational probe = step * Rational(i);
    EXPECT_EQ(map.measure_index_at(probe), fresh->measure_index_at(probe));
  }
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

TEST(MeasureMapTest, InsertMeasureAtBeginningMiddleAndEnd) {
  auto map = MeasureMap::create(
      {make_measure(4, 4), make_measure(3, 4), make_measure(2, 4)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->insert_measure(0, make_measure(6, 8)).ok());
  EXPECT_EQ(map->measure_count(), 4u);
  EXPECT_EQ(map->measure(0).time_signature, *TimeSignature::create(6, 8));
  EXPECT_EQ(map->measure(1).time_signature, *TimeSignature::create(4, 4));
  ExpectMatchesFreshBuild(*map);

  ASSERT_TRUE(map->insert_measure(2, make_measure(5, 8)).ok());
  EXPECT_EQ(map->measure_count(), 5u);
  EXPECT_EQ(map->measure(2).time_signature, *TimeSignature::create(5, 8));
  EXPECT_EQ(map->measure(3).time_signature, *TimeSignature::create(3, 4));
  ExpectMatchesFreshBuild(*map);

  ASSERT_TRUE(
      map->insert_measure(map->measure_count(), make_measure(7, 8)).ok());
  EXPECT_EQ(map->measure_count(), 6u);
  EXPECT_EQ(map->measure(5).time_signature, *TimeSignature::create(7, 8));
  ExpectMatchesFreshBuild(*map);
}

TEST(MeasureMapTest, InsertMeasureRejectsOutOfRangeIndexAndLeavesMapUnchanged) {
  auto map = MeasureMap::create({make_measure(4, 4), make_measure(3, 4)});
  ASSERT_TRUE(map.has_value());
  const MeasureMap before = *map;

  EXPECT_FALSE(map->insert_measure(3, make_measure(2, 4)).ok());
  EXPECT_EQ(*map, before);
}

TEST(MeasureMapTest, InsertMeasureOverloadInheritsSignatureAtIndex) {
  auto map = MeasureMap::create({make_measure(4, 4, 0), make_measure(3, 4, 2)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->insert_measure(0).ok());
  EXPECT_EQ(map->measure_count(), 3u);
  EXPECT_EQ(map->measure(0).time_signature, *TimeSignature::create(4, 4));
  EXPECT_EQ(map->measure(0).key_signature, *KeySignature::create(0));
  EXPECT_EQ(map->measure(1).time_signature, *TimeSignature::create(4, 4));
  EXPECT_EQ(map->measure(2).time_signature, *TimeSignature::create(3, 4));
  ExpectMatchesFreshBuild(*map);
}

TEST(MeasureMapTest, InsertMeasureOverloadAppendInheritsLastMeasure) {
  auto map = MeasureMap::create({make_measure(4, 4, 0), make_measure(3, 4, 2)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->insert_measure(map->measure_count()).ok());
  EXPECT_EQ(map->measure_count(), 3u);
  EXPECT_EQ(map->measure(2).time_signature, *TimeSignature::create(3, 4));
  EXPECT_EQ(map->measure(2).key_signature, *KeySignature::create(2));
  ExpectMatchesFreshBuild(*map);
}

TEST(MeasureMapTest,
     InsertMeasureOverloadRejectsOutOfRangeIndexAndLeavesMapUnchanged) {
  auto map = MeasureMap::create({make_measure(4, 4)});
  ASSERT_TRUE(map.has_value());
  const MeasureMap before = *map;

  EXPECT_FALSE(map->insert_measure(2).ok());
  EXPECT_EQ(*map, before);
}

TEST(MeasureMapTest, RemoveMeasureShiftsLaterMeasuresAndUpdatesDerivedState) {
  auto map = MeasureMap::create(
      {make_measure(4, 4), make_measure(3, 4), make_measure(6, 8)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->remove_measure(0).ok());
  EXPECT_EQ(map->measure_count(), 2u);
  EXPECT_EQ(map->measure(0).time_signature, *TimeSignature::create(3, 4));
  EXPECT_EQ(map->measure_start(0), Rational(0));
  ExpectMatchesFreshBuild(*map);
}

TEST(MeasureMapTest, RemoveMeasureRejectsOutOfRangeIndexAndLeavesMapUnchanged) {
  auto map = MeasureMap::create({make_measure(4, 4), make_measure(3, 4)});
  ASSERT_TRUE(map.has_value());
  const MeasureMap before = *map;

  EXPECT_FALSE(map->remove_measure(2).ok());
  EXPECT_EQ(*map, before);
}

TEST(MeasureMapTest, RemoveMeasureRejectsRemovingTheOnlyRemainingMeasure) {
  auto map = MeasureMap::create({make_measure(4, 4)});
  ASSERT_TRUE(map.has_value());
  const MeasureMap before = *map;

  EXPECT_FALSE(map->remove_measure(0).ok());
  EXPECT_EQ(map->measure_count(), 1u);
  EXPECT_EQ(*map, before);
}

TEST(MeasureMapTest, RemoveMeasureAllowsShrinkingDownToOneMeasure) {
  auto map = MeasureMap::create({make_measure(4, 4), make_measure(3, 4)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->remove_measure(0).ok());
  EXPECT_EQ(map->measure_count(), 1u);
  EXPECT_FALSE(map->remove_measure(0).ok());
  EXPECT_EQ(map->measure_count(), 1u);
}

TEST(MeasureMapTest, SetMeasureReplacesSignatureInPlaceWithoutChangingCount) {
  auto map = MeasureMap::create({make_measure(4, 4), make_measure(3, 4)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->set_measure(0, make_measure(6, 8, 3)).ok());
  EXPECT_EQ(map->measure_count(), 2u);
  EXPECT_EQ(map->measure(0).time_signature, *TimeSignature::create(6, 8));
  EXPECT_EQ(map->measure(0).key_signature, *KeySignature::create(3));
  EXPECT_EQ(map->measure_start(1), *Rational::create(3, 4));
  ExpectMatchesFreshBuild(*map);
}

TEST(MeasureMapTest, SetMeasureRejectsOutOfRangeIndexAndLeavesMapUnchanged) {
  auto map = MeasureMap::create({make_measure(4, 4)});
  ASSERT_TRUE(map.has_value());
  const MeasureMap before = *map;

  EXPECT_FALSE(map->set_measure(1, make_measure(3, 4)).ok());
  EXPECT_EQ(*map, before);
}

TEST(MeasureMapTest,
     SetMeasureChangingTimeSignatureCascadesIntoLaterDerivedLengths) {
  auto map = MeasureMap::create(
      {make_measure(4, 4), make_measure(4, 4), make_measure(4, 4)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->set_measure(0, make_measure(2, 4)).ok());
  EXPECT_EQ(map->measure_start(0), Rational(0));
  EXPECT_EQ(map->measure_start(1), *Rational::create(2, 4));
  EXPECT_EQ(map->measure_start(2), *Rational::create(6, 4));
  EXPECT_EQ(map->total_length(), *Rational::create(10, 4));
  ExpectMatchesFreshBuild(*map);
}

TEST(MeasureMapTest, DerivedStateFidelityAcrossMutationSequence) {
  auto map =
      MeasureMap::create({make_measure(4, 4, 0), make_measure(3, 4, 1),
                          make_measure(6, 8, -2), make_measure(2, 4, 3)});
  ASSERT_TRUE(map.has_value());

  ASSERT_TRUE(map->insert_measure(2, make_measure(5, 8, 4)).ok());
  ExpectMatchesFreshBuild(*map);

  ASSERT_TRUE(map->remove_measure(0).ok());
  ExpectMatchesFreshBuild(*map);

  ASSERT_TRUE(map->set_measure(1, make_measure(7, 8, -5)).ok());
  ExpectMatchesFreshBuild(*map);

  ASSERT_TRUE(map->insert_measure(map->measure_count()).ok());
  ExpectMatchesFreshBuild(*map);
}
