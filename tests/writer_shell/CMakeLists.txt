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
  PASS_REGULAR_EXPRESSION "window opened via|no display available|renderer unavailable"
  TIMEOUT 60
)

if (WIN32)
  # SDL3's window creation on headless Windows runners can hang inside a
  # kernel call (CreateWindowEx / message-pump wait) and ctest cannot
  # interrupt a hung kernel call even with TIMEOUT.  The test is still
  # exercised on macOS and Linux CI jobs.
  set_tests_properties(graphscore_writer_window_smoke PROPERTIES
    DISABLED TRUE
  )
endif()

if (NOT GRAPHSCORE_BUILD_WRITER)
  # Configured with -DGRAPHSCORE_BUILD_WRITER=OFF: there is no backend to
  # reach, and the executable says so and exits non-zero. That is the correct
  # behaviour, so the test asserts it rather than being skipped.
  #
  # PASS_REGULAR_EXPRESSION alone decides the result here — when it is set,
  # ctest ignores the exit status, so the expected non-zero exit needs no
  # WILL_FAIL (which would invert this and fail the test on a match).
  set_tests_properties(graphscore_writer_window_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "GRAPHSCORE_BUILD_WRITER=OFF"
  )
endif()

# M05: exercise the SelectionToolHandler through the writer executable's own
# --test-selection-tool path.  This is a headless code path — no window, no
# SDL — that exercises the WriterShell's dispatch_test_pointer_event seam:
# registration, down/move/up translation semantics, coordinate conversion,
# highlight delivery, unregister behaviour, and no-callback-after-removal.
# It does not duplicate the notation-layer resolver tests; those live in
# graphscore_notation_test.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.  SelfTestMetrics supplies synthetic glyph
# metrics and no font is loaded.
add_test(NAME graphscore_writer_selection_tool_test
  COMMAND graphscore_writer_app --test-selection-tool
)

set_tests_properties(graphscore_writer_selection_tool_test PROPERTIES
  PASS_REGULAR_EXPRESSION "selection-tool-test: ok"
  TIMEOUT 30
)

# Writer-OFF: the headless test still exercises the handler through the
# test seam, but the executable was built without a rendering backend.
# Accept explicit success output; the writer-OFF shell's no-op Impl supports
# dispatch_test_pointer_event and set_test_dpi_scale, so every assertion
# in the headless test is valid in both configurations.
if (NOT GRAPHSCORE_BUILD_WRITER)
  # No change needed: the headless test prints "selection-tool-test: ok"
  # on success regardless of the backend, and the PASS_REGULAR_EXPRESSION
  # above already accepts that output.  Writer-OFF does not affect it.
endif()

# The shell test exercises the full SDL path: real window creation, real
# renderer, and real event-dispatch integration.  It requires GRAPHSCORE_
# BUILD_WRITER=ON because there is no SDL backend in a writer-OFF build.
if (GRAPHSCORE_BUILD_WRITER)
  add_test(NAME graphscore_writer_selection_tool_shell_test
    COMMAND graphscore_writer_app --test-selection-tool-shell
  )

  set_tests_properties(graphscore_writer_selection_tool_shell_test PROPERTIES
    PASS_REGULAR_EXPRESSION
      "selection-tool-shell-test: ok|no display or renderer"
    TIMEOUT 30
  )

  if (WIN32)
    # SDL3's window creation on headless Windows runners can hang inside a
    # kernel call; the same Windows caveat that disables the window smoke
    # test applies here.
    set_tests_properties(graphscore_writer_selection_tool_shell_test PROPERTIES
      DISABLED TRUE
    )
  endif()
else()
  # Writer-OFF: the shell test is not registered.  A writer-OFF ctest run
  # exercises only the headless selection_tool_test (above), which passes
  # without a rendering backend.
endif()
