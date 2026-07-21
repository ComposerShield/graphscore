// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <version>

TEST(FoundationSmoke, CompilesAsCpp23) {
#if __cplusplus < 202302L && !defined(_MSC_VER)
  FAIL() << "expected C++23 or later, got " << __cplusplus;
#else
  SUCCEED();
#endif
}

TEST(FoundationSmoke, WarningsAsErrorsBuildSucceeded) {
  // Merely reaching this point proves the target-scoped warning flags in
  // cmake/Warnings.cmake compiled this translation unit clean.
  EXPECT_EQ(2 + 2, 4);
}
