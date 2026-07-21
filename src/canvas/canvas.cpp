// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

namespace graphscore {
namespace {
constexpr int kCanvasVersion = 1;
}  // namespace

int canvas_version() {
  return kCanvasVersion;
}
}  // namespace graphscore
