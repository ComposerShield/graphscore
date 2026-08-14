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
// ---- M5-phase-24: Primary+Up/Down staff step -------------------------------
//
// Exercises the Primary chord mapping -- Command on macOS, Control on
// Windows/Linux -- as a pure function over BOTH platform values from one
// host, and the SelectionToolHandler wiring that turns Primary+Up/Down into
// a staff step: stepping down and up through the node's staves, wrapping at
// both ends, the wrong-platform modifier not firing, Shift+Primary+Up
// staying a no-op (rather than performing range staff-scope extension, as
// it did before this phase tightened the Shift branch) while plain Shift+Up
// still extends, no selection at all being a no-op, and Primary+Up never
// falling through to M5-phase-20's unmodified diatonic notehead move.

namespace {
[[nodiscard]] graphscore::KeyEvent primary_key(graphscore::KeyCode code,
                                               PrimaryModifier     primary) {
  graphscore::KeyEvent event;
  event.code = code;
  if (primary == PrimaryModifier::kMeta) {
    event.modifiers.meta = true;
  } else {
    event.modifiers.control = true;
  }
  return event;
}

// The modifier this host does NOT map Primary to.
[[nodiscard]] constexpr PrimaryModifier other_primary(PrimaryModifier primary) {
  return primary == PrimaryModifier::kMeta ? PrimaryModifier::kControl
                                           : PrimaryModifier::kMeta;
}
}  // namespace

int staff_step_test() {
  const SelfTestMetrics   metrics;
  const graphscore::Voice voice1 = voice_one();

  // --- test 1: the Primary chord rule, asserted for both platform mappings
  //     from this one host. The macro selects only the default; every rule
  //     below is a property of the parameter. -----------------------------
  {
    const auto mods = [](bool shift, bool control, bool alt, bool meta) {
      graphscore::KeyModifiers modifiers;
      modifiers.shift   = shift;
      modifiers.control = control;
      modifiers.alt     = alt;
      modifiers.meta    = meta;
      return modifiers;
    };

    struct ChordCase {
      const char*              label;
      graphscore::KeyModifiers modifiers;
      PrimaryModifier          primary;
      bool                     expected;
    };

    const std::array<ChordCase, 11> kChordCases{{
        {"Command is Primary on macOS", mods(false, false, false, true),
         PrimaryModifier::kMeta, true},
        {"Control is NOT Primary on macOS", mods(false, true, false, false),
         PrimaryModifier::kMeta, false},
        {"Control is Primary on Windows/Linux", mods(false, true, false, false),
         PrimaryModifier::kControl, true},
        {"Command is NOT Primary on Windows/Linux",
         mods(false, false, false, true), PrimaryModifier::kControl, false},
        {"no modifier is not Primary (macOS)", mods(false, false, false, false),
         PrimaryModifier::kMeta, false},
        {"no modifier is not Primary (Windows/Linux)",
         mods(false, false, false, false), PrimaryModifier::kControl, false},
        {"Shift+Command is not an exact Primary chord",
         mods(true, false, false, true), PrimaryModifier::kMeta, false},
        {"Alt+Command is not an exact Primary chord",
         mods(false, false, true, true), PrimaryModifier::kMeta, false},
        {"Control+Command is not an exact Primary chord",
         mods(false, true, false, true), PrimaryModifier::kMeta, false},
        {"Shift+Control is not an exact Primary chord",
         mods(true, true, false, false), PrimaryModifier::kControl, false},
        {"Command+Control is not an exact Primary chord",
         mods(false, true, false, true), PrimaryModifier::kControl, false},
    }};

    for (const ChordCase& test_case : kChordCases) {
      if (is_primary_chord(test_case.modifiers, test_case.primary) !=
          test_case.expected) {
        std::fprintf(stderr, "staff-step-test: %s\n", test_case.label);
        return 1;
      }
    }

    // The host's own default really is one of the two, and the other one is
    // really not Primary here.
    if (!is_primary_chord(
            primary_key(graphscore::KeyCode::kUp, kPlatformPrimaryModifier)
                .modifiers,
            kPlatformPrimaryModifier)) {
      std::fprintf(stderr,
                   "staff-step-test: the host's own Primary modifier does "
                   "not satisfy is_primary_chord\n");
      return 1;
    }
    if (is_primary_chord(primary_key(graphscore::KeyCode::kUp,
                                     other_primary(kPlatformPrimaryModifier))
                             .modifiers,
                         kPlatformPrimaryModifier)) {
      std::fprintf(stderr,
                   "staff-step-test: the other platform's modifier satisfies "
                   "is_primary_chord on this host\n");
      return 1;
    }
  }

  // --- test 2: Primary+Down/Up steps through the node's three staves and
  //     wraps at both ends; the wrong-platform modifier never fires. ------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr, "staff-step-test: fixture build failed (2)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    std::array<graphscore::NotationEntityId, 3> first_ids{};
    for (std::size_t i = 0; i < first_ids.size(); ++i) {
      const graphscore::Node* node = handler.project().find_node(dp->node_id);
      if (node == nullptr) {
        std::fprintf(stderr, "staff-step-test: node missing (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::TrackLane* lane = node->lane(dp->track_ids[i]);
      if (lane == nullptr || lane->stave(dp->stave_ids[i]) == nullptr) {
        std::fprintf(stderr, "staff-step-test: lane/stave missing (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      const auto& events =
          lane->stave(dp->stave_ids[i])->voice(voice1).events();
      if (events.empty()) {
        std::fprintf(stderr, "staff-step-test: empty source voice (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      first_ids[i] = graphscore::event_id(events[0]);
    }

    if (!select_noteheads(
            handler, {graphscore::NoteheadItem{dp->node_id, dp->track_ids[0],
                                               dp->stave_ids[0], voice1,
                                               first_ids[0]}})) {
      std::fprintf(stderr, "staff-step-test: selection setup rejected (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto landed = [&](std::size_t index, const char* label) {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != first_ids[index] ||
          set->items()[0].track != dp->track_ids[index] ||
          set->items()[0].stave != dp->stave_ids[index] ||
          set->items()[0].voice != voice1) {
        std::fprintf(stderr, "staff-step-test: %s\n", label);
        return false;
      }
      return true;
    };

    const auto press_primary = [&](graphscore::KeyCode code) {
      shell.dispatch_test_key_event(
          primary_key(code, kPlatformPrimaryModifier));
    };

    press_primary(graphscore::KeyCode::kDown);
    if (!landed(1, "Primary+Down did not step to the second staff")) {
      shell.set_input_handler(nullptr);
      return 1;
    }
    press_primary(graphscore::KeyCode::kDown);
    if (!landed(2, "Primary+Down did not step to the third staff")) {
      shell.set_input_handler(nullptr);
      return 1;
    }
    press_primary(graphscore::KeyCode::kDown);
    if (!landed(0, "Primary+Down did not wrap to the first staff")) {
      shell.set_input_handler(nullptr);
      return 1;
    }
    press_primary(graphscore::KeyCode::kUp);
    if (!landed(2, "Primary+Up did not wrap to the last staff")) {
      shell.set_input_handler(nullptr);
      return 1;
    }
    press_primary(graphscore::KeyCode::kUp);
    if (!landed(1, "Primary+Up did not step to the second staff")) {
      shell.set_input_handler(nullptr);
      return 1;
    }

    // The other platform's modifier is not Primary here: the selection must
    // not move.
    const std::optional<graphscore::Selection> before =
        handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(primary_key(
        graphscore::KeyCode::kDown, other_primary(kPlatformPrimaryModifier)));
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "staff-step-test: the wrong-platform modifier stepped the "
                   "staff (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: Shift+Primary+Up is a no-op on a committed range selection,
  //     while plain Shift+Up still extends its staff scope. ---------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr, "staff-step-test: fixture build failed (3)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    // Drag across the MIDDLE staff, so a subsequent Shift+Up has a staff
    // above it to widen onto.
    {
      const auto&  layout = handler.layout();
      const double x1     = layout.systems[0].measures[0].bounds.x;
      const double x2     = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[1].bounds.y +
                       layout.systems[0].staves[1].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto* dragged = committed_range_set(handler);
    if (dragged == nullptr || dragged->items().size() != 1u) {
      std::fprintf(stderr,
                   "staff-step-test: drag did not commit a one-staff range "
                   "selection (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const std::optional<graphscore::Selection> before =
        handler.drag_state().committed_selection();
    graphscore::KeyEvent chord =
        primary_key(graphscore::KeyCode::kUp, kPlatformPrimaryModifier);
    chord.modifiers.shift = true;
    shell.dispatch_test_key_event(chord);
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "staff-step-test: Shift+Primary+Up changed the committed "
                   "range selection (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Plain Shift+Up still performs M5-phase-19b-iii's staff-scope
    // extension, so tightening the Shift branch removed only the superset
    // chord.
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    const auto* widened = committed_range_set(handler);
    if (widened == nullptr || widened->items().size() != 2u) {
      std::fprintf(stderr,
                   "staff-step-test: plain Shift+Up no longer extends the "
                   "staff scope (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: no committed selection is a no-op. ----------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr, "staff-step-test: fixture build failed (4)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    shell.dispatch_test_key_event(
        primary_key(graphscore::KeyCode::kDown, kPlatformPrimaryModifier));
    if (handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "staff-step-test: Primary+Down created a selection from "
                   "nothing (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: a single-staff node is a no-op, and Primary+Up never falls
  //     through to M5-phase-20's unmodified diatonic notehead move. -------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "staff-step-test: fixture build failed (5)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice1, fx->first_note_id}})) {
      std::fprintf(stderr, "staff-step-test: selection setup rejected (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto pitch_of_first = [&]() {
      const graphscore::Node* node = handler.project().find_node(fx->node_id);
      if (node == nullptr) {
        return graphscore::SpelledPitch{};
      }
      const graphscore::TrackLane* lane = node->lane(fx->track_id);
      if (lane == nullptr || lane->stave(fx->stave_id) == nullptr) {
        return graphscore::SpelledPitch{};
      }
      const auto& events = lane->stave(fx->stave_id)->voice(voice1).events();
      if (events.empty()) {
        return graphscore::SpelledPitch{};
      }
      const auto* note = std::get_if<graphscore::Note>(&events[0]);
      return note == nullptr ? graphscore::SpelledPitch{} : note->pitch;
    };

    const graphscore::SpelledPitch             before_pitch = pitch_of_first();
    const std::optional<graphscore::Selection> before =
        handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(
        primary_key(graphscore::KeyCode::kUp, kPlatformPrimaryModifier));
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "staff-step-test: a single-staff node stepped its "
                   "selection (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (pitch_of_first() != before_pitch) {
      std::fprintf(stderr,
                   "staff-step-test: Primary+Up fell through to the diatonic "
                   "notehead move (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("staff-step-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
