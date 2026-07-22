// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::Rational;

TEST(RationalTest, CreateRejectsZeroDenominator) {
  EXPECT_FALSE(Rational::create(1, 0).has_value());
}

TEST(RationalTest, CreateReducesToLowestTerms) {
  const Rational r = Rational::create(2, 4).value();
  EXPECT_EQ(r.numerator(), 1);
  EXPECT_EQ(r.denominator(), 2);
}

TEST(RationalTest, CreateCanonicalizesNegativeDenominator) {
  const Rational r = Rational::create(1, -2).value();
  EXPECT_EQ(r.numerator(), -1);
  EXPECT_EQ(r.denominator(), 2);
}

TEST(RationalTest, WholeNumberConstructorIsAlwaysValid) {
  const Rational r(3);
  EXPECT_EQ(r.numerator(), 3);
  EXPECT_EQ(r.denominator(), 1);
}

TEST(RationalTest, CrossDenominatorEquality) {
  EXPECT_EQ(Rational::create(1, 2).value(), Rational::create(2, 4).value());
  EXPECT_EQ(Rational::create(1, 3).value(), Rational::create(2, 6).value());
}

TEST(RationalTest, CrossDenominatorOrdering) {
  EXPECT_LT(Rational::create(1, 3).value(), Rational::create(1, 2).value());
  EXPECT_GT(Rational::create(2, 3).value(), Rational::create(1, 2).value());
}

TEST(RationalTest, Addition) {
  const Rational sum =
      Rational::create(1, 3).value() + Rational::create(1, 6).value();
  EXPECT_EQ(sum, Rational::create(1, 2).value());
}

TEST(RationalTest, Subtraction) {
  const Rational difference =
      Rational::create(3, 4).value() - Rational::create(1, 4).value();
  EXPECT_EQ(difference, Rational::create(1, 2).value());
}

TEST(RationalTest, Multiplication) {
  const Rational product =
      Rational::create(2, 3).value() * Rational::create(3, 4).value();
  EXPECT_EQ(product, Rational::create(1, 2).value());
}

TEST(RationalTest, Division) {
  const Rational quotient =
      Rational::create(1, 2).value() / Rational::create(1, 4).value();
  EXPECT_EQ(quotient, Rational(2));
}

TEST(RationalTest, UnaryNegation) {
  EXPECT_EQ(-Rational::create(3, 4).value(), Rational::create(-3, 4).value());
}

TEST(RationalTest, ComponentsRoundTrip) {
  const Rational                       r = Rational::create(5, 8).value();
  const graphscore::RationalComponents components = r.to_components();
  EXPECT_EQ(components.numerator, 5);
  EXPECT_EQ(components.denominator, 8);
  EXPECT_EQ(Rational::from_components(components).value(), r);
}

TEST(RationalTest, FromComponentsRejectsZeroDenominator) {
  const graphscore::RationalComponents components{1, 0};
  EXPECT_FALSE(Rational::from_components(components).has_value());
}
