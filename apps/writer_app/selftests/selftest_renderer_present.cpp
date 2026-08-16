// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <cstdio>
#include <utility>

namespace graphscore::writer_app {
// ---- M6-phase-5: real-renderer present + flush-failure gate ---------------
//
// Two proofs that need a real renderer (display-attached; a genuinely absent
// display is a benign skip):
//
//  1. Nonblocking present smoke: test_present_frame() runs the production
//     render pass with present=true and returns its ShellResult, so an
//     observable queued-command flush/API failure is surfaced. It makes no
//     readback claim — a presented frame's back buffer is invalidated, and
//     physical presentation is manual/smoke evidence only: a backend failure
//     that occurs only at the present step is unobservable at the pinned SDL
//     SHA, where SDL_RenderPresent returns true unconditionally.
//
//  2. Flush-failure injection: set_test_force_render_flush_failure forces the
//     next present to fail as though SDL_FlushRenderer had returned false,
//     which must surface as kRenderingSetupFailed before SDL_RenderPresent is
//     reached. The test-only present-call counter — incremented at the actual
//     SDL_RenderPresent call site — independently proves the present was
//     skipped: it must not advance on the injected failure, then must advance
//     on recovery.
int renderer_present_test() {
  graphscore::WriterShell shell;

  // A small opaque sentinel surface, so the render pass has notation to draw.
  graphscore::RasterSurface surface;
  surface.width  = 32;
  surface.height = 32;
  surface.rgba.assign(32 * 32 * 4, 0x00);
  for (std::size_t i = 0; i < surface.rgba.size(); i += 4) {
    surface.rgba[i + 3] = 0xFF;  // opaque
  }
  if (!shell.set_notation_surface(std::move(surface)).ok()) {
    std::fprintf(stderr,
                 "renderer-present-test: could not register the notation "
                 "surface\n");
    return 1;
  }

  graphscore::WindowOptions options;
  options.run_event_loop                    = false;
  const graphscore::ShellResult open_result = shell.open_window(options);
  if (!open_result.ok()) {
    const bool is_benign_headless =
        open_result.error == graphscore::ShellError::kBackendUnavailable;
    std::fprintf(stderr, "renderer-present-test: open_window failed: %s\n",
                 is_benign_headless
                     ? "no display available — headless host (skip)"
                     : open_result.message.c_str());
    return is_benign_headless ? 0 : 1;
  }

  // Present smoke: a real frame runs the flush/API gate without failure and
  // reaches SDL_RenderPresent.
  const graphscore::ShellResult presented = shell.test_present_frame();
  if (!presented.ok()) {
    std::fprintf(stderr, "renderer-present-test: initial present failed: %s\n",
                 presented.message.c_str());
    return 1;
  }
  const std::uint64_t calls_before_injection = shell.test_present_call_count();
  if (calls_before_injection < 1) {
    std::fprintf(stderr,
                 "renderer-present-test: initial present did not reach "
                 "SDL_RenderPresent\n");
    return 1;
  }

  // Flush-failure injection: the next present must surface
  // kRenderingSetupFailed before SDL_RenderPresent, and the present-call
  // counter must not advance — the flush gate skipped presentation.
  shell.set_test_force_render_flush_failure(true);
  const graphscore::ShellResult injected = shell.test_present_frame();
  if (injected.error != graphscore::ShellError::kRenderingSetupFailed) {
    std::fprintf(stderr,
                 "renderer-present-test: injected flush failure not surfaced "
                 "(got %s)\n",
                 injected.message.c_str());
    return 1;
  }
  if (shell.test_present_call_count() != calls_before_injection) {
    std::fprintf(stderr,
                 "renderer-present-test: injected flush failure advanced the "
                 "present-call counter (presentation was not skipped)\n");
    return 1;
  }

  // The injection is one-shot: the next present succeeds again and reaches
  // SDL_RenderPresent (counter advances).
  const graphscore::ShellResult recovered = shell.test_present_frame();
  if (!recovered.ok()) {
    std::fprintf(stderr,
                 "renderer-present-test: present did not recover after the "
                 "one-shot injection: %s\n",
                 recovered.message.c_str());
    return 1;
  }
  if (shell.test_present_call_count() != calls_before_injection + 1) {
    std::fprintf(stderr,
                 "renderer-present-test: recovered present did not advance "
                 "the present-call counter\n");
    return 1;
  }

  std::printf("renderer-present-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
