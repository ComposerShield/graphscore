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

# M5-phase-19b-ii: exercise WriterShell's platform-neutral key-event
# delivery through the writer executable's own --test-key-events path.
# This is a headless code path — no window, no SDL — that exercises
# dispatch_test_key_event: KeyCode/modifier round-trip, no-handler and
# post-unregistration no-op behaviour, and the InputHandler::on_key_press
# default no-op. It does not interpret keys into selection changes; that
# is M5-phase-19b-iii.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_key_events_test
  COMMAND graphscore_writer_app --test-key-events
)

set_tests_properties(graphscore_writer_key_events_test PROPERTIES
  PASS_REGULAR_EXPRESSION "key-events-test: ok"
  TIMEOUT 30
)

# The shell test exercises the production SDL physical-scancode and
# modifier-mask translation (dispatch_sdl_test_key_event), which requires
# GRAPHSCORE_BUILD_WRITER=ON because there is no SDL backend in a writer-OFF
# build. Unlike the pointer shell test above, it never calls open_window():
# dispatch_sdl_test_key_event needs no window, renderer, or SDL_Init, so
# there is no headless-host skip outcome to accept and no Windows
# window-creation hang risk to guard against.
if (GRAPHSCORE_BUILD_WRITER)
  add_test(NAME graphscore_writer_key_events_shell_test
    COMMAND graphscore_writer_app --test-key-events-shell
  )

  set_tests_properties(graphscore_writer_key_events_shell_test PROPERTIES
    PASS_REGULAR_EXPRESSION "key-events-shell-test: ok"
    TIMEOUT 30
  )
else()
  # Writer-OFF: the shell test is not registered.  A writer-OFF ctest run
  # exercises only the headless key_events_test (above), which passes
  # without a rendering backend.
endif()

# M5-phase-19b-iii: exercise SelectionToolHandler's accessible range
# controls and its on_key_press override through the writer executable's
# own --test-key-selection path -- Shift extension, the accessible
# start/end/staff-scope controls, and their equivalence with a pointer
# drag over the same musical coordinates. This is a headless code path --
# no window, no SDL -- built on WriterShell's dispatch_test_pointer_event
# and dispatch_test_key_event seams.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_key_selection_test
  COMMAND graphscore_writer_app --test-key-selection
)

set_tests_properties(graphscore_writer_key_selection_test PROPERTIES
  PASS_REGULAR_EXPRESSION "key-selection-test: ok"
  TIMEOUT 30
)

# M5-phase-20: exercise SelectionToolHandler's unmodified Up/Down notehead
# move through the writer executable's own --test-notehead-move path -- the
# single-selected-notehead diatonic step, same-selection retention, layout
# refresh, and every no-op case (no selection, stale identity, multi-notehead,
# non-notehead selection, Shift chord). This is a headless code path -- no
# window, no SDL -- built on WriterShell's dispatch_test_key_event and
# dispatch_test_pointer_event seams.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_notehead_move_test
  COMMAND graphscore_writer_app --test-notehead-move
)

set_tests_properties(graphscore_writer_notehead_move_test PROPERTIES
  PASS_REGULAR_EXPRESSION "notehead-move-test: ok"
  TIMEOUT 30
)

# M5-phase-21: exercise SelectionToolHandler's unmodified `-`/`=` accidental
# step through the writer executable's own --test-accidental-step path -- the
# single-selected-notehead step along the double-flat .. double-sharp ladder,
# the hard reject at either end of that ladder, same-selection retention,
# layout refresh, post-edit audition, and every no-op case (no selection,
# stale identity, multi-notehead, range selection, Shift chord). This is a
# headless code path -- no window, no SDL -- built on WriterShell's
# dispatch_test_key_event and dispatch_test_pointer_event seams.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_accidental_step_test
  COMMAND graphscore_writer_app --test-accidental-step
)

set_tests_properties(graphscore_writer_accidental_step_test PROPERTIES
  PASS_REGULAR_EXPRESSION "accidental-step-test: ok"
  TIMEOUT 30
)

# M5-phase-22: exercise SelectionToolHandler's unmodified Backspace/Delete
# notehead deletion through the writer executable's own --test-notehead-delete
# path -- the single-selected-notehead delete as a normalized rest, prior-onset
# selection in the same voice/staff, the insertion-caret fallback at the first
# onset, chord-notehead contraction, layout refresh, undo/redo, and every no-op
# case (no selection, stale identity, multi-notehead, Shift/modifier chords).
# This is a headless code path -- no window, no SDL -- built on WriterShell's
# dispatch_test_key_event and dispatch_test_pointer_event seams.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_notehead_delete_test
  COMMAND graphscore_writer_app --test-notehead-delete
)

set_tests_properties(graphscore_writer_notehead_delete_test PROPERTIES
  PASS_REGULAR_EXPRESSION "notehead-delete-test: ok"
  TIMEOUT 30
)

# M5-phase-23: exercise SelectionToolHandler's unmodified `R` convert-to-rest
# action through the writer executable's own --test-convert-to-rest path --
# converting a selected note/chord event to an equal-duration rest, the
# resulting RestSet selection on the preserved identity, layout refresh for
# both the NoteheadSet and ChordSet selection arms (including the
# ChordNote-addressed NoteheadSet arm's own cross-measure invalidation
# coverage), undo/redo, and every no-op case (no selection, stale identity,
# multi-item selection, Shift/modifier chords) -- each also asserting the
# committed selection is untouched. This is a headless code path -- no
# window, no SDL -- built on WriterShell's dispatch_test_key_event and
# dispatch_test_pointer_event seams.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_convert_to_rest_test
  COMMAND graphscore_writer_app --test-convert-to-rest
)

set_tests_properties(graphscore_writer_convert_to_rest_test PROPERTIES
  PASS_REGULAR_EXPRESSION "convert-to-rest-test: ok"
  TIMEOUT 30
)
