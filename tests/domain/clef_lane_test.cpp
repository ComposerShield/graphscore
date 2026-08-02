// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Clef;
using graphscore::ClefChange;
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
  ClefLane       treble_lane(Clef::kTreble);
  const ClefLane bass_lane(Clef::kBass);

  ASSERT_TRUE(treble_lane.add_change(Rational(4), Clef::kAlto).ok());

  EXPECT_EQ(treble_lane.clef_at(Rational(4)), Clef::kAlto);
  EXPECT_EQ(bass_lane.clef_at(Rational(4)), Clef::kBass);
  EXPECT_TRUE(bass_lane.changes().empty());
}

TEST(ClefLaneTest, RemoveChangeRemovesExactPositionAndAffectsClefAt) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());
  ASSERT_TRUE(lane.add_change(Rational(8), Clef::kAlto).ok());

  ASSERT_TRUE(lane.remove_change(Rational(4)).ok());
  ASSERT_EQ(lane.changes().size(), 1u);
  EXPECT_EQ(lane.changes()[0].position, Rational(8));

  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(Rational(8)), Clef::kAlto);
}

TEST(ClefLaneTest, RemoveChangeMissingPositionFailsAndLeavesLaneUnchanged) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());
  const ClefLane before = lane;

  EXPECT_FALSE(lane.remove_change(Rational(5)).ok());
  EXPECT_EQ(lane, before);
}

TEST(ClefLaneTest, MoveChangeRelocatesPreservingClefAndSortedOrder) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());
  ASSERT_TRUE(lane.add_change(Rational(8), Clef::kAlto).ok());

  ASSERT_TRUE(lane.move_change(Rational(4), Rational(2)).ok());
  ASSERT_EQ(lane.changes().size(), 2u);
  EXPECT_EQ(lane.changes()[0].position, Rational(2));
  EXPECT_EQ(lane.changes()[0].clef, Clef::kBass);
  EXPECT_EQ(lane.changes()[1].position, Rational(8));

  EXPECT_EQ(lane.clef_at(Rational(1)), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(Rational(2)), Clef::kBass);
  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kBass);
}

TEST(ClefLaneTest, MoveChangeToItsOwnPositionSucceedsAsANoOp) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());

  ASSERT_TRUE(lane.move_change(Rational(4), Rational(4)).ok());
  ASSERT_EQ(lane.changes().size(), 1u);
  EXPECT_EQ(lane.changes()[0].position, Rational(4));
  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kBass);
}

TEST(ClefLaneTest,
     MoveChangeFailsWhenSourceMissingDestinationNegativeOrOccupied) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());
  ASSERT_TRUE(lane.add_change(Rational(8), Clef::kAlto).ok());
  const ClefLane before = lane;

  EXPECT_FALSE(lane.move_change(Rational(5), Rational(6)).ok());
  EXPECT_EQ(lane, before);

  EXPECT_FALSE(lane.move_change(Rational(4), Rational(-1)).ok());
  EXPECT_EQ(lane, before);

  EXPECT_FALSE(lane.move_change(Rational(4), Rational(8)).ok());
  EXPECT_EQ(lane, before);
}

TEST(ClefLaneTest, SetChangeReplacesClefAtExactPosition) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());

  ASSERT_TRUE(lane.set_change(Rational(4), Clef::kTenor).ok());
  ASSERT_EQ(lane.changes().size(), 1u);
  EXPECT_EQ(lane.changes()[0].position, Rational(4));
  EXPECT_EQ(lane.changes()[0].clef, Clef::kTenor);
  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kTenor);
}

TEST(ClefLaneTest, SetChangeMissingPositionFailsAndLeavesLaneUnchanged) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());
  const ClefLane before = lane;

  EXPECT_FALSE(lane.set_change(Rational(5), Clef::kAlto).ok());
  EXPECT_EQ(lane, before);
}

TEST(ClefLaneTest, SetChangeIsDistinctFromAddChange) {
  ClefLane lane(Clef::kTreble);

  EXPECT_FALSE(lane.set_change(Rational(4), Clef::kBass).ok());
  EXPECT_TRUE(lane.changes().empty());

  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());
  EXPECT_FALSE(lane.add_change(Rational(4), Clef::kAlto).ok());

  ASSERT_TRUE(lane.set_change(Rational(4), Clef::kAlto).ok());
  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kAlto);
}

TEST(ClefLaneTest, NegativePositionsAlwaysFailAcrossEveryMutator) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kBass).ok());

  EXPECT_FALSE(lane.remove_change(Rational(-1)).ok());
  EXPECT_FALSE(lane.set_change(Rational(-1), Clef::kAlto).ok());
  EXPECT_FALSE(lane.move_change(Rational(4), Rational(-1)).ok());
  EXPECT_FALSE(lane.move_change(Rational(-1), Rational(2)).ok());
  EXPECT_EQ(lane.changes().size(), 1u);
}

TEST(ClefLaneTest, OrderingAndClefAtInvariantsHoldAfterMutationSequence) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(2), Clef::kBass).ok());
  ASSERT_TRUE(lane.add_change(Rational(6), Clef::kAlto).ok());
  ASSERT_TRUE(lane.add_change(Rational(10), Clef::kTenor).ok());

  ASSERT_TRUE(lane.move_change(Rational(6), Rational(4)).ok());
  ASSERT_TRUE(lane.remove_change(Rational(10)).ok());
  ASSERT_TRUE(lane.set_change(Rational(2), Clef::kTenor).ok());
  ASSERT_TRUE(lane.add_change(Rational(8), Clef::kBass).ok());

  ASSERT_EQ(lane.changes().size(), 3u);
  EXPECT_TRUE(std::is_sorted(lane.changes().begin(), lane.changes().end(),
                             [](const ClefChange& a, const ClefChange& b) {
                               return a.position < b.position;
                             }));
  EXPECT_EQ(lane.changes()[0].position, Rational(2));
  EXPECT_EQ(lane.changes()[1].position, Rational(4));
  EXPECT_EQ(lane.changes()[2].position, Rational(8));

  EXPECT_EQ(lane.clef_at(Rational(0)), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(Rational(2)), Clef::kTenor);
  EXPECT_EQ(lane.clef_at(Rational(3)), Clef::kTenor);
  EXPECT_EQ(lane.clef_at(Rational(4)), Clef::kAlto);
  EXPECT_EQ(lane.clef_at(Rational(8)), Clef::kBass);
  EXPECT_EQ(lane.clef_at(Rational(100)), Clef::kBass);
}

TEST(ClefLaneTest, MoveChangeForwardPastAnotherChangeReindexesCorrectly) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(2), Clef::kAlto).ok());
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kTenor).ok());
  ASSERT_TRUE(lane.add_change(Rational(8), Clef::kBass).ok());

  ASSERT_TRUE(lane.move_change(Rational(2), Rational(6)).ok());
  ASSERT_EQ(lane.changes().size(), 3u);
  EXPECT_EQ(lane.changes()[0].position, Rational(4));
  EXPECT_EQ(lane.changes()[0].clef, Clef::kTenor);
  EXPECT_EQ(lane.changes()[1].position, Rational(6));
  EXPECT_EQ(lane.changes()[1].clef, Clef::kAlto);
  EXPECT_EQ(lane.changes()[2].position, Rational(8));
  EXPECT_EQ(lane.changes()[2].clef, Clef::kBass);

  EXPECT_EQ(lane.clef_at(Rational(3)), Clef::kTreble);
  EXPECT_EQ(lane.clef_at(Rational(5)), Clef::kTenor);
  EXPECT_EQ(lane.clef_at(Rational(6)), Clef::kAlto);
}

TEST(ClefLaneTest, MoveChangeBackwardPastAnotherChangeReindexesCorrectly) {
  ClefLane lane(Clef::kTreble);
  ASSERT_TRUE(lane.add_change(Rational(2), Clef::kAlto).ok());
  ASSERT_TRUE(lane.add_change(Rational(4), Clef::kTenor).ok());
  ASSERT_TRUE(lane.add_change(Rational(8), Clef::kBass).ok());

  ASSERT_TRUE(lane.move_change(Rational(8), Rational(3)).ok());
  ASSERT_EQ(lane.changes().size(), 3u);
  EXPECT_EQ(lane.changes()[0].position, Rational(2));
  EXPECT_EQ(lane.changes()[0].clef, Clef::kAlto);
  EXPECT_EQ(lane.changes()[1].position, Rational(3));
  EXPECT_EQ(lane.changes()[1].clef, Clef::kBass);
  EXPECT_EQ(lane.changes()[2].position, Rational(4));
  EXPECT_EQ(lane.changes()[2].clef, Clef::kTenor);
}
