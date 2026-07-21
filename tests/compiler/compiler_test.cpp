// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/compiler/graphscore_compiler.hpp>

TEST(CompilerTest, Compiles) {
  graphscore::Compiler c;
  static_cast<void>(c);
  SUCCEED();
}
