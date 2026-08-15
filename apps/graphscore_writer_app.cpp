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
//
// M05 scope: the active tool (note-entry or range-selection) and the
// selection-drag state machine are owned at the application assembly layer
// (see writer_app/selection_tool_handler.hpp). The 18 `--test-*` flags below
// exercise that layer through the WriterShell event-dispatch seams; their
// declarations live in writer_app/selftests/selftests.hpp.
//
// M05 startup tool: the default active tool on launch is kSelection, not
// kNoteEntry, because range selection is the first increment of M05
// delivered. The note-entry tool activates only after the note palette and
// pointer-entry phases are wired; until then, kSelection is the deliberate
// temporary default. See docs/plan/05-notation-editor.md §"Selection and
// keyboard behavior".

#include "writer_app/app_run.hpp"
#include "writer_app/selftests/selftests.hpp"

#include <cstdio>
#include <exception>
#include <string_view>

// The shell allocates (window titles, backend names), so this call path can
// throw. Letting an exception escape `main` gives std::terminate and an
// unhelpful abort; catching it here turns the same failure into a diagnostic
// and a non-zero exit. Note that the realtime prohibition on exceptions
// applies to the runtime's process path, not to the writer application.
int main(int argc, char** argv) {
  using graphscore::writer_app::accidental_step_test;
  using graphscore::writer_app::action_table_test;
  using graphscore::writer_app::clipboard_test;
  using graphscore::writer_app::command_palette_test;
  using graphscore::writer_app::convert_to_rest_test;
  using graphscore::writer_app::interval_entry_shell_test;
  using graphscore::writer_app::interval_entry_test;
  using graphscore::writer_app::kAccidentalStepTestFlag;
  using graphscore::writer_app::kActionTableTestFlag;
  using graphscore::writer_app::kClipboardTestFlag;
  using graphscore::writer_app::kCommandPaletteTestFlag;
  using graphscore::writer_app::kConvertToRestTestFlag;
  using graphscore::writer_app::key_events_shell_test;
  using graphscore::writer_app::key_events_test;
  using graphscore::writer_app::key_selection_test;
  using graphscore::writer_app::kIntervalEntryShellTestFlag;
  using graphscore::writer_app::kIntervalEntryTestFlag;
  using graphscore::writer_app::kKeyEventsShellTestFlag;
  using graphscore::writer_app::kKeyEventsTestFlag;
  using graphscore::writer_app::kKeySelectionTestFlag;
  using graphscore::writer_app::kMeasureEditTestFlag;
  using graphscore::writer_app::kNoteheadDeleteTestFlag;
  using graphscore::writer_app::kNoteheadMoveTestFlag;
  using graphscore::writer_app::kSelectionToolShellTestFlag;
  using graphscore::writer_app::kSelectionToolTestFlag;
  using graphscore::writer_app::kSmokeTestFlag;
  using graphscore::writer_app::kStaffStepTestFlag;
  using graphscore::writer_app::kStepEntryTestFlag;
  using graphscore::writer_app::kTupletEditTestFlag;
  using graphscore::writer_app::measure_edit_test;
  using graphscore::writer_app::notehead_delete_test;
  using graphscore::writer_app::notehead_move_test;
  using graphscore::writer_app::run;
  using graphscore::writer_app::selection_tool_shell_test;
  using graphscore::writer_app::selection_tool_test;
  using graphscore::writer_app::staff_step_test;
  using graphscore::writer_app::step_entry_test;
  using graphscore::writer_app::tuplet_edit_test;

  bool smoke_test                    = false;
  bool run_selection_test            = false;
  bool run_selection_shell_test      = false;
  bool run_key_events_test           = false;
  bool run_key_events_shell_test     = false;
  bool run_key_selection_test        = false;
  bool run_notehead_move_test        = false;
  bool run_accidental_step_test      = false;
  bool run_notehead_delete_test      = false;
  bool run_convert_to_rest_test      = false;
  bool run_staff_step_test           = false;
  bool run_interval_entry_test       = false;
  bool run_interval_entry_shell_test = false;
  bool run_step_entry_test           = false;
  bool run_clipboard_test            = false;
  bool run_command_palette_test      = false;
  bool run_action_table_test         = false;
  bool run_measure_edit_test         = false;
  bool run_tuplet_edit_test          = false;
  for (int i = 1; i < argc; ++i) {
    if (kSmokeTestFlag == argv[i]) {
      smoke_test = true;
    }
    if (kSelectionToolTestFlag == argv[i]) {
      run_selection_test = true;
    }
    if (kSelectionToolShellTestFlag == argv[i]) {
      run_selection_shell_test = true;
    }
    if (kKeyEventsTestFlag == argv[i]) {
      run_key_events_test = true;
    }
    if (kKeyEventsShellTestFlag == argv[i]) {
      run_key_events_shell_test = true;
    }
    if (kKeySelectionTestFlag == argv[i]) {
      run_key_selection_test = true;
    }
    if (kNoteheadMoveTestFlag == argv[i]) {
      run_notehead_move_test = true;
    }
    if (kAccidentalStepTestFlag == argv[i]) {
      run_accidental_step_test = true;
    }
    if (kNoteheadDeleteTestFlag == argv[i]) {
      run_notehead_delete_test = true;
    }
    if (kConvertToRestTestFlag == argv[i]) {
      run_convert_to_rest_test = true;
    }
    if (kStaffStepTestFlag == argv[i]) {
      run_staff_step_test = true;
    }
    if (kIntervalEntryTestFlag == argv[i]) {
      run_interval_entry_test = true;
    }
    if (kIntervalEntryShellTestFlag == argv[i]) {
      run_interval_entry_shell_test = true;
    }
    if (kStepEntryTestFlag == argv[i]) {
      run_step_entry_test = true;
    }
    if (kClipboardTestFlag == argv[i]) {
      run_clipboard_test = true;
    }
    if (kCommandPaletteTestFlag == argv[i]) {
      run_command_palette_test = true;
    }
    if (kActionTableTestFlag == argv[i]) {
      run_action_table_test = true;
    }
    if (kMeasureEditTestFlag == argv[i]) {
      run_measure_edit_test = true;
    }
    if (kTupletEditTestFlag == argv[i]) {
      run_tuplet_edit_test = true;
    }
  }

  try {
    if (run_selection_test) {
      return selection_tool_test();
    }
    if (run_selection_shell_test) {
      return selection_tool_shell_test();
    }
    if (run_key_events_test) {
      return key_events_test();
    }
    if (run_key_events_shell_test) {
      return key_events_shell_test();
    }
    if (run_key_selection_test) {
      return key_selection_test();
    }
    if (run_notehead_move_test) {
      return notehead_move_test();
    }
    if (run_accidental_step_test) {
      return accidental_step_test();
    }
    if (run_notehead_delete_test) {
      return notehead_delete_test();
    }
    if (run_convert_to_rest_test) {
      return convert_to_rest_test();
    }
    if (run_staff_step_test) {
      return staff_step_test();
    }
    if (run_interval_entry_test) {
      return interval_entry_test();
    }
    if (run_interval_entry_shell_test) {
      return interval_entry_shell_test();
    }
    if (run_step_entry_test) {
      return step_entry_test();
    }
    if (run_clipboard_test) {
      return clipboard_test();
    }
    if (run_command_palette_test) {
      return command_palette_test();
    }
    if (run_action_table_test) {
      return action_table_test();
    }
    if (run_measure_edit_test) {
      return measure_edit_test();
    }
    if (run_tuplet_edit_test) {
      return tuplet_edit_test();
    }
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
