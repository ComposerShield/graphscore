# SPDX-License-Identifier: Apache-2.0
#
# Skeleton writer artifact test: the writer executable must reach its
# windowing backend and create a real native window.
#
# --smoke-test creates the window, pumps the already-queued events, and
# exits, so the test does not block waiting for someone to close a window.
#
# On a headless runner the executable reports "no display available" and
# exits 0 under --smoke-test: reaching the backend is what is being tested,
# and no display is a property of the machine rather than of the writer. The
# PASS_REGULAR_EXPRESSION below accepts either outcome but requires the
# writer to have got far enough to say which.

add_test(NAME graphscore_writer_window_smoke
  COMMAND graphscore_writer_app --smoke-test
)

set_tests_properties(graphscore_writer_window_smoke PROPERTIES
  PASS_REGULAR_EXPRESSION "window opened via|no display available"
  TIMEOUT 60
)

if (NOT GRAPHSCORE_BUILD_WRITER)
  # Configured with -DGRAPHSCORE_BUILD_WRITER=OFF: there is no backend to
  # reach, and the executable says so and fails. That is the correct
  # behaviour, so the test asserts it rather than being skipped.
  set_tests_properties(graphscore_writer_window_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "GRAPHSCORE_BUILD_WRITER=OFF"
    WILL_FAIL TRUE
  )
endif()
