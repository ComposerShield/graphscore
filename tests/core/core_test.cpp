// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/core/graphscore_core.hpp>

TEST(CoreTest, UuidDefaultIsZero) {
  const graphscore::Uuid u;
  for (auto b : u.bytes()) {
    EXPECT_EQ(b, 0);
  }
}

TEST(CoreTest, UuidGenerateIsNonZero) {
  const graphscore::Uuid u = graphscore::Uuid::generate();
  // misc-const-correctness false positive on clang-tidy 18: the variable is
  // written inside the range-for body, but the check does not see the write.
  bool any_nonzero = false;  // NOLINT(misc-const-correctness)
  for (auto b : u.bytes()) {
    if (b != 0)
      any_nonzero = true;
  }
  EXPECT_TRUE(any_nonzero);
}

TEST(CoreTest, UuidEquality) {
  const graphscore::Uuid a = graphscore::Uuid::generate();
  const graphscore::Uuid b = graphscore::Uuid::generate();
  EXPECT_NE(a, b);
  EXPECT_EQ(a, a);
}

TEST(CoreTest, RationalDefaults) {
  const graphscore::Rational r;
  EXPECT_EQ(r.numerator(), 0);
  EXPECT_EQ(r.denominator(), 1);
  EXPECT_EQ(r.to_double(), 0.0);
}

TEST(CoreTest, RationalValues) {
  const graphscore::Rational r = graphscore::Rational::create(3, 4).value();
  EXPECT_EQ(r.numerator(), 3);
  EXPECT_EQ(r.denominator(), 4);
  EXPECT_DOUBLE_EQ(r.to_double(), 0.75);
}

TEST(CoreTest, ResultSuccessOk) {
  const graphscore::Result r(graphscore::ResultCode::kSuccess);
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(static_cast<bool>(r));
}

TEST(CoreTest, ResultFailureNotOk) {
  const graphscore::Result r(graphscore::ResultCode::kInvalidArgument);
  EXPECT_FALSE(r.ok());
}
