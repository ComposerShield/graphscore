// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

namespace graphscore {
class GlyphMetrics;
}  // namespace graphscore

namespace graphscore::writer_app {

constexpr std::string_view kSmokeTestFlag         = "--smoke-test";
constexpr std::string_view kSelectionToolTestFlag = "--test-selection-tool";
constexpr std::string_view kSelectionToolShellTestFlag =
    "--test-selection-tool-shell";
constexpr std::string_view kKeyEventsTestFlag      = "--test-key-events";
constexpr std::string_view kKeyEventsShellTestFlag = "--test-key-events-shell";
constexpr std::string_view kKeySelectionTestFlag   = "--test-key-selection";
constexpr std::string_view kNoteheadMoveTestFlag   = "--test-notehead-move";
constexpr std::string_view kAccidentalStepTestFlag = "--test-accidental-step";
constexpr std::string_view kNoteheadDeleteTestFlag = "--test-notehead-delete";
constexpr std::string_view kConvertToRestTestFlag  = "--test-convert-to-rest";
constexpr std::string_view kStaffStepTestFlag      = "--test-staff-step";
constexpr std::string_view kIntervalEntryTestFlag  = "--test-interval-entry";
constexpr std::string_view kIntervalEntryShellTestFlag =
    "--test-interval-entry-shell";
constexpr std::string_view kStepEntryTestFlag = "--test-step-entry";
constexpr std::string_view kNotationAccessibilityTestFlag =
    "--test-notation-accessibility";
constexpr std::string_view kClipboardTestFlag      = "--test-clipboard";
constexpr std::string_view kCommandPaletteTestFlag = "--test-command-palette";
constexpr std::string_view kActionTableTestFlag    = "--test-action-table";
constexpr std::string_view kMeasureEditTestFlag    = "--test-measure-edit";
constexpr std::string_view kTupletEditTestFlag     = "--test-tuplet-edit";
constexpr std::string_view kEventStyleEditTestFlag = "--test-event-style-edit";
constexpr std::string_view kMarkingStyleEditTestFlag =
    "--test-marking-style-edit";
constexpr std::string_view kPickdownEditTestFlag    = "--test-pickdown-edit";
constexpr std::string_view kTrackpadGestureTestFlag = "--test-trackpad-gesture";
constexpr std::string_view kRendererBackendTestFlag = "--test-renderer-backend";
constexpr std::string_view kRendererZoomTestFlag    = "--test-renderer-zoom";
constexpr std::string_view kRendererPresentTestFlag = "--test-renderer-present";
constexpr std::string_view kRenderGeometryTestFlag  = "--test-render-geometry";
constexpr std::string_view kM5AcceptanceTestFlag    = "--test-m5-acceptance";

[[nodiscard]] int selection_tool_test();
[[nodiscard]] int selection_tool_shell_test();
[[nodiscard]] int key_events_test();
[[nodiscard]] int key_events_shell_test();
[[nodiscard]] int key_selection_test();
[[nodiscard]] int notehead_move_test();
[[nodiscard]] int accidental_step_test();
[[nodiscard]] int notehead_delete_test();
[[nodiscard]] int convert_to_rest_test();
[[nodiscard]] int staff_step_test();
[[nodiscard]] int interval_entry_test();
[[nodiscard]] int interval_entry_shell_test();
[[nodiscard]] int step_entry_test();
[[nodiscard]] int notation_accessibility_test();
[[nodiscard]] int clipboard_test();
[[nodiscard]] int command_palette_test();
[[nodiscard]] int action_table_test();
[[nodiscard]] int measure_edit_test();
[[nodiscard]] int tuplet_edit_test();
[[nodiscard]] int event_style_edit_test();
[[nodiscard]] int marking_style_edit_test();
[[nodiscard]] int pickdown_edit_test();
[[nodiscard]] int trackpad_gesture_test();
[[nodiscard]] int renderer_backend_test();
[[nodiscard]] int renderer_zoom_test();
[[nodiscard]] int renderer_present_test();
[[nodiscard]] int render_geometry_test();
[[nodiscard]] int m5_acceptance_test();

// notehead_move_test is split across two translation units (its local-move
// checks and its rollback-failure checks), so the four rollback checks below
// gain external linkage rather than living in one TU's anonymous namespace.
[[nodiscard]] int check_notehead_move_7(
    const graphscore::GlyphMetrics& metrics);
[[nodiscard]] int check_notehead_move_7b(
    const graphscore::GlyphMetrics& metrics);
[[nodiscard]] int check_notehead_move_8(
    const graphscore::GlyphMetrics& metrics);
[[nodiscard]] int check_notehead_move_9(
    const graphscore::GlyphMetrics& metrics);

}  // namespace graphscore::writer_app
