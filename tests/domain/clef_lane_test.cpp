// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Clef;
using graphscore::ClefLane;
using graphscore::Rational;

TEST(ClefLaneTest, DefaultClefAppliesWithNoChanges) {
  const ClefLane lane(Clef::kTreble);
  EXPECT_EQ(lane.default_clef(), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(Rational(0)), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(Rational(100)), Clef::kTreble);
}

TEST(ClefLaneTest, ChangeAppliesFromItsExactPositionOnward) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());

  EXPECT_EQ(lane.clef_at(Rational(0)), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(*Rational::create(15, 4)), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kBass);
  EXPECT_EQ(lane.clef_at(Rational(10)), Clef::kBass);
}

TEST(ClefLaneTest, ChangesApplyInOrderRegardlessOfInsertionOrder) {
  ClefLane lane(Clef::kBass);
  ASSERT_TRUE(lane.add_change(Rational(8), Clef::kTenor).ok());
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kAlto).ok());

  ASSERT_EQ(lane.changes().size(), 2u);
  EXPECT_EQ(lane.changes()[0].position, Rational(4));
  EXPECT_EQ(lane.changes()[1].position, Rational(8));

  EXPECT_EQ(lane.clef_at(Rational(0)), Clef::kBass);
  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kAlto);
  EXPECT_EQ(lane.clef_at(Rational(8)), Clef::kTenor);
}

TEST(ClefLaneTest, RejectsNegativePositionAndDuplicatePosition) {
  ClefLane lane(Clef::kTreble);
  EXPECT_FALSE(lane.add_change(Rational(-1), Clef::kBass).ok());

  ASSERT_TRUE(lane.add_change(Rational(2), Clef::kBass).ok());
  EXPECT_FALSE(lane.add_change(Rational(2), Clef::kAlto).ok());
  EXPECT_EQ(lane.changes().size(), 1u);
}

TEST(ClefLaneTest, GrandStaffStavesHaveIndependentClefLanes) {
  ClefLane treble_lane(Clef::kTreble);
  ClefLane bass_lane(Clef::kBass);

  ASSERT_TRUE(treble_lane.add_change(Rational(4), Clef::kAlto).ok());

  EXPECT_EQ(treble_lane.clef_at(Rational(4)), Clef::kAlto);
  EXPECT_EQ(bass_lane.clef_at(Rational(4)), Clef::kBass);
  EXPECT_TRUE(bass_lane.changes().empty());
}
