// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace graphscore::writer_app {
// ---- M6-phase-5/8: real-renderer zoom/pan notation-detail sweep ------------
//
// Regression coverage for the intermediate-zoom flicker: at some pinch-zoom
// increments the notation surface was skipped entirely, leaving only the
// cleared background. This test drives the production render path —
// set_viewport_transform → test_render_frame (the same render_frame helper the
// event loop calls, with present=false) → SDL_RenderReadPixels readback —
// across a sweep of representative incremental zoom values and combined pan
// offsets, and verifies four distinguishable notation elements remain visible
// at their expected positions after every increment. The assertion is on
// actual readback pixels, so a silently blank frame or zoom-dependent summary
// substitution fails immediately.
//
// This is a composition-readback proof: it proves the composed back buffer is
// not blank (present=false leaves the composed frame readable). The observable
// queued-command flush/API gate is a separate gate exercised by the
// renderer_present_test, not here. The sweep ends with one nonblocking present
// smoke via test_present_frame() so a flush/API failure is surfaced on the
// same host, but no readback follows that present, and physical presentation
// remains manual/smoke evidence only.
//
// The sweep anchors the notation surface's world centre under a fixed
// viewport focal point, so the sentinel is guaranteed to occupy that focal
// point at every zoom; a pan then offsets it, and the assertion follows the
// offset. Only a genuinely absent display (kBackendUnavailable) is a benign
// headless skip; a renderer or rendering-setup failure is a hard failure.
int renderer_zoom_test() {
  graphscore::WriterShell shell;

  enum class ElementColor : std::uint8_t { kRed, kGreen, kBlue, kYellow };

  struct Element {
    graphscore::GraphPosition position;
    ElementColor              color;
  };

  constexpr std::array<Element, 4> elements{{
      {{16.0, 16.0}, ElementColor::kRed},
      {{48.0, 16.0}, ElementColor::kGreen},
      {{16.0, 48.0}, ElementColor::kBlue},
      {{48.0, 48.0}, ElementColor::kYellow},
  }};

  // A textured 64×64 retained notation surface. The four colored blocks stand
  // in for independently meaningful notation elements; all must survive every
  // zoom level rather than being replaced by one aggregate marker.
  graphscore::RasterSurface surface;
  surface.width  = 64;
  surface.height = 64;
  surface.rgba.resize(64 * 64 * 4, 0xFF);
  for (std::size_t i = 0; i < surface.rgba.size(); i += 4) {
    surface.rgba[i + 0] = 0xC0;
    surface.rgba[i + 1] = 0xC0;
    surface.rgba[i + 2] = 0xC0;
  }
  const auto paint_block = [&](const Element& element) {
    for (int y = static_cast<int>(element.position.y) - 6;
         y < static_cast<int>(element.position.y) + 6; ++y) {
      for (int x = static_cast<int>(element.position.x) - 6;
           x < static_cast<int>(element.position.x) + 6; ++x) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * surface.width +
             static_cast<std::size_t>(x)) *
            4;
        surface.rgba[offset + 0] =
            element.color == ElementColor::kRed ||
                    element.color == ElementColor::kYellow
                ? 0xE0
                : 0x10;
        surface.rgba[offset + 1] =
            element.color == ElementColor::kGreen ||
                    element.color == ElementColor::kYellow
                ? 0xE0
                : 0x10;
        surface.rgba[offset + 2] =
            element.color == ElementColor::kBlue ? 0xE0 : 0x10;
      }
    }
  };
  for (const Element& element : elements) {
    paint_block(element);
  }
  const graphscore::ShellResult set_result =
      shell.set_notation_surface(std::move(surface));
  if (!set_result.ok()) {
    std::fprintf(stderr,
                 "renderer-zoom-test: could not register the notation "
                 "surface: %s\n",
                 set_result.message.c_str());
    return 1;
  }

  graphscore::WindowOptions options;
  options.run_event_loop                    = false;
  const graphscore::ShellResult open_result = shell.open_window(options);
  if (!open_result.ok()) {
    const bool is_benign_headless =
        open_result.error == graphscore::ShellError::kBackendUnavailable;
    std::fprintf(stderr, "renderer-zoom-test: open_window failed: %s\n",
                 is_benign_headless
                     ? "no display available — headless host (skip)"
                     : open_result.message.c_str());
    return is_benign_headless ? 0 : 1;
  }

  const double scale_x = shell.test_dpi_scale_x();
  const double scale_y = shell.test_dpi_scale_y();
  if (!std::isfinite(scale_x) || scale_x <= 0.0 || !std::isfinite(scale_y) ||
      scale_y <= 0.0) {
    std::fprintf(stderr,
                 "renderer-zoom-test: invalid DPI scale (x=%.3f, y=%.3f)\n",
                 scale_x, scale_y);
    return 1;
  }

  const auto read_pixel =
      [&](double logical_x,
          double logical_y) -> std::optional<std::array<std::uint8_t, 4>> {
    const std::uint32_t px =
        static_cast<std::uint32_t>(std::lround(logical_x * scale_x));
    const std::uint32_t py =
        static_cast<std::uint32_t>(std::lround(logical_y * scale_y));
    return shell.test_read_backbuffer_pixel(px, py);
  };

  const auto matches_element = [](const std::array<std::uint8_t, 4>& pixel,
                                  ElementColor                       color) {
    constexpr std::uint8_t kHigh = 0xA0;
    constexpr std::uint8_t kLow  = 0x60;
    switch (color) {
      case ElementColor::kRed:
        return pixel[0] > kHigh && pixel[1] < kLow && pixel[2] < kLow;
      case ElementColor::kGreen:
        return pixel[0] < kLow && pixel[1] > kHigh && pixel[2] < kLow;
      case ElementColor::kBlue:
        return pixel[0] < kLow && pixel[1] < kLow && pixel[2] > kHigh;
      case ElementColor::kYellow:
        return pixel[0] > kHigh && pixel[1] > kHigh && pixel[2] < kLow;
    }
    return false;
  };
  const auto is_clear_color = [](const std::array<std::uint8_t, 4>& pixel) {
    return pixel[0] < 0x50 && pixel[1] < 0x50 && pixel[2] < 0x50;
  };

  // The fixed viewport focal point the notation centre is anchored to, well
  // inside the default 1280×800 window on any supported display scale.
  const graphscore::ViewportPosition focal{320.0, 240.0};
  // The notation surface's world centre.
  const graphscore::GraphPosition surface_centre{32.0, 32.0};

  // ---- pre-sweep readback sanity: every element is visible, and the far
  //     corner is the clear colour (proves readback distinguishes it) ------
  {
    graphscore::ViewportTransform identity;
    if (!identity.set_anchor(surface_centre, focal) ||
        !identity.zoom_to(1.0, focal)) {
      std::fprintf(stderr,
                   "renderer-zoom-test: could not initialise the sanity "
                   "transform\n");
      return 1;
    }
    shell.set_viewport_transform(&identity);
    const graphscore::ShellResult frame = shell.test_render_frame();
    if (!frame.ok()) {
      std::fprintf(stderr, "renderer-zoom-test: sanity frame failed: %s\n",
                   frame.message.c_str());
      shell.set_viewport_transform(nullptr);
      return 1;
    }
    for (const Element& element : elements) {
      const auto viewport = identity.to_viewport(element.position);
      const auto pixel    = viewport.has_value()
                                ? read_pixel(viewport->x, viewport->y)
                                : std::nullopt;
      if (!pixel.has_value() || !matches_element(*pixel, element.color)) {
        std::fprintf(stderr,
                     "renderer-zoom-test: notation element missing during "
                     "sanity render\n");
        shell.set_viewport_transform(nullptr);
        return 1;
      }
    }
    const auto corner = read_pixel(1000.0, 700.0);
    if (!corner.has_value() || !is_clear_color(*corner)) {
      std::fprintf(stderr,
                   "renderer-zoom-test: far corner did not read as background "
                   "(readback cannot distinguish background)\n");
      shell.set_viewport_transform(nullptr);
      return 1;
    }
  }

  // ---- the sweep: representative incremental zoom values and combined pan
  //     offsets, every increment rendered and read back -------------------
  int frames_verified = 0;
  for (int zoom_step = -140; zoom_step <= 140; ++zoom_step) {
    // exp(step*0.01) sweeps roughly 0.247× .. 4.06× in fine increments: the
    // ordinary pinch-zoom continuum, not just power-of-two scales.
    const double zoom = std::exp(static_cast<double>(zoom_step) * 0.01);
    // A small, deterministic combined pan so both anchors move between
    // increments.
    const double pan_x = 3.0 * static_cast<double>(zoom_step % 5);
    const double pan_y = -2.0 * static_cast<double>((zoom_step / 5) % 5);

    graphscore::ViewportTransform transform;
    if (!transform.set_anchor(surface_centre, focal)) {
      std::fprintf(stderr,
                   "renderer-zoom-test: set_anchor rejected at step %d\n",
                   zoom_step);
      shell.set_viewport_transform(nullptr);
      return 1;
    }
    if (!transform.zoom_to(zoom, focal)) {
      std::fprintf(stderr,
                   "renderer-zoom-test: zoom_to rejected zoom=%.6f at step "
                   "%d\n",
                   zoom, zoom_step);
      shell.set_viewport_transform(nullptr);
      return 1;
    }
    if (!transform.pan_by({pan_x, pan_y})) {
      std::fprintf(stderr, "renderer-zoom-test: pan_by rejected at step %d\n",
                   zoom_step);
      shell.set_viewport_transform(nullptr);
      return 1;
    }
    shell.set_viewport_transform(&transform);

    // The production destination must be a positive, non-collapsed rect.
    const auto dest = shell.test_notation_destination();
    if (!dest.has_value() || dest->width <= 0.0 || dest->height <= 0.0) {
      std::fprintf(stderr,
                   "renderer-zoom-test: notation destination invalid at step "
                   "%d (zoom=%.6f)\n",
                   zoom_step, zoom);
      shell.set_viewport_transform(nullptr);
      return 1;
    }

    const graphscore::ShellResult frame = shell.test_render_frame();
    if (!frame.ok()) {
      std::fprintf(stderr,
                   "renderer-zoom-test: frame failed at step %d (zoom=%.6f): "
                   "%s\n",
                   zoom_step, zoom, frame.message.c_str());
      shell.set_viewport_transform(nullptr);
      return 1;
    }

    for (const Element& element : elements) {
      const auto viewport = transform.to_viewport(element.position);
      const auto pixel    = viewport.has_value()
                                ? read_pixel(viewport->x, viewport->y)
                                : std::nullopt;
      if (!pixel.has_value() || !matches_element(*pixel, element.color)) {
        std::fprintf(stderr,
                     "renderer-zoom-test: notation element missing at step "
                     "%d (zoom=%.6f, pan=(%.1f,%.1f))\n",
                     zoom_step, zoom, pan_x, pan_y);
        shell.set_viewport_transform(nullptr);
        return 1;
      }
    }
    ++frames_verified;
  }

  shell.set_viewport_transform(nullptr);

  // Present smoke: the observable queued-command flush/API gate must not fail
  // after the sweep. This surfaces a flush/API failure separately from the
  // composition readback above, which used test_render_frame() (present=false)
  // because a presented frame's back buffer is invalidated and cannot be read
  // back portably; physical presentation remains manual/smoke evidence only.
  const graphscore::ShellResult present = shell.test_present_frame();
  if (!present.ok()) {
    std::fprintf(stderr,
                 "renderer-zoom-test: present failed after the sweep: %s\n",
                 present.message.c_str());
    return 1;
  }

  std::printf("renderer-zoom-test: ok (%d frames verified)\n", frames_verified);
  return 0;
}

}  // namespace graphscore::writer_app
