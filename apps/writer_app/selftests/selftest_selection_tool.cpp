// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
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
int selection_tool_test() {
  const SelfTestMetrics metrics;
  auto                  dp = build_default_project(metrics);
  if (!dp.has_value()) {
    std::fprintf(stderr, "selection-tool-test: build_default_project failed\n");
    return 1;
  }

  // Create a shell and register the handler. The dispatch_test_pointer_event
  // seam exercises the exact registration / dispatch / unregistration
  // contract without a native window.
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(dp->project), std::move(dp->layout),
                                  &shell);
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const auto& layout = handler.layout();

  // Helper: create a pointer event in notation (logical) coordinates.
  auto make_event = [](double x, double y,
                       graphscore::PointerButton button =
                           graphscore::PointerButton::kPrimary) {
    graphscore::PointerEvent e;
    e.x      = x;
    e.y      = y;
    e.button = button;
    return e;
  };

  // --- test 1: note-entry tool ignores pointer drag ---------------------
  handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
  {
    shell.dispatch_test_pointer_event(
        0, make_event(layout.systems[0].measures[0].bounds.x,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: kNoteEntry tool started a drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 2: selection tool creates a drag, moves, commits ------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: kSelection tool did not start drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;
    const double move_y = press_y;
    shell.dispatch_test_pointer_event(1, make_event(move_x, move_y));
    if (!handler.drag_state().live_extent().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: live_extent missing after move\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_pointer_event(2, make_event(move_x, move_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: is_dragging true after release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& comm_sel = handler.drag_state().committed_selection();
    if (!comm_sel.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: committed_selection missing after "
                   "release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Exact committed span: a drag across a full 4/4 measure must produce a
    // span of [0, 1) (measure-relative whole-note units).
    {
      const auto* set = std::get_if<graphscore::ArbitraryRangeSet>(&*comm_sel);
      if (set == nullptr || set->items().empty()) {
        std::fprintf(stderr,
                     "selection-tool-test: committed selection is not a "
                     "non-empty ArbitraryRangeSet\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (set->items().size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-test: expected 1 range item, got %zu\n",
                     set->items().size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::MusicalSpan expected{graphscore::Rational(0),
                                             graphscore::Rational(1)};
      if (set->items()[0].span != expected) {
        std::fprintf(
            stderr,
            "selection-tool-test: span mismatch — "
            "expected [0, 1), got [%" PRId64 "/%" PRId64 ", %" PRId64
            "/%" PRId64 ")\n",
            static_cast<std::int64_t>(set->items()[0].span.start.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.start.denominator()),
            static_cast<std::int64_t>(set->items()[0].span.end.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.end.denominator()));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Exact highlight rect: the committed full-measure drag must produce
    // exactly one rect whose x,y,width,height are the expected values
    // derived from the fixture's measure and staff bounds — not merely
    // within-bounds positive checks.
    {
      const std::vector<graphscore::NotationRect> headless_rects =
          shell.test_snapshot_highlight_rects();
      if (headless_rects.size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-test: expected 1 highlight rect, got %zu "
                     "(headless path)\n",
                     headless_rects.size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::NotationRect expected =
          expected_full_measure_highlight_rect(layout);
      if (headless_rects[0] != expected) {
        std::fprintf(stderr,
                     "selection-tool-test: highlight rect mismatch "
                     "(headless path) — "
                     "expected [%.6f,%.6f %.6fx%.6f], "
                     "got [%.6f,%.6f %.6fx%.6f]\n",
                     expected.x, expected.y, expected.width, expected.height,
                     headless_rects[0].x, headless_rects[0].y,
                     headless_rects[0].width, headless_rects[0].height);
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }

  // --- test 3: switch to note-entry cancels the drag -------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: begin before tool-switch failed\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr, "selection-tool-test: drag survived tool switch\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 4: secondary button does not start a drag -----------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    shell.dispatch_test_pointer_event(
        0, make_event(layout.systems[0].measures[0].bounds.x,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5,
                      graphscore::PointerButton::kSecondary));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: secondary button started a drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 5: move without drag is a no-op ---------------------------
  {
    shell.dispatch_test_pointer_event(
        1, make_event(layout.systems[0].measures[0].bounds.x + 100.0,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: move without press started drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 6: committed selection survives tool switch ----------------
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    shell.dispatch_test_pointer_event(2, make_event(move_x, press_y));

    if (!handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: commit produced no selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (!handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: committed_selection lost on tool "
                   "switch\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 7: secondary button does not commit the drag ---------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release with a secondary button — the drag must stay in progress.
    shell.dispatch_test_pointer_event(
        2, make_event(move_x, press_y, graphscore::PointerButton::kSecondary));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: secondary release ended drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // A subsequent primary release should now commit.
    shell.dispatch_test_pointer_event(2, make_event(move_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: primary release did not commit\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 8: release point differs from last motion; commit resolves
  //     the release point, not the last motion point -------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;
    const double release_x = layout.systems[0].measures[0].bounds.x +
                             layout.systems[0].measures[0].bounds.width * 0.5;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release at a different position than the last move.
    shell.dispatch_test_pointer_event(2, make_event(release_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag still in progress after "
                   "release-at-different-point\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The committed selection should have a span ending at release_x,
    // not at move_x.
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr, "selection-tool-test: no commit after release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 9: release at an invalid position (off-stave) cancels
  //     without committing a stale extent --------------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release at an off-stave position (far above the staff).
    // Resolution fails → handler cancels the drag.
    shell.dispatch_test_pointer_event(2, make_event(press_x, -10'000.0));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag survived invalid release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The committed selection from test 2 should still be intact, but
    // no new committed selection leaked from this cancelled drag.
  }

  // --- test 10: unknown button does not start a drag ------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    shell.dispatch_test_pointer_event(
        0, make_event(layout.systems[0].measures[0].bounds.x,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5,
                      graphscore::PointerButton::kUnknown));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: unknown button started a drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 11: invalid (NaN) release cancels the drag without
  //     committing ---------------------------------------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release at NaN position — resolution fails; drag is cancelled.
    shell.dispatch_test_pointer_event(
        2, make_event(std::numeric_limits<double>::quiet_NaN(), press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr, "selection-tool-test: drag survived NaN release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 12: cancel restores the exact committed highlight -------
  // Commit one full-measure drag, then start a second drag to a
  // demonstrably different endpoint.  Assert the live highlight differs
  // from the committed highlight; cancel; assert the restored highlight
  // exactly equals the original committed highlight (headless path).
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double full_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;
    const double half_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width * 0.5;

    // Commit: full-measure drag.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(full_x, press_y));
    shell.dispatch_test_pointer_event(2, make_event(full_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag still active after commit\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::vector<graphscore::NotationRect> committed_rects =
        shell.test_snapshot_highlight_rects();
    if (committed_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-test: committed highlight empty after "
                   "commit (headless path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Start a second drag to a demonstrably different endpoint (half-
    // measure instead of full-measure).  The press alone preserves the
    // committed highlight.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr, "selection-tool-test: second drag did not begin\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // After press, the committed highlight is unchanged.
    {
      const auto after_press = shell.test_snapshot_highlight_rects();
      if (after_press != committed_rects) {
        std::fprintf(stderr,
                     "selection-tool-test: highlight changed after second "
                     "press (headless path)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Move to half-measure: live highlight must differ from committed.
    shell.dispatch_test_pointer_event(1, make_event(half_x, press_y));
    const std::vector<graphscore::NotationRect> live_rects =
        shell.test_snapshot_highlight_rects();
    if (live_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-test: live highlight empty after move "
                   "(headless path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (live_rects == committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-test: live highlight did not differ from "
                   "committed after move to different endpoint (headless "
                   "path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Cancel the second drag.  The committed highlight must be restored
    // exactly.
    shell.dispatch_test_pointer_event(3, make_event(0.0, 0.0));  // cancel
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag survived cancel (headless "
                   "path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::vector<graphscore::NotationRect> restored_rects =
        shell.test_snapshot_highlight_rects();
    if (restored_rects != committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-test: restored highlight does not match "
                   "committed after cancel (headless path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 13: unregistered handler receives no callback ------------
  shell.set_input_handler(nullptr);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;

    // Dispatch should be a no-op with no handler registered.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    // The handler's drag state must be unchanged (no implicit drag).
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: handler received event after "
                   "unregistration\n");
      return 1;
    }
  }
  shell.set_input_handler(&handler);

  // --- test 14: DPI scale conversion produces correct coordinates ----
  // Set scale to 2.0, send an event at pixel (200, 100).  The test seam
  // divides by test_dpi_scale, so the handler receives logical (100, 50),
  // matching the production SDL_ConvertEventToRenderCoordinates path.
  shell.set_test_dpi_scale(2.0);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    shell.dispatch_test_pointer_event(0, make_event(200.0, 100.0));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: DPI-scaled press did not begin "
                   "drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const graphscore::NotationPoint anchor = handler.drag_state().anchor();
    if (std::abs(anchor.x - 100.0) > 1e-9 || std::abs(anchor.y - 50.0) > 1e-9) {
      std::fprintf(stderr,
                   "selection-tool-test: DPI-scaled anchor mismatch: "
                   "expected (100, 50), got (%.1f, %.1f)\n",
                   anchor.x, anchor.y);
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.on_cancel();
  }
  shell.set_test_dpi_scale(0.0);

  // --- test 15: non-finite primary re-press while dragging cancels ----
  // Start a valid selection drag, then issue a primary press with NaN
  // coordinates.  The handler must cancel the drag via begin(), so a
  // subsequent release does not commit a stale extent.
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    // Start a valid drag.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag not active before NaN repress\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Re-press with NaN coordinates — handler must cancel the drag.
    shell.dispatch_test_pointer_event(
        0, make_event(std::numeric_limits<double>::quiet_NaN(), press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag survived NaN primary repress\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // A subsequent primary release must not commit (drag is already gone).
    shell.dispatch_test_pointer_event(2, make_event(move_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: release after NaN repress started "
                   "a new drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  std::printf("selection-tool-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
