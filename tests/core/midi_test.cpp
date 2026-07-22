// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <type_traits>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::MidiChannel;
using graphscore::MidiPitch;
using graphscore::MidiVelocity;

TEST(MidiPitchTest, MinIsValid) {
  const auto pitch = MidiPitch::create(MidiPitch::kMin);
  ASSERT_TRUE(pitch.has_value());
  EXPECT_EQ(pitch->value(), 0);
}

TEST(MidiPitchTest, MaxIsValid) {
  const auto pitch = MidiPitch::create(MidiPitch::kMax);
  ASSERT_TRUE(pitch.has_value());
  EXPECT_EQ(pitch->value(), 127);
}

TEST(MidiPitchTest, JustOutOfRangeIsInvalid) {
  EXPECT_FALSE(MidiPitch::create(128).has_value());
}

TEST(MidiPitchTest, WellOutOfRangeIsInvalid) {
  EXPECT_FALSE(MidiPitch::create(255).has_value());
}

TEST(MidiPitchTest, DefaultIsMinimum) {
  EXPECT_EQ(MidiPitch().value(), MidiPitch::kMin);
}

TEST(MidiPitchTest, OrdersByValue) {
  EXPECT_LT(MidiPitch::create(60).value(), MidiPitch::create(61).value());
}

TEST(MidiVelocityTest, MinIsValid) {
  EXPECT_TRUE(MidiVelocity::create(MidiVelocity::kMin).has_value());
}

TEST(MidiVelocityTest, MaxIsValid) {
  EXPECT_TRUE(MidiVelocity::create(MidiVelocity::kMax).has_value());
}

TEST(MidiVelocityTest, JustOutOfRangeIsInvalid) {
  EXPECT_FALSE(MidiVelocity::create(128).has_value());
}

TEST(MidiChannelTest, MinIsValid) {
  const auto channel = MidiChannel::create(MidiChannel::kMin);
  ASSERT_TRUE(channel.has_value());
  EXPECT_EQ(channel->value(), 0);
}

TEST(MidiChannelTest, MaxIsValid) {
  const auto channel = MidiChannel::create(MidiChannel::kMax);
  ASSERT_TRUE(channel.has_value());
  EXPECT_EQ(channel->value(), 15);
}

TEST(MidiChannelTest, JustOutOfRangeIsInvalid) {
  EXPECT_FALSE(MidiChannel::create(16).has_value());
}

TEST(MidiChannelTest, WellOutOfRangeIsInvalid) {
  EXPECT_FALSE(MidiChannel::create(255).has_value());
}

TEST(MidiPitchTest, DistinctFromVelocityAtCompileTime) {
  static_assert(!std::is_same_v<MidiPitch, MidiVelocity>);
  SUCCEED();
}
