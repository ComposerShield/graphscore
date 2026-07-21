// SPDX-License-Identifier: Apache-2.0
//
// External CMake consumer test: proves that graphscore_runtime can be
// consumed without pulling in any writer-only target.

#include <graphscore/c_abi/graphscore_c_abi.h>

#include <cstdio>

int main() {
  gs_version_t v = gs_query_version();
  if (v.major != GS_VERSION_MAJOR) {
    return 1;
  }
  return 0;
}
