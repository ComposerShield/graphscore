// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/loader/graphscore_loader.hpp>

TEST(LoaderTest, Compiles) {
  const graphscore::Loader l;
  static_cast<void>(l);
  SUCCEED();
}
