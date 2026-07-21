// SPDX-License-Identifier: Apache-2.0

#include <graphscore/loader/graphscore_loader.hpp>

namespace graphscore {
namespace {
constexpr int kLoaderVersion = 1;
}  // namespace

int loader_version() {
  return kLoaderVersion;
}
}  // namespace graphscore
