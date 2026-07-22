// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ConnectorId;
using graphscore::DeterministicPrng;
using graphscore::Rational;
using graphscore::select_weighted_random;
using graphscore::validate_weight_group;
using graphscore::weight_group_roll_bound;
using graphscore::WeightedCandidate;
using graphscore::WeightGroupValidity;

// -- validate_weight_group ---------------------------------------------

TEST(WeightedSelectionTest, EmptyGroupHasNoEligibleCandidates) {
  EXPECT_EQ(validate_weight_group({}),
            WeightGroupValidity::kNoEligibleCandidates);
}

TEST(WeightedSelectionTest, AllZeroWeightGroupHasNoEligibleCandidates) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), Rational(0)},
      WeightedCandidate{ConnectorId::generate(), Rational(0)},
  };
  EXPECT_EQ(validate_weight_group(group),
            WeightGroupValidity::kNoEligibleCandidates);
}

TEST(WeightedSelectionTest, TotalOfExactlyOneIsValid) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(2, 5)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(3, 5)},
  };
  EXPECT_EQ(validate_weight_group(group), WeightGroupValidity::kValid);
}

TEST(WeightedSelectionTest, ExactThreeWaySplitOfOneThirdEachIsValid) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
  };
  EXPECT_EQ(validate_weight_group(group), WeightGroupValidity::kValid);
}

TEST(WeightedSelectionTest, MixedDenominatorsSummingToExactlyOneAreValid) {
  // 1/2 + 1/3 + 1/6 == 1, over a common denominator of 6.
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 2)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 6)},
  };
  EXPECT_EQ(validate_weight_group(group), WeightGroupValidity::kValid);
}

TEST(WeightedSelectionTest, TotalSlightlyUnderOneIsRejected) {
  // 1/3 + 1/3 + 1/4 == 11/12, short of 1.
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 4)},
  };
  EXPECT_EQ(validate_weight_group(group), WeightGroupValidity::kInvalidTotal);
}

TEST(WeightedSelectionTest, TotalSlightlyOverOneIsRejected) {
  // 1/3 + 1/3 + 1/2 == 7/6, over 1.
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 2)},
  };
  EXPECT_EQ(validate_weight_group(group), WeightGroupValidity::kInvalidTotal);
}

TEST(WeightedSelectionTest,
     ANegativeWeightIsInvalidEvenWhenTheGroupWouldOtherwiseSumToExactlyOne) {
  // -1/3 + 1/3 + 1/3 + 2/3 == 1 arithmetically, but a negative weight must
  // never be able to cross-cancel a malformed group into reading kValid.
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(-1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(2, 3)},
  };
  EXPECT_EQ(validate_weight_group(group), WeightGroupValidity::kInvalidTotal);
}

TEST(WeightedSelectionTest,
     AnOutOfRangeWholeWeightAloneIsInvalidNotClampedOrTruncated) {
  // Rational(2) is non-negative but out of [0, 1] (connector.hpp's
  // documented out-of-range-storage contract: OutputConnector::set_weight
  // permits it verbatim, and this header simply fails the enclosing group's
  // exact-1 check rather than clamping or truncating it).
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), Rational(2)},
  };
  EXPECT_EQ(validate_weight_group(group), WeightGroupValidity::kInvalidTotal);
}

TEST(WeightedSelectionTest,
     PairwiseCoprimeLargeDenominatorsOverflowTheCommonDenominator) {
  // 5,000,000,000 and 5,000,000,001 are consecutive (hence coprime)
  // integers whose product, ~2.5e19, exceeds what a 64-bit unsigned
  // integer can represent (~1.8e19), so their LCM overflows exactly the
  // way this group's common-denominator construction must detect.
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(),
                        *Rational::create(1, 5'000'000'000)},
      WeightedCandidate{ConnectorId::generate(),
                        *Rational::create(1, 5'000'000'001)},
  };
  EXPECT_EQ(validate_weight_group(group),
            WeightGroupValidity::kDenominatorOverflow);
}

TEST(WeightedSelectionTest,
     ALargeNumeratorTimesTheCommonDenominatorFactorOverflowsAShare) {
  // The LCM fold itself never overflows here (1 then 8), but 2^62's share of
  // that common denominator, 2^62 * 8 == 2^65, does not fit in 64 bits --
  // this exercises the share-multiply check (weighted_selection.cpp:84),
  // distinct from the LCM-fold check covered above.
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(),
                        Rational(std::int64_t{1} << 62)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 8)},
  };
  EXPECT_EQ(validate_weight_group(group),
            WeightGroupValidity::kDenominatorOverflow);
}

TEST(WeightedSelectionTest,
     ThreeMaximalWholeWeightsOverflowTheRunningSumButNotAnyShare) {
  // Each share individually (INT64_MAX * 1) fits in 64 bits, and even the
  // first two summed (2 * INT64_MAX == 2^64 - 2) still fit, but the third
  // addition pushes the running total past what 64 bits can represent --
  // this exercises the running-sum check (weighted_selection.cpp:89),
  // distinct from both checks covered above.
  constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), Rational(kMax)},
      WeightedCandidate{ConnectorId::generate(), Rational(kMax)},
      WeightedCandidate{ConnectorId::generate(), Rational(kMax)},
  };
  EXPECT_EQ(validate_weight_group(group),
            WeightGroupValidity::kDenominatorOverflow);
}

// -- weight_group_roll_bound ----------------------------------------------

TEST(WeightedSelectionTest, RollBoundIsTheGroupsCommonDenominatorWhenValid) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 2)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 6)},
  };
  EXPECT_EQ(weight_group_roll_bound(group), 6u);
}

TEST(WeightedSelectionTest, RollBoundIsNulloptForAnInvalidGroup) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 4)},
  };
  EXPECT_FALSE(weight_group_roll_bound(group).has_value());
}

TEST(WeightedSelectionTest, RollBoundIsNulloptOnDenominatorOverflow) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(),
                        *Rational::create(1, 5'000'000'000)},
      WeightedCandidate{ConnectorId::generate(),
                        *Rational::create(1, 5'000'000'001)},
  };
  EXPECT_FALSE(weight_group_roll_bound(group).has_value());
}

// -- select_weighted_random ----------------------------------------------

TEST(WeightedSelectionTest, InvalidTotalNeverSelectsRegardlessOfRoll) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 4)},
  };
  for (std::uint64_t roll = 0; roll < 12; ++roll) {
    EXPECT_FALSE(select_weighted_random(group, roll).has_value());
  }
}

TEST(WeightedSelectionTest, AllZeroWeightGroupNeverSelects) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), Rational(0)},
      WeightedCandidate{ConnectorId::generate(), Rational(0)},
  };
  for (std::uint64_t roll = 0; roll < 4; ++roll) {
    EXPECT_FALSE(select_weighted_random(group, roll).has_value());
  }
}

TEST(WeightedSelectionTest,
     ANegativeWeightGroupNeverSelectsEvenWhenItWouldOtherwiseSumToOne) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(), *Rational::create(-1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(1, 3)},
      WeightedCandidate{ConnectorId::generate(), *Rational::create(2, 3)},
  };
  for (std::uint64_t roll = 0; roll < 4; ++roll) {
    EXPECT_FALSE(select_weighted_random(group, roll).has_value());
  }
}

TEST(WeightedSelectionTest,
     DenominatorOverflowingGroupNeverSelectsRegardlessOfRoll) {
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{ConnectorId::generate(),
                        *Rational::create(1, 5'000'000'000)},
      WeightedCandidate{ConnectorId::generate(),
                        *Rational::create(1, 5'000'000'001)},
  };
  for (std::uint64_t roll = 0; roll < 4; ++roll) {
    EXPECT_FALSE(select_weighted_random(group, roll).has_value());
  }
}

TEST(WeightedSelectionTest, SoleCandidateAtWeightOneAlwaysWins) {
  const ConnectorId                    only  = ConnectorId::generate();
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{only, Rational(1)},
  };
  EXPECT_EQ(weight_group_roll_bound(group), 1u);
  EXPECT_EQ(select_weighted_random(group, 0), only);
}

TEST(WeightedSelectionTest, RollsPartitionExactlyAtTheWeightBoundary) {
  const ConnectorId                    first  = ConnectorId::generate();
  const ConnectorId                    second = ConnectorId::generate();
  const std::vector<WeightedCandidate> group  = {
      WeightedCandidate{first, *Rational::create(3, 10)},
      WeightedCandidate{second, *Rational::create(7, 10)},
  };
  ASSERT_EQ(weight_group_roll_bound(group), 10u);

  EXPECT_EQ(select_weighted_random(group, 0), first);
  EXPECT_EQ(select_weighted_random(group, 2), first);
  EXPECT_EQ(select_weighted_random(group, 3), second);
  EXPECT_EQ(select_weighted_random(group, 9), second);
}

TEST(WeightedSelectionTest,
     ZeroWeightCandidateIsNeverSelectedEvenAsTheOnlyRemainingCandidate) {
  const ConnectorId                    winner = ConnectorId::generate();
  const ConnectorId                    zero   = ConnectorId::generate();
  const std::vector<WeightedCandidate> group  = {
      WeightedCandidate{winner, Rational(1)},
      WeightedCandidate{zero, Rational(0)},
  };
  ASSERT_EQ(weight_group_roll_bound(group), 1u);
  EXPECT_EQ(select_weighted_random(group, 0), winner);
}

TEST(WeightedSelectionTest,
     ZeroWeightCandidateBetweenTwoOthersOccupiesNoBucket) {
  const ConnectorId                    first  = ConnectorId::generate();
  const ConnectorId                    zero   = ConnectorId::generate();
  const ConnectorId                    second = ConnectorId::generate();
  const std::vector<WeightedCandidate> group  = {
      WeightedCandidate{first, *Rational::create(3, 10)},
      WeightedCandidate{zero, Rational(0)},
      WeightedCandidate{second, *Rational::create(7, 10)},
  };
  ASSERT_EQ(weight_group_roll_bound(group), 10u);

  EXPECT_EQ(select_weighted_random(group, 2), first);
  EXPECT_EQ(select_weighted_random(group, 3), second);
  EXPECT_EQ(select_weighted_random(group, 9), second);
}

TEST(WeightedSelectionTest, MixedDenominatorGroupSelectsTheCorrectBuckets) {
  // 1/2 + 1/3 + 1/6 over a common denominator of 6: buckets [0, 3), [3, 5),
  // [5, 6).
  const ConnectorId                    half  = ConnectorId::generate();
  const ConnectorId                    third = ConnectorId::generate();
  const ConnectorId                    sixth = ConnectorId::generate();
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{half, *Rational::create(1, 2)},
      WeightedCandidate{third, *Rational::create(1, 3)},
      WeightedCandidate{sixth, *Rational::create(1, 6)},
  };
  ASSERT_EQ(weight_group_roll_bound(group), 6u);

  EXPECT_EQ(select_weighted_random(group, 0), half);
  EXPECT_EQ(select_weighted_random(group, 2), half);
  EXPECT_EQ(select_weighted_random(group, 3), third);
  EXPECT_EQ(select_weighted_random(group, 4), third);
  EXPECT_EQ(select_weighted_random(group, 5), sixth);
}

// -- Deterministic distribution and golden-vector determinism -------------

TEST(WeightedSelectionTest,
     FixedSeedProducesTheDocumentedProportionOfPicksExactly) {
  const ConnectorId                    low   = ConnectorId::generate();
  const ConnectorId                    high  = ConnectorId::generate();
  const std::vector<WeightedCandidate> group = {
      WeightedCandidate{low, *Rational::create(1, 5)},
      WeightedCandidate{high, *Rational::create(4, 5)},
  };
  ASSERT_EQ(weight_group_roll_bound(group), 5u);

  // Golden rolls for DeterministicPrng(42).next_below(5), six times:
  // 3, 1, 3, 4, 0, 2 -- exactly one (the fifth) falls below low's bucket
  // width of 1.
  DeterministicPrng prng(42);
  int               low_count  = 0;
  int               high_count = 0;
  for (int i = 0; i < 6; ++i) {
    const std::optional<ConnectorId> winner =
        select_weighted_random(group, prng.next_below(5));
    ASSERT_TRUE(winner.has_value());
    if (*winner == low)
      ++low_count;
    else
      ++high_count;
  }

  EXPECT_EQ(low_count, 1);
  EXPECT_EQ(high_count, 5);
}

TEST(WeightedSelectionTest,
     GoldenVectorExactThreeWaySplitProducesTheHardcodedWinnerSequence) {
  // The exact three-way split that motivates this Rational conversion:
  // 1/3 + 1/3 + 1/3 == 1, unrepresentable as an unbiased whole percent.
  const ConnectorId                    first  = ConnectorId::generate();
  const ConnectorId                    second = ConnectorId::generate();
  const ConnectorId                    third  = ConnectorId::generate();
  const std::vector<WeightedCandidate> group  = {
      WeightedCandidate{first, *Rational::create(1, 3)},
      WeightedCandidate{second, *Rational::create(1, 3)},
      WeightedCandidate{third, *Rational::create(1, 3)},
  };
  ASSERT_EQ(validate_weight_group(group), WeightGroupValidity::kValid);
  ASSERT_EQ(weight_group_roll_bound(group), 3u);

  // Golden rolls for DeterministicPrng(42).next_below(3), eight times:
  // 1, 1, 0, 0, 1, 0, 1, 2. Buckets are first [0, 1), second [1, 2), third
  // [2, 3), so this rolls out to the hardcoded winner sequence below, in
  // which all three candidates are selected at least once. A future change
  // to the selection algorithm that alters this sequence must not pass
  // silently.
  const std::vector<ConnectorId> expected_winners = {
      second, second, first, first, second, first, second, third,
  };

  DeterministicPrng prng(42);
  for (std::size_t i = 0; i < expected_winners.size(); ++i) {
    const std::optional<ConnectorId> winner =
        select_weighted_random(group, prng.next_below(3));
    ASSERT_TRUE(winner.has_value());
    EXPECT_EQ(*winner, expected_winners[i]);
  }
}
