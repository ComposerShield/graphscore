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

  const bool headless_under_smoke_test =
      smoke_test &&
      (result.error == graphscore::ShellError::kBackendUnavailable ||
       result.error == graphscore::ShellError::kRendererUnavailable);

  return headless_under_smoke_test ? 0 : 1;
}

}  // namespace graphscore::writer_app
