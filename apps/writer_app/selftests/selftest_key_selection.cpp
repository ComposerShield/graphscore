// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../key_bindings.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {
// ---- key-selection tests (M5-phase-19b-iii) --------------------------------
//
// Exercises SelectionToolHandler's accessible range controls and its
// on_key_press override -- interpreting Shift extension and the
// start/end/staff-scope controls, wired through the platform-neutral key
// events M5-phase-19b-ii delivers. Headless: no window, no SDL, works in
// both writer-ON and writer-OFF configurations.

namespace {
// Rational::create only fails on a zero denominator; every call site below
// passes a small nonzero literal, so the std::nullopt arm is unreachable in
// practice. Checked explicitly (rather than dereferencing the optional
// inline) so the value is read the same way every other runtime
// Rational/Duration/SpelledPitch construction in this file is: via a
// named, has_value()-checked local. (kProvisionalRangeExtensionStep above
// is the one constexpr exception: an inline dereference there is
// evaluated at compile time, not runtime.)
[[nodiscard]] graphscore::Rational rational(std::int64_t numerator,
                                            std::int64_t denominator) {
  const auto value = graphscore::Rational::create(numerator, denominator);
  if (!value.has_value()) {
    return graphscore::Rational(0);
  }
  return *value;
}
}  // namespace

int key_selection_test() {
  const SelfTestMetrics metrics;

  // --- test 1: equivalence -- the phase's core acceptance criterion.
  //     A pointer drag and a keyboard-driven extension from a different
  //     starting selection must reach the identical committed Selection,
  //     compared with the defaulted ArbitraryRangeSet/Selection
  //     operator==. --------------------------------------------------
  {
    // Both handlers below operate on independent copies of the same
    // project (same NodeId/TrackId/StaveId values), not two separately
    // built fixtures -- build_key_selection_project generates fresh
    // NodeId/TrackId/StaveId values on every call, so two independently
    // built fixtures could never satisfy Selection's own operator==
    // regardless of whether the span/staff-scope logic agrees.
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (1) "
                   "failed\n");
      return 1;
    }

    graphscore::Project              project_a = dp->project;
    graphscore::NotationLayoutResult layout_result_a =
        graphscore::layout_notation(project_a, dp->node_id, metrics);
    if (!layout_result_a || !layout_result_a.layout.has_value()) {
      std::fprintf(stderr, "key-selection-test: layout_notation (a) failed\n");
      return 1;
    }
    graphscore::WriterShell shell_a;
    SelectionToolHandler    handler_a(
        std::move(project_a), std::move(*layout_result_a.layout), &shell_a);
    shell_a.set_input_handler(&handler_a);
    handler_a.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout_a = handler_a.layout();
    {
      const double x1 = layout_a.systems[0].measures[0].bounds.x;
      const double x2 = layout_a.systems[0].measures[1].bounds.x +
                        layout_a.systems[0].measures[1].bounds.width;
      const double y = layout_a.systems[0].staves[1].bounds.y +
                       layout_a.systems[0].staves[1].bounds.height * 0.5;
      drag_through_shell(shell_a, x1, y, x2, y);
    }
    const auto target = handler_a.drag_state().committed_selection();
    shell_a.set_input_handler(nullptr);
    if (!target.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: target selection missing "
                   "(equivalence)\n");
      return 1;
    }

    graphscore::Project              project_b = dp->project;
    graphscore::NotationLayoutResult layout_result_b =
        graphscore::layout_notation(project_b, dp->node_id, metrics);
    if (!layout_result_b || !layout_result_b.layout.has_value()) {
      std::fprintf(stderr, "key-selection-test: layout_notation (b) failed\n");
      return 1;
    }
    graphscore::WriterShell shell_b;
    SelectionToolHandler    handler_b(
        std::move(project_b), std::move(*layout_result_b.layout), &shell_b);
    shell_b.set_input_handler(&handler_b);
    handler_b.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout_b = handler_b.layout();
    {
      // A different starting selection: the same track/staff, but only the
      // first measure.
      const double x1 = layout_b.systems[0].measures[0].bounds.x;
      const double x2 = layout_b.systems[0].measures[0].bounds.x +
                        layout_b.systems[0].measures[0].bounds.width;
      const double y = layout_b.systems[0].staves[1].bounds.y +
                       layout_b.systems[0].staves[1].bounds.height * 0.5;
      drag_through_shell(shell_b, x1, y, x2, y);
    }
    if (!handler_b.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: starting selection missing "
                   "(equivalence)\n");
      shell_b.set_input_handler(nullptr);
      return 1;
    }
    if (!handler_b.select_to_node_end()) {
      std::fprintf(stderr,
                   "key-selection-test: select_to_node_end failed "
                   "(equivalence)\n");
      shell_b.set_input_handler(nullptr);
      return 1;
    }
    if (handler_b.drag_state().committed_selection() != target) {
      std::fprintf(stderr,
                   "key-selection-test: keyboard-reached selection did not "
                   "equal the pointer-drag selection (equivalence)\n");
      shell_b.set_input_handler(nullptr);
      return 1;
    }
    shell_b.set_input_handler(nullptr);
  }

  // --- test 2: edge extension in both directions produces the expected
  //     span. -----------------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (2) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const std::vector<graphscore::NotationRect> rects_before_edge =
        shell.test_snapshot_highlight_rects();
    if (rects_before_edge.empty()) {
      std::fprintf(stderr,
                   "key-selection-test: highlight empty after drag setup "
                   "(2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.extend_range_edge(graphscore::RangeEdge::kEnd,
                                   graphscore::Rational(2))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge(kEnd) failed\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span !=
              (graphscore::MusicalSpan{graphscore::Rational(0),
                                       graphscore::Rational(2)})) {
        std::fprintf(stderr,
                     "key-selection-test: extend_range_edge(kEnd) span "
                     "mismatch\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    {
      const std::vector<graphscore::NotationRect> rects_after_edge =
          shell.test_snapshot_highlight_rects();
      if (rects_after_edge == rects_before_edge) {
        std::fprintf(stderr,
                     "key-selection-test: highlight rects did not change "
                     "after extend_range_edge(kEnd) (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   graphscore::Rational(1))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge(kStart) failed\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span !=
              (graphscore::MusicalSpan{graphscore::Rational(1),
                                       graphscore::Rational(2)})) {
        std::fprintf(stderr,
                     "key-selection-test: extend_range_edge(kStart) span "
                     "mismatch\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: focus-edge tracking across a crossing -- the next
  //     extension moves the edge a user would expect, proving the
  //     recompute takes effect. ------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (3) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    // span [0, 1) -> [1/4, 1): kStart moves to 1/4, no crossing.
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   kProvisionalRangeExtensionStep)) {
      std::fprintf(stderr, "key-selection-test: setup step 1 failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // span [1/4, 1) -> [1, 3/2): kStart moves to 3/2, crossing the fixed
    // end (1) -- the span swaps, and focus_edge_ must become kEnd since
    // the moved value (3/2) now lands on the span's own end.
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   rational(3, 2))) {
      std::fprintf(stderr, "key-selection-test: setup step 2 failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().front().span !=
                                (graphscore::MusicalSpan{
                                    graphscore::Rational(1), rational(3, 2)})) {
        std::fprintf(stderr,
                     "key-selection-test: crossing setup span mismatch "
                     "(3)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Shift+Left must now move the end (focus_edge_ == kEnd after the
    // crossing above), producing [1, 5/4) -- not the start, which would
    // instead produce [3/4, 3/2) if the recompute had not taken effect.
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kLeft));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().front().span !=
                                (graphscore::MusicalSpan{
                                    graphscore::Rational(1), rational(5, 4)})) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Left after crossing moved "
                     "the wrong edge\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: staff scope +/-1 in both directions, including that
  //     running off either end is a no-op leaving the selection
  //     unchanged. -----------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (4) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[1].bounds.y +
                       layout.systems[0].staves[1].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const std::vector<graphscore::NotationRect> rects_before_scope =
        shell.test_snapshot_highlight_rects();
    if (rects_before_scope.empty()) {
      std::fprintf(stderr,
                   "key-selection-test: highlight empty after drag setup "
                   "(4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    {
      const std::vector<graphscore::NotationRect> rects_after_scope =
          shell.test_snapshot_highlight_rects();
      if (rects_after_scope == rects_before_scope) {
        std::fprintf(stderr,
                     "key-selection-test: highlight rects did not change "
                     "after Shift+Up widened the staff scope (4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    const graphscore::MeasureScope top{dp->track_ids[0], dp->stave_ids[0]};
    if (handler.first_staff() != top) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Up did not widen first_staff_ "
                   "(4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 2u) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Up item count mismatch "
                     "(4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Shift+Up again: first_staff_ is already at index 0 -- no-op.
    const auto before_clamp_up = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    if (handler.drag_state().committed_selection() != before_clamp_up) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Up at the top staff was not a "
                   "no-op (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kDown));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 3u) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Down item count mismatch "
                     "(4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Shift+Down again: last_staff_ is already at the bottom staff --
    // no-op.
    const auto before_clamp_down = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kDown));
    if (handler.drag_state().committed_selection() != before_clamp_down) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Down at the bottom staff was "
                   "not a no-op (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: Shift+Home reaches exactly Rational(0); Shift+End reaches
  //     exactly the node's total_length(). --------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (5) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kEnd));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span.end != graphscore::Rational(2)) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+End did not reach "
                     "total_length()\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    {
      const double x1 = layout.systems[0].measures[1].bounds.x;
      const double x2 = layout.systems[0].measures[1].bounds.x +
                        layout.systems[0].measures[1].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kHome));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span.start != graphscore::Rational(0)) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Home did not reach "
                     "Rational(0)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 6: no committed selection -> every binding is a no-op and
  //     nothing crashes. -------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (6) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    if (handler.extend_range_edge(graphscore::RangeEdge::kEnd,
                                  graphscore::Rational(1)) ||
        handler.extend_range_staff_scope(
            graphscore::MeasureScope{dp->track_ids[0], dp->stave_ids[0]},
            graphscore::MeasureScope{dp->track_ids[1], dp->stave_ids[1]}) ||
        handler.select_to_node_start() || handler.select_to_node_end()) {
      std::fprintf(stderr,
                   "key-selection-test: a direct control call succeeded "
                   "with no committed selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    constexpr std::array<graphscore::KeyCode, 6> kAllBoundCodes{
        graphscore::KeyCode::kLeft, graphscore::KeyCode::kRight,
        graphscore::KeyCode::kUp,   graphscore::KeyCode::kDown,
        graphscore::KeyCode::kHome, graphscore::KeyCode::kEnd,
    };
    for (const graphscore::KeyCode code : kAllBoundCodes) {
      shell.dispatch_test_key_event(shift_key(code));
      if (handler.drag_state().committed_selection().has_value()) {
        std::fprintf(stderr,
                     "key-selection-test: a key binding created a "
                     "selection with none committed\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: unmodified arrows and wrong-modifier chords (control, alt,
  //     meta -- none substitutes for shift) are no-ops. -------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (7) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    if (!before.has_value()) {
      std::fprintf(stderr, "key-selection-test: setup selection missing (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Unmodified Left.
    {
      graphscore::KeyEvent event;
      event.code = graphscore::KeyCode::kLeft;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: unmodified Left changed the "
                   "selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Control+Left (wrong modifier -- control does not substitute for
    // shift).
    {
      graphscore::KeyEvent event;
      event.code              = graphscore::KeyCode::kLeft;
      event.modifiers.control = true;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Control+Left changed the "
                   "selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Alt+Left (wrong modifier -- alt does not substitute for shift).
    {
      graphscore::KeyEvent event;
      event.code          = graphscore::KeyCode::kLeft;
      event.modifiers.alt = true;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Alt+Left changed the selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Meta+Left (wrong modifier -- meta does not substitute for shift).
    {
      graphscore::KeyEvent event;
      event.code           = graphscore::KeyCode::kLeft;
      event.modifiers.meta = true;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Meta+Left changed the selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 8: the override takes effect through the InputHandler* seam,
  //     not the base class's non-pure no-op default. ----------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (8) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    graphscore::InputHandler* base = &handler;
    shell.set_input_handler(base);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kRight));
    if (handler.drag_state().committed_selection() == before) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Right through the "
                   "InputHandler* seam did not reach the override\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 9: staff endpoints survive an edge-only extension: an
  //     extreme staff contributing no items keeps its place in the
  //     tracked scope, and is recovered once the widened span overlaps
  //     it. -------------------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (9) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    // Drag the second measure only on track 0.
    {
      const double x1 = layout.systems[0].measures[1].bounds.x;
      const double x2 = layout.systems[0].measures[1].bounds.x +
                        layout.systems[0].measures[1].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const graphscore::MeasureScope first{dp->track_ids[0], dp->stave_ids[0]};
    const graphscore::MeasureScope last{dp->track_ids[2], dp->stave_ids[2]};
    if (!handler.extend_range_staff_scope(first, last)) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_staff_scope setup "
                   "failed (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // track_ids[2] carries content only in [0, 1); the committed span is
    // [1, 2), so it contributes no item here, even though it is part of
    // the tracked scope.
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 2u) {
        std::fprintf(stderr,
                     "key-selection-test: pre-extension item count "
                     "mismatch (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (handler.first_staff() != first || handler.last_staff() != last) {
      std::fprintf(stderr,
                   "key-selection-test: staff scope not tracked before "
                   "extension (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   graphscore::Rational(0))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge(kStart) failed "
                   "(9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The widened span [0, 2) now overlaps track_ids[2]'s content too.
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 3u) {
        std::fprintf(stderr,
                     "key-selection-test: post-extension item count "
                     "mismatch (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (handler.first_staff() != first || handler.last_staff() != last) {
      std::fprintf(stderr,
                   "key-selection-test: staff scope not preserved across "
                   "an edge-only extension (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 10: a resolver rejection during extend_range_edge is a true
  //     no-op -- the committed selection must never be cleared, only left
  //     exactly as it was, when the requested span falls outside the
  //     node's bounds. -----------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (10) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    if (!before.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: setup selection missing (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // rational(99, 1) is well past the node's total_length() of 2, so
    // resolve_range_selection_spec must reject the request.
    if (handler.extend_range_edge(graphscore::RangeEdge::kEnd,
                                  rational(99, 1))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge accepted an "
                   "out-of-bounds span (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: a rejected extend_range_edge "
                   "changed the committed selection (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 11: the active-tool gate -- Shift+arrow bindings must not
  //     touch the committed selection while a non-selection tool is
  //     active. ---------------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (11) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    if (!before.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: setup selection missing (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kRight));
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Right/Up changed the "
                   "committed selection while kNoteEntry was active\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("key-selection-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
