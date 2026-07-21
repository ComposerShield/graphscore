// SPDX-License-Identifier: Apache-2.0
//
// GraphScore Writer application entry point.
//
// M1 scope: open an empty native window on each desktop platform and run the
// event loop until it is closed. Document lifecycle, canvas, notation
// editing, and audio arrive in later milestones; this executable exists now
// so that the platform shell is continuously built and exercised.
//
// `--smoke-test` creates and destroys the window without entering the
// blocking event loop, so CI can verify the shell without anyone closing a
// window.

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kSmokeTestFlag = "--smoke-test";

int report(const graphscore::ShellResult& result, bool smoke_test) {
  switch (result.error) {
    case graphscore::ShellError::kNone:
      return 0;

    case graphscore::ShellError::kBackendUnavailable:
      // A headless machine is a normal environment for a CI runner, not a
      // defect in the writer. Under --smoke-test this is a pass: the shell
      // reached the backend and the backend reported no display.
      std::fprintf(stderr, "graphscore_writer_app: no display available (%s)\n",
                   result.message.c_str());
      return smoke_test ? 0 : 1;

    case graphscore::ShellError::kWindowCreationFailed:
      std::fprintf(stderr,
                   "graphscore_writer_app: window creation failed (%s)\n",
                   result.message.c_str());
      return 1;

    case graphscore::ShellError::kBackendNotCompiledIn:
      std::fprintf(stderr, "graphscore_writer_app: %s\n",
                   result.message.c_str());
      return 1;
  }

  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  bool smoke_test = false;
  for (int i = 1; i < argc; ++i) {
    if (kSmokeTestFlag == argv[i]) {
      smoke_test = true;
    }
  }

  graphscore::WriterShell shell;

  graphscore::WindowOptions options;
  options.run_event_loop = !smoke_test;

  const graphscore::ShellResult result = shell.open_window(options);
  const int                     status = report(result, smoke_test);

  if (result.ok()) {
    std::printf("graphscore_writer_app: window opened via '%s' backend\n",
                std::string(shell.backend_name()).c_str());
  }

  return status;
}
