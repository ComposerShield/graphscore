// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/cooked_format/graphscore_cooked_format.hpp>

TEST(CookedFormatTest, MagicConstantIsCorrect) {
  EXPECT_EQ(graphscore::cooked_format::kMagic, 0x4753434B);
}

TEST(CookedFormatTest, VersionFields) {
  EXPECT_EQ(graphscore::cooked_format::kVersionMajor, 0U);
  EXPECT_EQ(graphscore::cooked_format::kVersionMinor, 1U);
}
