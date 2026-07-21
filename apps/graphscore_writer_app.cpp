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
#include <exception>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kSmokeTestFlag = "--smoke-test";

const char* describe(graphscore::ShellError error) {
  switch (error) {
    case graphscore::ShellError::kNone:
      return "ok";
    case graphscore::ShellError::kBackendUnavailable:
      return "no display available";
    case graphscore::ShellError::kWindowCreationFailed:
      return "window creation failed";
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

  // A headless machine is a normal environment for a CI runner, not a defect
  // in the writer. Under --smoke-test that is a pass: the shell reached the
  // backend, and the backend reported no display. Every other failure is
  // real, and no display without --smoke-test means the user asked for a
  // window that cannot be shown.
  const bool headless_under_smoke_test =
      smoke_test && result.error == graphscore::ShellError::kBackendUnavailable;

  return headless_under_smoke_test ? 0 : 1;
}

int run(bool smoke_test) {
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

}  // namespace

// The shell allocates (window titles, backend names), so this call path can
// throw. Letting an exception escape `main` gives std::terminate and an
// unhelpful abort; catching it here turns the same failure into a diagnostic
// and a non-zero exit. Note that the realtime prohibition on exceptions
// applies to the runtime's process path, not to the writer application.
int main(int argc, char** argv) {
  bool smoke_test = false;
  for (int i = 1; i < argc; ++i) {
    if (kSmokeTestFlag == argv[i]) {
      smoke_test = true;
    }
  }

  try {
    return run(smoke_test);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "graphscore_writer_app: unhandled exception: %s\n",
                 error.what());
    return 1;
  } catch (...) {
    std::fprintf(stderr, "graphscore_writer_app: unhandled exception\n");
    return 1;
  }
}
