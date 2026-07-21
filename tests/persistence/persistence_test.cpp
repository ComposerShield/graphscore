// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/persistence/graphscore_persistence.hpp>

TEST(PersistenceTest, Compiles) {
  graphscore::Persistence p;
  static_cast<void>(p);
  SUCCEED();
}
