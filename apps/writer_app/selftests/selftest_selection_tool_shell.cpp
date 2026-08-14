// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../app_shell_report.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {
// ---- shell-integration selection-tool test (requires display) ---------------
//
// Opens a real SDL window with a registered handler, then injects pointer
// events through both dispatch_test_pointer_event (the headless seam) and
// dispatch_sdl_test_pointer_event (the production SDL conversion path) and
// asserts exact handler state: coordinates, drag lifecycle, highlight
// delivery, registration/unregistration, DPI scale conversion, RGBA texture
// upload-readback, committed-selection highlight persistence across cancel,
// and texture failure non-success.
//
// The test seam applies the same DPI scale conversion contract as the
// production SDL_ConvertEventToRenderCoordinates path, so coordinate
// assertions hold for both paths.  Texture-readback assertions use
// SDL_RenderReadPixels; they verify channel-order integrity and are not
// framebuffer-pixel-accuracy claims.
//
// A genuinely absent display (kBackendUnavailable) or absent GPU
// (kRendererUnavailable) is a skip-match; a rendering-setup defect such as
// a texture creation or upload failure (kRenderingSetupFailed) is a hard
// test failure and does not match the PASS regex.
int selection_tool_shell_test() {
  // ---- pre-open surface validation (display-independent) ----
  // set_notation_surface validates dimensions and buffer size eagerly,
  // before any window or renderer exists.  Every invalid case below must
  // return kRenderingSetupFailed; the canonical empty (0,0,empty) and an
  // exact-size buffer must succeed.
  {
    graphscore::WriterShell preopen_shell;

    // Canonical empty: (0, 0, empty buffer) — accepted.
    {
      graphscore::RasterSurface surface;
      surface.width  = 0;
      surface.height = 0;
      surface.rgba.clear();
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (!result.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: canonical empty surface "
                     "rejected before open: %s\n",
                     result.message.c_str());
        return 1;
      }
    }

    // Canonical empty with non-empty rgba: (0, 0, non-empty) — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 0;
      surface.height = 0;
      surface.rgba   = {0x00, 0x00, 0x00, 0xFF};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (0,0,non-empty) surface not "
                     "rejected before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // One-zero dimension: (0, 1) — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 0;
      surface.height = 1;
      surface.rgba   = {0, 0, 0, 0};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (0,1) surface not rejected "
                     "before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // One-zero dimension: (1, 0) — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 1;
      surface.height = 0;
      surface.rgba   = {0, 0, 0, 0};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (1,0) surface not rejected "
                     "before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // Undersized buffer: 2×2 needs 16 bytes, only 1 supplied — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 2;
      surface.height = 2;
      surface.rgba   = {0xFF};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: undersized buffer not "
                     "rejected before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // Oversized buffer: 2×2 needs 16 bytes, 17 supplied — rejected
    // (exact equality is load-bearing).
    {
      graphscore::RasterSurface surface;
      surface.width  = 2;
      surface.height = 2;
      surface.rgba.assign(17, 0x00);
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: oversized buffer not "
                     "rejected before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // Exact buffer: 2×2 with exactly 16 bytes — accepted.
    {
      graphscore::RasterSurface surface;
      surface.width  = 2;
      surface.height = 2;
      surface.rgba.assign(16, 0x00);
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (!result.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: exact buffer surface "
                     "rejected before open: %s\n",
                     result.message.c_str());
        return 1;
      }
    }
  }

  // ---- set_notation_surface eagerly rejects invalid dimensions ----
  // The surface validation is backend-independent: it rejects
  // undersized/oversized buffers and one-zero dimensions before any
  // window or renderer exists, in both writer-ON and writer-OFF builds.
  // No open_window call is needed — the eager check alone catches every
  // invalid case above.
  {
    graphscore::WriterShell   invalid_shell;
    graphscore::RasterSurface surface;
    surface.width  = 2;
    surface.height = 2;
    surface.rgba   = {0xFF};  // 1 byte; 2×2×4 = 16 required
    const auto set_result =
        invalid_shell.set_notation_surface(std::move(surface));
    // Eager validation rejects the undersized buffer immediately — no
    // renderer is required for the dimension/buffer-size check.
    if (set_result.error != graphscore::ShellError::kRenderingSetupFailed) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: undersized surface not "
                   "eagerly rejected (got %s)\n",
                   describe(set_result.error));
      return 1;
    }
  }

  const SelfTestMetrics metrics;
  auto                  dp = build_default_project(metrics);
  if (!dp.has_value()) {
    std::fprintf(stderr,
                 "selection-tool-shell-test: build_default_project failed\n");
    return 1;
  }

  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(dp->project), std::move(dp->layout),
                                  &shell);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  shell.set_input_handler(&handler);

  // Open the window without event loop (smoke-test style). This verifies
  // the SDL window and renderer are functional on this host, but does not
  // run a blocking event loop — event injection below uses the test seam.
  graphscore::WindowOptions options;
  options.run_event_loop = false;

  const graphscore::ShellResult result = shell.open_window(options);
  if (!result.ok()) {
    const bool is_benign_headless =
        result.error == graphscore::ShellError::kBackendUnavailable ||
        result.error == graphscore::ShellError::kRendererUnavailable;
    std::fprintf(stderr, "selection-tool-shell-test: open_window failed: %s\n",
                 is_benign_headless
                     ? "no display or renderer — headless host (skip)"
                     : result.message.c_str());
    shell.set_input_handler(nullptr);
    return is_benign_headless ? 0 : 1;
  }

  const auto& layout = handler.layout();

  auto make_event = [](double x, double y,
                       graphscore::PointerButton button =
                           graphscore::PointerButton::kPrimary) {
    graphscore::PointerEvent e;
    e.x      = x;
    e.y      = y;
    e.button = button;
    return e;
  };

  // ---- exact coordinate assertions at 1x DPI (production SDL path) ----
  // Route pixel-space coordinates through dispatch_sdl_test_pointer_event,
  // which exercises SDL_ConvertEventToRenderCoordinates.  At 1x DPI,
  // pixel and logical coordinates are identical.
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;

    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag did not begin at 1x "
                   "(production SDL path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Anchor must be exactly the press position (at 1x DPI,
    // SDL_ConvertEventToRenderCoordinates is the identity).
    const graphscore::NotationPoint anchor = handler.drag_state().anchor();
    if (std::abs(anchor.x - press_x) > 1e-9 ||
        std::abs(anchor.y - press_y) > 1e-9) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: anchor mismatch at 1x: "
                   "expected (%.2f, %.2f), got (%.2f, %.2f)\n",
                   press_x, press_y, anchor.x, anchor.y);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }
  handler.on_cancel();

  // ---- drag-move-release span assertion at 1x (production SDL path) ----
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_sdl_test_pointer_event(1, make_event(move_x, press_y));
    shell.dispatch_sdl_test_pointer_event(2, make_event(move_x, press_y));

    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress after "
                   "release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: no committed selection after "
                   "drag-release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Exact committed span and highlight rects.
    const auto* set = std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
    if (set == nullptr || set->items().empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: committed selection is not a "
                   "non-empty ArbitraryRangeSet\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (set->items().size() != 1u) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: expected 1 range item at 1x, "
                   "got %zu\n",
                   set->items().size());
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const graphscore::MusicalSpan expected{graphscore::Rational(0),
                                             graphscore::Rational(1)};
      if (set->items()[0].span != expected) {
        std::fprintf(
            stderr,
            "selection-tool-shell-test: span mismatch at 1x — "
            "expected [0, 1), got [%" PRId64 "/%" PRId64 ", %" PRId64
            "/%" PRId64 ")\n",
            static_cast<std::int64_t>(set->items()[0].span.start.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.start.denominator()),
            static_cast<std::int64_t>(set->items()[0].span.end.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.end.denominator()));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Exact highlight rect at 1x: x,y,width,height must equal the expected
    // values derived from the fixture's measure and staff bounds.
    {
      const std::vector<graphscore::NotationRect> rects =
          shell.test_snapshot_highlight_rects();
      if (rects.size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: expected 1 highlight rect "
                     "at 1x, got %zu\n",
                     rects.size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::NotationRect expected =
          expected_full_measure_highlight_rect(layout);
      if (rects[0] != expected) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: highlight rect mismatch "
                     "at 1x — "
                     "expected [%.6f,%.6f %.6fx%.6f], "
                     "got [%.6f,%.6f %.6fx%.6f]\n",
                     expected.x, expected.y, expected.width, expected.height,
                     rects[0].x, rects[0].y, rects[0].width, rects[0].height);
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }

  // ---- 2x DPI scale: press, move, release, exact converted anchor ----
  // Use dispatch_sdl_test_pointer_event which routes through the production
  // SDL_ConvertEventToRenderCoordinates path.  set_test_dpi_scale also
  // pushes the scale to the renderer via SDL_SetRenderScale so the
  // conversion path sees the correct 2x mapping.
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  shell.set_test_dpi_scale(2.0);
  {
    // Pixel-space coordinates at the logical position × 2.  The SDL
    // production path converts these through
    // SDL_ConvertEventToRenderCoordinates with the 2x render scale, yielding
    // the correct logical position.
    const double logical_x = layout.systems[0].measures[0].bounds.x;
    const double logical_y = layout.systems[0].staves[0].bounds.y +
                             layout.systems[0].staves[0].bounds.height * 0.5;
    const double pixel_x = logical_x * 2.0;
    const double pixel_y = logical_y * 2.0;

    shell.dispatch_sdl_test_pointer_event(0, make_event(pixel_x, pixel_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag did not begin at 2x "
                   "(production SDL path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Anchor must be at the logical coordinates (SDL conversion applied).
    const graphscore::NotationPoint anchor = handler.drag_state().anchor();
    if (std::abs(anchor.x - logical_x) > 1e-9 ||
        std::abs(anchor.y - logical_y) > 1e-9) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: anchor mismatch at 2x: "
                   "expected logical (%.2f, %.2f), got (%.2f, %.2f)\n",
                   logical_x, logical_y, anchor.x, anchor.y);
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Move and release at 2x pixel coords.
    const double move_logical_x = layout.systems[0].measures[0].bounds.x +
                                  layout.systems[0].measures[0].bounds.width;
    const double move_pixel_x = move_logical_x * 2.0;
    shell.dispatch_sdl_test_pointer_event(1, make_event(move_pixel_x, pixel_y));
    shell.dispatch_sdl_test_pointer_event(2, make_event(move_pixel_x, pixel_y));

    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress at 2x "
                   "after release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: no committed selection at 2x\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto* set = std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
    if (set == nullptr || set->items().empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: committed selection at 2x is "
                   "empty\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Exact span at 2x: same fixture, same full-measure drag, same [0, 1).
    if (set->items().size() != 1u) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: expected 1 range item at 2x, "
                   "got %zu\n",
                   set->items().size());
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const graphscore::MusicalSpan expected{graphscore::Rational(0),
                                             graphscore::Rational(1)};
      if (set->items()[0].span != expected) {
        std::fprintf(
            stderr,
            "selection-tool-shell-test: span mismatch at 2x — "
            "expected [0, 1), got [%" PRId64 "/%" PRId64 ", %" PRId64
            "/%" PRId64 ")\n",
            static_cast<std::int64_t>(set->items()[0].span.start.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.start.denominator()),
            static_cast<std::int64_t>(set->items()[0].span.end.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.end.denominator()));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Exact highlight rect at 2x: same expected geometry as 1x — the
    // rect is in logical (notation) coordinates, not pixel coordinates,
    // so the DPI scale does not affect it.  x,y,width,height must exactly
    // equal the values derived from the fixture's measure and staff bounds.
    {
      const std::vector<graphscore::NotationRect> rects =
          shell.test_snapshot_highlight_rects();
      if (rects.size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: expected 1 highlight rect "
                     "at 2x, got %zu\n",
                     rects.size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::NotationRect expected =
          expected_full_measure_highlight_rect(layout);
      if (rects[0] != expected) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: highlight rect mismatch "
                     "at 2x — "
                     "expected [%.6f,%.6f %.6fx%.6f], "
                     "got [%.6f,%.6f %.6fx%.6f]\n",
                     expected.x, expected.y, expected.width, expected.height,
                     rects[0].x, rects[0].y, rects[0].width, rects[0].height);
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }
  shell.set_test_dpi_scale(0.0);

  // ---- unregistered handler receives no event ----
  shell.set_input_handler(nullptr);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: handler received event after "
                   "unregistration\n");
      return 1;
    }
  }

  // ---- RGBA upload-readback: distinctive non-symmetric pixel pattern ----
  // Upload a 2×2 raster surface with known per-pixel RGBA values and read
  // back two pixels to verify the byte-order contract survives the upload /
  // readback round-trip.  The pattern uses non-symmetric channel values so
  // any channel swap (R↔B, R↔A, etc.) is immediately detected.
  {
    graphscore::RasterSurface surface;
    surface.width  = 2;
    surface.height = 2;
    // Pixel (0,0): R=0xAA, G=0x00, B=0xFF, A=0x80 — distinctive
    // Pixel (1,0): R=0x55, G=0xCC, B=0x11, A=0xFF
    // Pixel (0,1): R=0x22, G=0x33, B=0x44, A=0x55
    // Pixel (1,1): R=0x66, G=0x77, B=0x88, A=0x99
    surface.rgba = {0xAA, 0x00, 0xFF, 0x80, 0x55, 0xCC, 0x11, 0xFF,
                    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    shell.set_notation_surface(std::move(surface));

    // First upload: exactly one texture created, none destroyed, one alive.
    {
      auto s = shell.test_notation_texture_stats();
      if (s.created != 1 || s.destroyed != 0) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture stats after first "
                     "upload — expected created=1 destroyed=0 alive=1, "
                     "got created=%" PRIu64 " destroyed=%" PRIu64
                     " alive=%" PRIu64 "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        return 1;
      }
    }

    auto p00 = shell.test_read_notation_pixel(0, 0);
    if (!p00.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback of pixel (0,0) "
                   "returned nullopt\n");
      return 1;
    }
    if ((*p00)[0] != 0xAA || (*p00)[1] != 0x00 || (*p00)[2] != 0xFF ||
        (*p00)[3] != 0x80) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: pixel (0,0) mismatch — "
                   "expected [0xAA,0x00,0xFF,0x80], got "
                   "[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                   (*p00)[0], (*p00)[1], (*p00)[2], (*p00)[3]);
      return 1;
    }

    auto p11 = shell.test_read_notation_pixel(1, 1);
    if (!p11.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback of pixel (1,1) "
                   "returned nullopt\n");
      return 1;
    }
    if ((*p11)[0] != 0x66 || (*p11)[1] != 0x77 || (*p11)[2] != 0x88 ||
        (*p11)[3] != 0x99) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: pixel (1,1) mismatch — "
                   "expected [0x66,0x77,0x88,0x99], got "
                   "[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                   (*p11)[0], (*p11)[1], (*p11)[2], (*p11)[3]);
      return 1;
    }
  }

  // ---- texture failure injection: zero-dimension surface produces
  //     null-texture / nullopt readback ----
  {
    // Set an empty surface (width==0); the texture should be destroyed
    // and readback must return nullopt — not e.g. stale data.
    shell.set_notation_surface(graphscore::RasterSurface{});
    // Clear: old texture destroyed, alive becomes zero.
    {
      auto s = shell.test_notation_texture_stats();
      if (s.created != 1 || s.destroyed != 1) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture stats after clear — "
                     "expected created=1 destroyed=1 alive=0, "
                     "got created=%" PRIu64 " destroyed=%" PRIu64
                     " alive=%" PRIu64 "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        return 1;
      }
    }
    auto pixel = shell.test_read_notation_pixel(0, 0);
    if (pixel.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback after clearing "
                   "notation surface returned a pixel (expected nullopt)\n");
      return 1;
    }

    // Out-of-bounds readback on a valid texture must also return nullopt.
    graphscore::RasterSurface small;
    small.width  = 1;
    small.height = 1;
    small.rgba   = {0xFF, 0x00, 0xFF, 0x80};
    shell.set_notation_surface(std::move(small));
    // New upload: created=2, destroyed=1, alive=1.
    {
      auto s = shell.test_notation_texture_stats();
      if (s.created != 2 || s.destroyed != 1) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture stats after OOB "
                     "upload — expected created=2 destroyed=1 alive=1, "
                     "got created=%" PRIu64 " destroyed=%" PRIu64
                     " alive=%" PRIu64 "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        return 1;
      }
    }
    auto oob = shell.test_read_notation_pixel(1, 1);
    if (oob.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback past texture "
                   "bounds returned a pixel (expected nullopt)\n");
      return 1;
    }
  }

  // ---- post-open invalid surface: error returned, valid surface survives ----
  // After a valid surface is uploaded and readable, call set_notation_surface
  // with invalid surfaces and verify: (a) the call returns
  // kRenderingSetupFailed, (b) the previously valid texture is not destroyed or
  // replaced, and (c) a prior distinctive pixel remains readable. Honest skip
  // when no renderer is available.
  {
    // Upload a known-good 2×2 distinctive pattern.
    graphscore::RasterSurface good;
    good.width               = 2;
    good.height              = 2;
    good.rgba                = {0xAA, 0x00, 0xFF, 0x80, 0x55, 0xCC, 0x11, 0xFF,
                                0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    const auto upload_result = shell.set_notation_surface(std::move(good));
    if (!upload_result.ok()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: valid surface rejected "
                   "post-open: %s\n",
                   upload_result.message.c_str());
      return 1;
    }

    auto       prior         = shell.test_read_notation_pixel(0, 0);
    const bool have_renderer = prior.has_value();
    if (!have_renderer) {
      // Renderer unavailable — honest skip.
      std::printf(
          "selection-tool-shell-test: no renderer — post-open surface "
          "validation skipped (renderer-unavailable)\n");
    }
    if (have_renderer) {
      if ((*prior)[0] != 0xAA || (*prior)[1] != 0x00 || (*prior)[2] != 0xFF ||
          (*prior)[3] != 0x80) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: pixel (0,0) mismatch before "
                     "invalid post-open call\n");
        return 1;
      }
      // Upload replaced old 1x1 texture: created=3, destroyed=2, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 3 || s.destroyed != 2) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "post-open good upload — "
                       "expected created=3 destroyed=2 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
    }

    // Post-open invalid: oversized buffer (2×2 needs 16, 20 supplied).
    if (have_renderer) {
      graphscore::RasterSurface bad;
      bad.width  = 2;
      bad.height = 2;
      bad.rgba.assign(20, 0x00);
      const graphscore::ShellResult set_result =
          shell.set_notation_surface(std::move(bad));
      if (set_result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: oversized buffer not rejected "
                     "post-open (got %s)\n",
                     describe(set_result.error));
        return 1;
      }
      // The prior valid texture must survive — pixel (0,0) still readable.
      auto after = shell.test_read_notation_pixel(0, 0);
      if (!after.has_value() || (*after)[0] != 0xAA || (*after)[1] != 0x00 ||
          (*after)[2] != 0xFF || (*after)[3] != 0x80) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: valid pixel destroyed after "
                     "invalid post-open call\n");
        return 1;
      }
      // Invalid upload created/destroyed nothing: alive still 1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 3 || s.destroyed != 2) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats changed after "
                       "invalid oversized post-open call — "
                       "expected created=3 destroyed=2 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
    }

    // Post-open invalid: one-zero dimension (0, 1).
    if (have_renderer) {
      graphscore::RasterSurface bad;
      bad.width  = 0;
      bad.height = 1;
      bad.rgba   = {0x00};
      const graphscore::ShellResult set_result =
          shell.set_notation_surface(std::move(bad));
      if (set_result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (0,1) not rejected "
                     "post-open (got %s)\n",
                     describe(set_result.error));
        return 1;
      }
      // Valid pixel still readable.
      auto after = shell.test_read_notation_pixel(0, 0);
      if (!after.has_value() || (*after)[0] != 0xAA || (*after)[1] != 0x00 ||
          (*after)[2] != 0xFF || (*after)[3] != 0x80) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: valid pixel destroyed after "
                     "(0,1) post-open call\n");
        return 1;
      }
      // Invalid upload created/destroyed nothing: alive still 1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 3 || s.destroyed != 2) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats changed after "
                       "invalid (0,1) post-open call — "
                       "expected created=3 destroyed=2 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
    }
  }

  // ---- transactional texture replacement: successful replace, successful
  //     clear, and test-injected failure ----
  // Verify that set_notation_surface atomically swaps textures on success
  // (old destroyed, new uploaded), clears to null on empty surface, and
  // preserves the prior surface when SDL operations fail.
  {
    // ---- successful replace ----
    // Upload surface A (red pattern), then surface B (green pattern).
    // After B, verify B is readable and the old A texture is gone.
    {
      graphscore::RasterSurface surface_a;
      surface_a.width  = 2;
      surface_a.height = 2;
      // All-red pixels: (0,0) through (1,1)
      surface_a.rgba = {0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF,
                        0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF};
      const graphscore::ShellResult set_a =
          shell.set_notation_surface(std::move(surface_a));
      if (!set_a.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface A rejected: %s\n",
                     set_a.message.c_str());
        return 1;
      }
      // Successful replace: created=4, destroyed=3, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 4 || s.destroyed != 3) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "surface A — expected created=4 destroyed=3 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      auto pixel_a = shell.test_read_notation_pixel(0, 0);
      if (!pixel_a.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback of surface A "
                     "returned nullopt after successful upload\n");
        return 1;
      }
      // Verify surface A is what we uploaded (red).
      if ((*pixel_a)[0] != 0xFF || (*pixel_a)[1] != 0x00 ||
          (*pixel_a)[2] != 0x00 || (*pixel_a)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface A readback "
                     "mismatch\n");
        return 1;
      }

      // Upload surface B (distinct green pattern), replacing A.
      graphscore::RasterSurface surface_b;
      surface_b.width  = 2;
      surface_b.height = 2;
      // All-green pixels
      surface_b.rgba = {0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
      const graphscore::ShellResult set_b =
          shell.set_notation_surface(std::move(surface_b));
      if (!set_b.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface B rejected: %s\n",
                     set_b.message.c_str());
        return 1;
      }
      // Successful replace: created=5, destroyed=4, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 5 || s.destroyed != 4) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "surface B — expected created=5 destroyed=4 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      // After successful replace, pixel (0,0) must read as green, not red.
      auto pixel_b = shell.test_read_notation_pixel(0, 0);
      if (!pixel_b.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback of surface B "
                     "returned nullopt after successful upload\n");
        return 1;
      }
      if ((*pixel_b)[0] != 0x00 || (*pixel_b)[1] != 0xFF ||
          (*pixel_b)[2] != 0x00 || (*pixel_b)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface B readback "
                     "mismatch (old texture may have leaked)\n");
        return 1;
      }
    }

    // ---- successful clear ----
    // After B, clear with canonical empty surface.  Texture must be null
    // and readback must return nullopt.
    {
      const graphscore::ShellResult clear_result =
          shell.set_notation_surface(graphscore::RasterSurface{});
      if (!clear_result.ok()) {
        std::fprintf(stderr, "selection-tool-shell-test: clear rejected: %s\n",
                     clear_result.message.c_str());
        return 1;
      }
      // Clear destroys texture: alive becomes zero.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 5 || s.destroyed != 5) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after clear "
                       "in replace block — "
                       "expected created=5 destroyed=5 alive=0, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      auto pixel_after_clear = shell.test_read_notation_pixel(0, 0);
      if (pixel_after_clear.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback after clear "
                     "returned pixel (expected nullopt)\n");
        return 1;
      }
    }

    // ---- test-injected failure: prior surface survives, candidate cleaned
    // ---- Upload a distinctive surface, then attempt a replacement with the
    // test injection seam forcing SDL failure.  The prior surface must
    // remain readable with its distinctive pixel values.
    {
      graphscore::RasterSurface survival;
      survival.width  = 2;
      survival.height = 2;
      // Distinctive: blue pixel at (0,0)
      survival.rgba = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
                       0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF};
      const graphscore::ShellResult set_survival =
          shell.set_notation_surface(std::move(survival));
      if (!set_survival.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: survival surface rejected: "
                     "%s\n",
                     set_survival.message.c_str());
        return 1;
      }
      // New upload: created=6, destroyed=5, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 6 || s.destroyed != 5) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "blue upload — expected created=6 destroyed=5 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      // Verify it's readable.
      auto survival_pixel = shell.test_read_notation_pixel(0, 0);
      if (!survival_pixel.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback of blue survival "
                     "pixel returned nullopt after successful upload\n");
        return 1;
      }
      if ((*survival_pixel)[0] != 0x00 || (*survival_pixel)[1] != 0x00 ||
          (*survival_pixel)[2] != 0xFF || (*survival_pixel)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: survival pixel mismatch\n");
        return 1;
      }

      // Inject a failure for the next set_notation_surface call.
      shell.set_test_force_texture_failure(true);

      // Attempt to replace with an orange surface — this must fail.
      graphscore::RasterSurface orange;
      orange.width  = 2;
      orange.height = 2;
      orange.rgba   = {0xFF, 0x80, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0xFF,
                       0xFF, 0x80, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0xFF};
      const graphscore::ShellResult fail_result =
          shell.set_notation_surface(std::move(orange));
      if (fail_result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: injected failure not "
                     "returned (got %s)\n",
                     describe(fail_result.error));
        return 1;
      }

      // The prior (blue) surface must survive — pixel (0,0) still blue.
      auto after_fail = shell.test_read_notation_pixel(0, 0);
      if (!after_fail.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: prior pixel destroyed after "
                     "injected failure (readback returned nullopt)\n");
        return 1;
      }
      if ((*after_fail)[0] != 0x00 || (*after_fail)[1] != 0x00 ||
          (*after_fail)[2] != 0xFF || (*after_fail)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: prior surface corrupted "
                     "after injected failure — expected blue, got "
                     "[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                     (*after_fail)[0], (*after_fail)[1], (*after_fail)[2],
                     (*after_fail)[3]);
        return 1;
      }
      // Candidate created and destroyed; prior blue survives: alive stays 1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 7 || s.destroyed != 6) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "injected failure — "
                       "expected created=7 destroyed=6 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }

      // The injection flag is one-shot (consumed by the failed call above);
      // verify the production path is not corrupted by re-uploading the blue
      // surface cleanly.
      {
        graphscore::RasterSurface replay;
        replay.width  = 2;
        replay.height = 2;
        replay.rgba   = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
                         0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF};
        if (!shell.set_notation_surface(std::move(replay)).ok()) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: post-injection recovery "
                       "upload failed\n");
          return 1;
        }
        // Recovery replaces prior blue: created=8, destroyed=7, alive=1.
        {
          auto s = shell.test_notation_texture_stats();
          if (s.created != 8 || s.destroyed != 7) {
            std::fprintf(stderr,
                         "selection-tool-shell-test: texture stats after "
                         "recovery upload — "
                         "expected created=8 destroyed=7 alive=1, "
                         "got created=%" PRIu64 " destroyed=%" PRIu64
                         " alive=%" PRIu64 "\n",
                         static_cast<std::uint64_t>(s.created),
                         static_cast<std::uint64_t>(s.destroyed),
                         static_cast<std::uint64_t>(s.created - s.destroyed));
            return 1;
          }
        }
      }
    }
  }

  // ---- committed-selection highlight persistence across drag+cancel ----
  // Commit a selection; press alone preserves the old committed highlight
  // (the handler now calls update_highlight on press, which shows the
  // committed extent since begin() clears live_extent).  After a different
  // live move, highlight changes to the live geometry.  A transient cancel
  // restores the exact original committed geometry.
  //
  // Uses dispatch_sdl_test_pointer_event to exercise the production
  // SDL_ConvertEventToRenderCoordinates path.
  {
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    // Commit: press → move → release (production SDL path).
    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_sdl_test_pointer_event(1, make_event(move_x, press_y));
    shell.dispatch_sdl_test_pointer_event(2, make_event(move_x, press_y));

    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress after "
                   "commit-release\n");
      return 1;
    }
    if (!handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: no committed selection after "
                   "release\n");
      return 1;
    }

    // Snapshot the shell-visible highlight after commit.
    const std::vector<graphscore::NotationRect> committed_rects =
        shell.test_snapshot_highlight_rects();
    if (committed_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects empty "
                   "after commit (expected at least one rect)\n");
      return 1;
    }

    // Begin a second drag: press alone preserves the committed highlight
    // (on_pointer_press now calls update_highlight, which falls through to
    // committed_selection when live_extent is empty).
    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: second drag did not begin\n");
      return 1;
    }
    // Committed highlight must still be present (press does not clear it).
    const std::vector<graphscore::NotationRect> after_press_rects =
        shell.test_snapshot_highlight_rects();
    if (after_press_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects lost after "
                   "second press (expected committed highlight preserved)\n");
      return 1;
    }
    if (after_press_rects != committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects changed after "
                   "second press (expected committed highlight unchanged)\n");
      return 1;
    }

    // Move to a demonstrably different endpoint (half-measure):
    // the live highlight must differ from the committed highlight.
    const double half_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width * 0.5;
    shell.dispatch_sdl_test_pointer_event(1, make_event(half_x, press_y));
    const std::vector<graphscore::NotationRect> live_rects =
        shell.test_snapshot_highlight_rects();
    if (live_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects empty after "
                   "second-drag move\n");
      return 1;
    }
    if (live_rects == committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: live highlight did not differ "
                   "from committed after move to half-measure endpoint\n");
      return 1;
    }

    // Cancel the second drag (simulating focus-loss or dismissal).
    handler.on_cancel();

    // After cancel, the committed selection highlight must be restored
    // exactly.
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress after "
                   "cancel\n");
      return 1;
    }
    const std::vector<graphscore::NotationRect> restored_rects =
        shell.test_snapshot_highlight_rects();
    if (restored_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects empty "
                   "after cancel (expected committed highlight restored)\n");
      return 1;
    }
    if (restored_rects != committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: restored highlight rects "
                   "differ from committed rects\n");
      return 1;
    }
  }

  // ---- destructor destroys live notation texture ----
  // Acquire a stats handle from a dedicated WriterShell scope, upload
  // one texture, verify alive=1, then let the shell go out of scope.
  // The handle survives destruction; assert Impl::~Impl destroyed the
  // texture (alive=0).
  {
    bool                                                       dtor_ran = false;
    std::optional<graphscore::WriterShell::TextureStatsHandle> dtor_handle;

    {
      graphscore::WriterShell dtor_shell;
      dtor_handle = dtor_shell.test_acquire_texture_stats_handle();

      graphscore::WindowOptions opts;
      opts.run_event_loop                       = false;
      const graphscore::ShellResult open_result = dtor_shell.open_window(opts);
      const bool                    is_headless =
          open_result.error == graphscore::ShellError::kBackendUnavailable ||
          open_result.error == graphscore::ShellError::kRendererUnavailable;
      if (!open_result.ok()) {
        if (is_headless) {
          std::printf(
              "selection-tool-shell-test: renderer unavailable in "
              "destructor scope — skipped\n");
          dtor_handle.reset();
        } else {
          std::fprintf(stderr,
                       "selection-tool-shell-test: destructor-scope "
                       "open_window failed: %s\n",
                       open_result.message.c_str());
          shell.set_input_handler(nullptr);
          return 1;
        }
      } else {
        // Upload one texture and verify alive=1.
        graphscore::RasterSurface surface;
        surface.width  = 2;
        surface.height = 2;
        surface.rgba.assign(16, 0xFF);
        const auto set_result =
            dtor_shell.set_notation_surface(std::move(surface));
        if (!set_result.ok()) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: destructor-scope upload "
                       "failed: %s\n",
                       set_result.message.c_str());
          shell.set_input_handler(nullptr);
          return 1;
        }
        auto s = dtor_handle->snapshot();
        if (s.created != 1 || s.destroyed != 0) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: destructor scope "
                       "alive != 1 before destruction — "
                       "created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          shell.set_input_handler(nullptr);
          return 1;
        }
        dtor_ran = true;
      }
      // dtor_shell destroyed here → ~Impl runs if window was opened
    }

    // Handle survived; verify destructor destroyed the texture.
    if (dtor_ran && dtor_handle.has_value()) {
      auto s = dtor_handle->snapshot();
      if (s.created != s.destroyed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture leak after shell "
                     "destruction — "
                     "created=%" PRIu64 " destroyed=%" PRIu64 " alive=%" PRIu64
                     "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }

  // Clear the notation surface to destroy the final texture, then assert
  // no texture remains alive (no leak to the shell's destructor).
  shell.set_notation_surface(graphscore::RasterSurface{});
  {
    auto s = shell.test_notation_texture_stats();
    if (s.created != s.destroyed) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: texture leak at test end — "
                   "created=%" PRIu64 " destroyed=%" PRIu64 " alive=%" PRIu64
                   "\n",
                   static_cast<std::uint64_t>(s.created),
                   static_cast<std::uint64_t>(s.destroyed),
                   static_cast<std::uint64_t>(s.created - s.destroyed));
      return 1;
    }
  }

  std::printf("selection-tool-shell-test: ok\n");
  shell.set_input_handler(nullptr);
  return 0;
}

}  // namespace graphscore::writer_app
