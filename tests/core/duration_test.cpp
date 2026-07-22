// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::Duration;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::TupletRatio;

TEST(TupletRatioTest, ThreeTwoTripletIsValid) {
  const auto ratio = TupletRatio::create(3, 2);
  ASSERT_TRUE(ratio.has_value());
  EXPECT_EQ(ratio->played(), 3);
  EXPECT_EQ(ratio->normal(), 2);
  EXPECT_EQ(ratio->factor(), *Rational::create(2, 3));
}

TEST(TupletRatioTest, ZeroPlayedIsInvalid) {
  EXPECT_FALSE(TupletRatio::create(0, 2).has_value());
}

TEST(TupletRatioTest, ZeroNormalIsInvalid) {
  EXPECT_FALSE(TupletRatio::create(3, 0).has_value());
}

TEST(TupletRatioTest, ArbitraryQuintupletIsValid) {
  const auto ratio = TupletRatio::create(5, 4);
  ASSERT_TRUE(ratio.has_value());
  EXPECT_EQ(ratio->factor(), *Rational::create(4, 5));
}

struct DurationCase {
  NoteValue    base;
  std::uint8_t dots;
  Rational     expected;
};

class DurationTableTest : public testing::TestWithParam<DurationCase> {};

TEST_P(DurationTableTest, ResolvesToExactRational) {
  const auto duration = Duration::create(GetParam().base, GetParam().dots);
  ASSERT_TRUE(duration.has_value());
  EXPECT_EQ(duration->resolved(), GetParam().expected);
}

INSTANTIATE_TEST_SUITE_P(
    AllBaseValuesUndotted, DurationTableTest,
    testing::Values(
        DurationCase{NoteValue::kWhole, 0, Rational(1)},
        DurationCase{NoteValue::kHalf, 0, *Rational::create(1, 2)},
        DurationCase{NoteValue::kQuarter, 0, *Rational::create(1, 4)},
        DurationCase{NoteValue::kEighth, 0, *Rational::create(1, 8)},
        DurationCase{NoteValue::kSixteenth, 0, *Rational::create(1, 16)},
        DurationCase{NoteValue::kThirtySecond, 0, *Rational::create(1, 32)},
        DurationCase{NoteValue::kSixtyFourth, 0, *Rational::create(1, 64)}));

INSTANTIATE_TEST_SUITE_P(
    DotsOnQuarterNote, DurationTableTest,
    testing::Values(
        DurationCase{NoteValue::kQuarter, 0, *Rational::create(1, 4)},
        DurationCase{NoteValue::kQuarter, 1, *Rational::create(3, 8)},
        DurationCase{NoteValue::kQuarter, 2, *Rational::create(7, 16)}));

INSTANTIATE_TEST_SUITE_P(
    DotsOnHalfNote, DurationTableTest,
    testing::Values(DurationCase{NoteValue::kHalf, 1, *Rational::create(3, 4)},
                    DurationCase{NoteValue::kHalf, 2,
                                 *Rational::create(7, 8)}));

TEST(DurationTest, ThreeDotsIsInvalid) {
  EXPECT_FALSE(Duration::create(NoteValue::kQuarter, 3).has_value());
}

TEST(DurationTest, TripletEighthResolvesToOneTwelfth) {
  const auto duration =
      Duration::create(NoteValue::kEighth, 0, TupletRatio::create(3, 2));
  ASSERT_TRUE(duration.has_value());
  EXPECT_EQ(duration->resolved(), *Rational::create(1, 12));
}

TEST(DurationTest, QuintupletSixteenthResolvesExactly) {
  const auto duration =
      Duration::create(NoteValue::kSixteenth, 0, TupletRatio::create(5, 4));
  ASSERT_TRUE(duration.has_value());
  EXPECT_EQ(duration->resolved(), *Rational::create(1, 20));
}

TEST(DurationTest, SeptupletEighthResolvesExactly) {
  // A 7:8 septuplet packs 7 eighth notes into the space normally occupied
  // by 8, so each one shrinks to (1/8) * (8/7) == 1/7.
  const auto duration =
      Duration::create(NoteValue::kEighth, 0, TupletRatio::create(7, 8));
  ASSERT_TRUE(duration.has_value());
  EXPECT_EQ(duration->resolved(), *Rational::create(1, 7));
}

TEST(DurationTest, DottedEighthTripletCombinesDotsAndTuplet) {
  // A dotted eighth is 3/16 of a whole note; as the middle note of a
  // triplet (3:2) it resolves to (3/16) * (2/3) == 1/8.
  const auto duration =
      Duration::create(NoteValue::kEighth, 1, TupletRatio::create(3, 2));
  ASSERT_TRUE(duration.has_value());
  EXPECT_EQ(duration->resolved(), *Rational::create(1, 8));
}

TEST(DurationTest, NoTupletMeansPlainDuration) {
  const auto duration = Duration::create(NoteValue::kQuarter, 0);
  ASSERT_TRUE(duration.has_value());
  EXPECT_FALSE(duration->tuplet().has_value());
}

TEST(DurationTest, DefaultIsUndottedQuarter) {
  const Duration duration;
  EXPECT_EQ(duration.base(), NoteValue::kQuarter);
  EXPECT_EQ(duration.dots(), 0);
  EXPECT_EQ(duration.resolved(), *Rational::create(1, 4));
}
