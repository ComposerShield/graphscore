// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::DeterministicPrng;

TEST(DeterministicPrngTest, SameSeedProducesIdenticalStream) {
  DeterministicPrng a(12345);
  DeterministicPrng b(12345);

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(a.next_u64(), b.next_u64());
  }
}

TEST(DeterministicPrngTest, DifferentSeedsDiverge) {
  DeterministicPrng a(1);
  DeterministicPrng b(2);

  bool saw_difference = false;
  for (int i = 0; i < 8; ++i) {
    if (a.next_u64() != b.next_u64()) {
      saw_difference = true;
      break;
    }
  }
  EXPECT_TRUE(saw_difference);
}

TEST(DeterministicPrngTest, ResetReproducesTheOriginalStreamExactly) {
  DeterministicPrng          original(999999937);
  std::vector<std::uint64_t> expected;
  expected.reserve(10);
  for (int i = 0; i < 10; ++i) {
    expected.push_back(original.next_u64());
  }

  DeterministicPrng replay(1);  // Arbitrary different starting seed.
  replay.reset(999999937);
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(replay.next_u64(), expected[static_cast<std::size_t>(i)]);
  }
}

TEST(DeterministicPrngTest, ResetMidStreamRestartsFromTheGivenSeed) {
  DeterministicPrng prng(42);
  (void)prng.next_u64();
  (void)prng.next_u64();
  (void)prng.next_u64();

  prng.reset(42);

  DeterministicPrng fresh(42);
  EXPECT_EQ(prng.next_u64(), fresh.next_u64());
}

TEST(DeterministicPrngTest, NextBelowStaysWithinBound) {
  DeterministicPrng prng(7);
  for (int i = 0; i < 200; ++i) {
    EXPECT_LT(prng.next_below(5), 5u);
  }
}

TEST(DeterministicPrngTest, NextBelowZeroReturnsZeroAndStillAdvances) {
  DeterministicPrng a(42);
  DeterministicPrng b(42);
  EXPECT_EQ(a.next_below(0), 0u);
  EXPECT_EQ(b.next_u64(), 13679457532755275413ULL);
  EXPECT_EQ(a.next_u64(), b.next_u64());
}

// -- Golden vectors ----------------------------------------------------
// Hardcoded expected output of the exact SplitMix64 arithmetic this class
// documents, so a future change to the algorithm cannot pass silently.

TEST(DeterministicPrngTest, GoldenVectorSeedZero) {
  DeterministicPrng prng(0);
  EXPECT_EQ(prng.next_u64(), 16294208416658607535ULL);
  EXPECT_EQ(prng.next_u64(), 7960286522194355700ULL);
  EXPECT_EQ(prng.next_u64(), 487617019471545679ULL);
  EXPECT_EQ(prng.next_u64(), 17909611376780542444ULL);
}

TEST(DeterministicPrngTest, GoldenVectorSeedFortyTwo) {
  DeterministicPrng prng(42);
  EXPECT_EQ(prng.next_u64(), 13679457532755275413ULL);
  EXPECT_EQ(prng.next_u64(), 2949826092126892291ULL);
  EXPECT_EQ(prng.next_u64(), 5139283748462763858ULL);
  EXPECT_EQ(prng.next_u64(), 6349198060258255764ULL);
  EXPECT_EQ(prng.next_u64(), 701532786141963250ULL);
  EXPECT_EQ(prng.next_u64(), 16015981125662989062ULL);
}

TEST(DeterministicPrngTest, GoldenVectorNextBelowOneHundredSeedFortyTwo) {
  DeterministicPrng                prng(42);
  const std::vector<std::uint64_t> expected = {13, 91, 58, 64, 50, 62};
  for (const std::uint64_t expected_roll : expected) {
    EXPECT_EQ(prng.next_below(100), expected_roll);
  }
}
