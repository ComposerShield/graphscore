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

# M5-phase-24: exercise the Primary chord mapping and SelectionToolHandler's
# Primary+Up/Down staff step through the writer executable's own
# --test-staff-step path -- is_primary_chord asserted for BOTH platform
# mappings (Command on macOS, Control on Windows/Linux) from one host,
# including the wrong-platform modifier not firing and every superset chord
# being rejected; Primary+Down/Up stepping through the node's staves and
# wrapping at both ends; Shift+Primary+Up staying a no-op on a committed
# range selection while plain Shift+Up still extends its staff scope; no
# committed selection being a no-op; and a single-staff node being a no-op
# that never falls through to M5-phase-20's unmodified diatonic notehead
# move. This is a headless code path -- no window, no SDL -- built on
# WriterShell's dispatch_test_key_event and dispatch_test_pointer_event
# seams.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_staff_step_test
  COMMAND graphscore_writer_app --test-staff-step
)

set_tests_properties(graphscore_writer_staff_step_test PROPERTIES
  PASS_REGULAR_EXPRESSION "staff-step-test: ok"
  TIMEOUT 30
)

# M5-phase-25: exercise SelectionToolHandler's digit-key diatonic interval
# entry through the writer executable's own --test-interval-entry path --
# unmodified `2`..`8` adding above, Shift+`2`..`8` adding below, the fresh
# notehead becoming the selection, key-signature spelling reaching the app,
# note-to-chord and chord growth with existing-notehead preservation,
# duplicate and bounds rejection, undo/redo, layout refresh/rollback, `1`
# no-op, and every ineligible/forbidden-modifier no-op. This is a headless
# code path -- no window, no SDL -- built on WriterShell's
# dispatch_test_key_event and dispatch_test_pointer_event seams.
#
# The headless test uses no rendering backend and works in both writer-ON
# and writer-OFF configurations.
add_test(NAME graphscore_writer_interval_entry_test
  COMMAND graphscore_writer_app --test-interval-entry
)

set_tests_properties(graphscore_writer_interval_entry_test PROPERTIES
  PASS_REGULAR_EXPRESSION "interval-entry-test: ok"
  TIMEOUT 30
)

# M5-phase-25: exercise the production SDL physical-scancode digit path into
# the actual SelectionToolHandler through the writer executable's own
# --test-interval-entry-shell path -- unmodified SDL digit 2 adding above,
# SDL Shift+3 adding below, and the SDL forbidden chord plus SDL digit 1
# no-ops. This is the shell twin of the headless interval_entry_test above.
# It requires GRAPHSCORE_BUILD_WRITER=ON because dispatch_sdl_test_key_event
# is a no-op in a writer-OFF build, exactly like the key_events_shell_test;
# like that test it never calls open_window(), so there is no headless-host
# skip outcome to accept.
if (GRAPHSCORE_BUILD_WRITER)
  add_test(NAME graphscore_writer_interval_entry_shell_test
    COMMAND graphscore_writer_app --test-interval-entry-shell
  )

  set_tests_properties(graphscore_writer_interval_entry_shell_test PROPERTIES
    PASS_REGULAR_EXPRESSION "interval-entry-shell-test: ok"
    TIMEOUT 30
  )
else()
  # Writer-OFF: the SDL shell test is not registered.  A writer-OFF ctest run
  # exercises only the headless interval_entry_test (above), which passes
  # without a rendering backend and never calls dispatch_sdl_test_key_event.
endif()

# M5-phase-27: exercise the app-owned keyboard step-entry protocol through the
# writer executable's own --test-step-entry path -- the cursor lifecycle, the
# deterministic pitch reference and nearest-octave rule, the octave offset,
# duration/dot/voice arming, rest commit, and the atomic node-end rejection.
# This is a headless code path -- no window, no SDL -- built on WriterShell's
# dispatch_test_key_event seam.
add_test(NAME graphscore_writer_step_entry_test
  COMMAND graphscore_writer_app --test-step-entry
)

set_tests_properties(graphscore_writer_step_entry_test PROPERTIES
  PASS_REGULAR_EXPRESSION "step-entry-test: ok"
  TIMEOUT 30
)

# M5-phase-27: exercise the app-owned in-memory clipboard handoff (copy/cut/
# paste) and undo/redo through the writer executable's own --test-clipboard
# path.
add_test(NAME graphscore_writer_clipboard_test
  COMMAND graphscore_writer_app --test-clipboard
)

set_tests_properties(graphscore_writer_clipboard_test PROPERTIES
  PASS_REGULAR_EXPRESSION "clipboard-test: ok"
  TIMEOUT 30
)

# M5-phase-27: exercise the nonvisual command-palette model (toggle, routing,
# inventory, availability, search, execution) through the writer executable's
# own --test-command-palette path.
add_test(NAME graphscore_writer_command_palette_test
  COMMAND graphscore_writer_app --test-command-palette
)

add_test(NAME graphscore_writer_tuplet_edit_test
  COMMAND graphscore_writer_app --test-tuplet-edit
)

add_test(NAME graphscore_writer_event_style_edit_test
  COMMAND graphscore_writer_app --test-event-style-edit
)

add_test(NAME graphscore_writer_marking_style_edit_test
  COMMAND graphscore_writer_app --test-marking-style-edit
)

set_tests_properties(graphscore_writer_tuplet_edit_test PROPERTIES
  TIMEOUT 30
)

set_tests_properties(graphscore_writer_event_style_edit_test PROPERTIES
  TIMEOUT 30
)

set_tests_properties(graphscore_writer_marking_style_edit_test PROPERTIES
  TIMEOUT 30
)

set_tests_properties(graphscore_writer_command_palette_test PROPERTIES
  PASS_REGULAR_EXPRESSION "command-palette-test: ok"
  TIMEOUT 30
)

# M5-phase-27: exercise the chord-class, collision-rule, and repeat-policy
# routing through the writer executable's own --test-action-table path.
add_test(NAME graphscore_writer_action_table_test
  COMMAND graphscore_writer_app --test-action-table
)

set_tests_properties(graphscore_writer_action_table_test PROPERTIES
  PASS_REGULAR_EXPRESSION "action-table-test: ok"
  TIMEOUT 30
)
