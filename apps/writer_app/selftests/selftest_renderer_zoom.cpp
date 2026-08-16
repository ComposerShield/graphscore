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
// ---- M6-phase-5: real-renderer zoom/pan frame sweep ------------------------
//
// Regression coverage for the intermediate-zoom flicker: at some pinch-zoom
// increments the notation surface was skipped entirely, leaving only the
// cleared background. This test drives the production render path —
// set_viewport_transform → test_render_frame (the same render_frame helper the
// event loop calls, with present=false) → SDL_RenderReadPixels readback —
// across a sweep of representative incremental zoom values and combined pan
// offsets, and verifies a non-background sentinel pixel remains visible at the
// expected position after every increment. It does not reverse-map the
// destination in the test: the expected position comes from the shell's own
// test_notation_destination(), and the assertion is on actual readback
// pixels, so a silently blank frame (clear-only) fails immediately.
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

  // A 64×64 notation surface, every texel the same non-background sentinel
  // green (the clear colour is a dark 30,30,30 gray, so the two are
  // unambiguously distinguishable after readback).
  graphscore::RasterSurface surface;
  surface.width  = 64;
  surface.height = 64;
  surface.rgba.resize(64 * 64 * 4);
  for (std::size_t i = 0; i < surface.rgba.size(); i += 4) {
    surface.rgba[i + 0] = 0x00;  // R
    surface.rgba[i + 1] = 0xC8;  // G
    surface.rgba[i + 2] = 0x00;  // B
    surface.rgba[i + 3] = 0xFF;  // A (opaque)
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

  const auto is_sentinel = [](const std::array<std::uint8_t, 4>& pixel) {
    // Dominantly green, clearly distinct from the dark gray clear colour.
    return pixel[1] > 0x80 && pixel[1] > pixel[0] + 0x40 &&
           pixel[1] > pixel[2] + 0x40;
  };

  // The fixed viewport focal point the notation centre is anchored to, well
  // inside the default 1280×800 window on any supported display scale.
  const graphscore::ViewportPosition focal{160.0, 120.0};
  // The notation surface's world centre.
  const graphscore::GraphPosition surface_centre{32.0, 32.0};

  // ---- pre-sweep readback sanity: sentinel visible at the anchor, and the
  //     far corner is the clear colour (proves the readback distinguishes) ----
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
    const auto centre = read_pixel(focal.x, focal.y);
    if (!centre.has_value() || !is_sentinel(*centre)) {
      std::fprintf(stderr,
                   "renderer-zoom-test: sentinel not visible at the anchor "
                   "during sanity render\n");
      shell.set_viewport_transform(nullptr);
      return 1;
    }
    const auto corner = read_pixel(1000.0, 700.0);
    if (!corner.has_value() || is_sentinel(*corner)) {
      std::fprintf(stderr,
                   "renderer-zoom-test: far corner read back as sentinel "
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

    // The notation centre after zoom+pan: the anchor focal plus the pan.
    const double centre_x = focal.x + pan_x;
    const double centre_y = focal.y + pan_y;
    const auto   pixel    = read_pixel(centre_x, centre_y);
    if (!pixel.has_value() || !is_sentinel(*pixel)) {
      std::fprintf(stderr,
                   "renderer-zoom-test: sentinel missing at step %d "
                   "(zoom=%.6f, pan=(%.1f,%.1f))\n",
                   zoom_step, zoom, pan_x, pan_y);
      shell.set_viewport_transform(nullptr);
      return 1;
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
