// SPDX-License-Identifier: Apache-2.0

#include "app_shell_report.hpp"

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdio>

namespace graphscore::writer_app {
const char* describe(graphscore::ShellError error) {
  switch (error) {
    case graphscore::ShellError::kNone:
      return "ok";
    case graphscore::ShellError::kBackendUnavailable:
      return "no display available";
    case graphscore::ShellError::kWindowCreationFailed:
      return "window creation failed";
    case graphscore::ShellError::kRendererUnavailable:
      return "renderer unavailable";
    case graphscore::ShellError::kRenderingSetupFailed:
      return "rendering failed";
    case graphscore::ShellError::kBackendNotCompiledIn:
      return "no windowing backend compiled in";
  }
  return "unknown error";
}

int report(const graphscore::ShellResult& result, bool smoke_test) {
  if (result.ok()) {
    return 0;
  }

  std::fprintf(stderr, "graphscore_writer_app: %s (%s)\n",
               describe(result.error), result.message.c_str());

  // Only a genuinely absent display is a benign headless outcome under
  // --smoke-test: kBackendUnavailable means SDL video initialisation itself
  // failed (no display, SSH session, container). kRendererUnavailable means
  // video initialised and the window was created but the ADR-locked GPU
  // renderer failed — a real defect on a display-capable machine, not a
  // headless skip, so it must not exit as success.
  const bool headless_under_smoke_test =
      smoke_test && result.error == graphscore::ShellError::kBackendUnavailable;

  return headless_under_smoke_test ? 0 : 1;
}

}  // namespace graphscore::writer_app
