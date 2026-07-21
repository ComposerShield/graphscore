// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/domain/graphscore_domain.hpp>

TEST(DomainTest, Compiles) {
  const graphscore::Project*           p = nullptr;
  const graphscore::Track*             t = nullptr;
  const graphscore::Node*              n = nullptr;
  const graphscore::Graph*             g = nullptr;
  const graphscore::Command*           c = nullptr;
  const graphscore::Selection*         s = nullptr;
  const graphscore::ValidationService* v = nullptr;
  EXPECT_EQ(p, nullptr);
  EXPECT_EQ(t, nullptr);
  EXPECT_EQ(n, nullptr);
  EXPECT_EQ(g, nullptr);
  EXPECT_EQ(c, nullptr);
  EXPECT_EQ(s, nullptr);
  EXPECT_EQ(v, nullptr);
}
