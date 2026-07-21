// SPDX-License-Identifier: Apache-2.0

#include <graphscore/rendering/graphscore_rendering.hpp>

namespace graphscore {
namespace {
constexpr int kRenderingVersion = 1;
}  // namespace

int rendering_version() {
  return kRenderingVersion;
}
}  // namespace graphscore
