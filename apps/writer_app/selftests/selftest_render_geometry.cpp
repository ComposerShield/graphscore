// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdio>
#include <limits>
#include <optional>

namespace graphscore::writer_app {
// ---- M6-phase-5: render-destination float-corner validation --------------
//
// The render pass converts the notation destination rect (doubles) to an
// SDL_FRect (floats). A double rect can be strictly positive yet have its
// float corners collapse — a width absorbed into the float origin
// (float(x) + float(width) == float(x)) or a right/bottom edge that overflows
// to infinity — which would make SDL draw a degenerate or clipped rectangle.
// This headless test drives WriterShell::test_float_rect (the SDL-free form of
// the exact check notation_rect_to_sdl_frect applies) across the boundary
// cases, so a regression to per-field-only validation fails on every build
// host rather than only a display-attached one.
int render_geometry_test() {
  const auto draws = [](graphscore::NotationRect rect) {
    return graphscore::WriterShell::test_float_rect(rect).has_value();
  };

  // Ordinary rect: finite, positive, advancing corners — accepted.
  if (!draws({10.0, 20.0, 30.0, 40.0})) {
    std::fprintf(stderr, "render-geometry-test: ordinary rect rejected\n");
    return 1;
  }

  // Negative origin with advancing corners — accepted (x + w > x still holds).
  if (!draws({-100.0, -50.0, 30.0, 20.0})) {
    std::fprintf(stderr,
                 "render-geometry-test: negative-origin advancing rect "
                 "rejected\n");
    return 1;
  }

  // Width absorbed into the float origin: float(1e30) + float(1.0) rounds back
  // to float(1e30), so the right edge does not advance — rejected.
  if (draws({1e30, 0.0, 1.0, 1.0})) {
    std::fprintf(stderr, "render-geometry-test: absorbed width accepted\n");
    return 1;
  }

  // Negative origin absorbing the width — rejected.
  if (draws({-1e30, 0.0, 1.0, 1.0})) {
    std::fprintf(stderr,
                 "render-geometry-test: absorbed negative-origin width "
                 "accepted\n");
    return 1;
  }

  // Right-edge overflow: float(3e38) + float(3e38) = infinity — rejected.
  if (draws({3e38, 0.0, 3e38, 1.0})) {
    std::fprintf(stderr,
                 "render-geometry-test: overflowing right edge accepted\n");
    return 1;
  }

  // Bottom-edge overflow — rejected.
  if (draws({0.0, 3e38, 1.0, 3e38})) {
    std::fprintf(stderr,
                 "render-geometry-test: overflowing bottom edge accepted\n");
    return 1;
  }

  // Non-finite field — rejected.
  if (draws({std::numeric_limits<double>::infinity(), 0.0, 1.0, 1.0}) ||
      draws({0.0, std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0})) {
    std::fprintf(stderr, "render-geometry-test: non-finite field accepted\n");
    return 1;
  }

  // Non-positive width/height — rejected.
  if (draws({0.0, 0.0, 0.0, 1.0}) || draws({0.0, 0.0, 1.0, -1.0})) {
    std::fprintf(stderr,
                 "render-geometry-test: non-positive dimension accepted\n");
    return 1;
  }

  std::printf("render-geometry-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
