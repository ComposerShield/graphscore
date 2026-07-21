// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/domain/graphscore_domain.hpp>

TEST(DomainTest, Compiles) {
  graphscore::Project* p = nullptr;
  graphscore::Track* t = nullptr;
  graphscore::Node* n = nullptr;
  graphscore::Graph* g = nullptr;
  graphscore::Command* c = nullptr;
  graphscore::Selection* s = nullptr;
  graphscore::ValidationService* v = nullptr;
  EXPECT_EQ(p, nullptr);
  EXPECT_EQ(t, nullptr);
  EXPECT_EQ(n, nullptr);
  EXPECT_EQ(g, nullptr);
  EXPECT_EQ(c, nullptr);
  EXPECT_EQ(s, nullptr);
  EXPECT_EQ(v, nullptr);
}
