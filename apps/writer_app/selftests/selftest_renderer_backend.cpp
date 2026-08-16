// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdio>
#include <string_view>

namespace graphscore::writer_app {

// ---- M6-phase-5: ADR 0002 §A5 renderer backend selection ------------------
//
// The writer shell must request a fixed, per-platform GPU backend from
// SDL_CreateRenderer — Metal on macOS, D3D11 on Windows, OpenGL on Linux —
// never unnamed auto-selection (which silently tries every compiled driver
// and discards each backend's specific failure). This headless test asserts
// the production selection the shell will pass, without requiring a display,
// so a regression to auto-selection — or a wrong platform backend — fails on
// every build host, not only on a display-attached one.
int renderer_backend_test() {
  const std::string_view selected =
      graphscore::WriterShell::test_renderer_driver_name();

  const bool matches =
#if defined(__APPLE__)
      selected == "metal";
#elif defined(_WIN32)
      selected == "direct3d11";
#else
      selected == "opengl";
#endif

  if (!matches) {
    std::fprintf(stderr,
                 "renderer-backend-test: selected backend \"%.*s\" does not "
                 "match the ADR 0002 §A5 backend for this platform\n",
                 static_cast<int>(selected.size()), selected.data());
    return 1;
  }

  std::printf("renderer-backend-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
