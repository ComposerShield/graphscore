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
// ---- M5-phase-22: notehead delete ----------------------------------------
//
// Exercises SelectionToolHandler's unmodified Backspace/Delete dispatch: with
// exactly one selected notehead, the notehead is removed -- a complete Note
// becomes a same-duration normalized Rest -- and the prior onset in the same
// voice/staff is selected. Deleting the voice's first event leaves an
// insertion caret at onset 0 instead. One pitch removed from a Chord leaves
// the remaining pitch (a two-note Chord contracts to a Note). The retained
// layout is refreshed and re-published, and the delete runs as one
// reversible history entry. No selection, a multi-notehead selection, a
// stale notehead, and Shift/modifier chords remain no-ops.
int notehead_delete_test() {
  const SelfTestMetrics metrics;

  const graphscore::Voice voice1 = voice_one();

  // --- test 1: a real click selects the second notehead, then Delete
  //     removes it as a normalized Rest, selects the prior onset (the first
  //     notehead), re-publishes a different visible surface, and undo/redo
  //     round-trips through the handler's history. -------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-delete-test: fixture build failed (1)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(
          stderr, "notehead-delete-test: initial surface publish failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto before_surface = shell.test_snapshot_notation_surface();

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->second_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->second_note_id) {
        std::fprintf(stderr,
                     "notehead-delete-test: click did not select the second "
                     "notehead (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    const auto event_at = [&](std::size_t index) {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      return lane->stave(fx->stave_id)->voice(voice1).events()[index];
    };
    if (!std::holds_alternative<graphscore::Note>(event_at(0)) ||
        !std::holds_alternative<graphscore::Note>(event_at(1))) {
      std::fprintf(stderr,
                   "notehead-delete-test: expected two notes before delete "
                   "(1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDelete));
    if (!std::holds_alternative<graphscore::Rest>(event_at(1)) ||
        !std::holds_alternative<graphscore::Note>(event_at(0))) {
      std::fprintf(stderr,
                   "notehead-delete-test: Delete did not replace the second "
                   "notehead with a Rest (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-delete-test: prior onset not selected after "
                     "Delete (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    const auto after_surface = shell.test_snapshot_notation_surface();
    if (!after_surface.has_value() || after_surface == before_surface) {
      std::fprintf(stderr,
                   "notehead-delete-test: visible surface not re-published "
                   "after Delete (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Undo/redo round-trip through the handler's history.
    if (!handler.test_undo() ||
        !std::holds_alternative<graphscore::Note>(event_at(1))) {
      std::fprintf(stderr,
                   "notehead-delete-test: undo did not restore the notehead "
                   "(1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.test_redo() ||
        !std::holds_alternative<graphscore::Rest>(event_at(1))) {
      std::fprintf(stderr,
                   "notehead-delete-test: redo did not re-apply the delete "
                   "(1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1b: Backspace routes through the same delete path. ------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-delete-test: fixture build failed (1b)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->second_note_id);
    click_at(shell, point.x, point.y);
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kBackspace));
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    if (!std::holds_alternative<graphscore::Rest>(vc.events()[1])) {
      std::fprintf(stderr,
                   "notehead-delete-test: Backspace did not remove the second "
                   "notehead (1b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-delete-test: prior onset not selected after "
                     "Backspace (1b)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1c: deleting the voice's first event has no prior onset, so an
  //     insertion caret is left at the deleted onset (0). ------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-delete-test: fixture build failed (1c)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_note_id);
    click_at(shell, point.x, point.y);
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDelete));
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    if (!std::holds_alternative<graphscore::Rest>(vc.events()[0])) {
      std::fprintf(stderr,
                   "notehead-delete-test: Delete did not replace the first "
                   "notehead with a Rest (1c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr,
                   "notehead-delete-test: no committed selection after "
                   "deleting the first notehead (1c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto* caret = std::get_if<graphscore::InsertionCaretSet>(&*committed);
    if (caret == nullptr || caret->items().size() != 1u ||
        caret->items().front().position != graphscore::Rational(0)) {
      std::fprintf(stderr,
                   "notehead-delete-test: no insertion caret at onset 0 after "
                   "deleting the first notehead (1c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1d: deleting one chord notehead leaves the remaining pitch as a
  //     Note and selects the prior onset. ----------------------------------
  {
    auto fx = build_notehead_click_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-delete-test: fixture build failed (1d)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->chord_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->chord_note_id) {
        std::fprintf(stderr,
                     "notehead-delete-test: click did not select the chord "
                     "notehead (1d)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDelete));
    // The two-note chord is now a single Note carrying the surviving pitch
    // (G4); the E4 notehead that was clicked is gone.
    const auto surviving = [&]() -> std::optional<graphscore::Note> {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      for (const auto& event : vc.events()) {
        if (const auto* note = std::get_if<graphscore::Note>(&event)) {
          if (note->id == fx->chord_other_id) {
            return *note;
          }
        }
      }
      return std::nullopt;
    };
    const auto remaining = surviving();
    if (!remaining.has_value() ||
        remaining->pitch != spelled(graphscore::Letter::kG, 4)) {
      std::fprintf(stderr,
                   "notehead-delete-test: chord delete did not leave the "
                   "surviving pitch as a Note (1d)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The prior onset (the D4 note at event index 1) is now the committed
    // selection, matching selection_after_notehead_delete's Chord→prior
    // recovery branch.
    {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      const graphscore::NotationEntityId prior_id =
          graphscore::event_id(vc.events()[1]);
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != prior_id) {
        std::fprintf(stderr,
                     "notehead-delete-test: prior onset not selected after "
                     "chord delete (1d)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // The clicked E4 notehead is gone from the voice.
    for (const auto& event : handler.project()
                                 .find_node(fx->node_id)
                                 ->lane(fx->track_id)
                                 ->stave(fx->stave_id)
                                 ->voice(voice1)
                                 .events()) {
      if (const auto* chord = std::get_if<graphscore::Chord>(&event)) {
        for (const auto& note : chord->notes) {
          if (note.id == fx->chord_note_id) {
            std::fprintf(stderr,
                         "notehead-delete-test: clicked chord notehead still "
                         "present (1d)\n");
            shell.set_input_handler(nullptr);
            return 1;
          }
        }
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 2/3/4/5: every no-op case leaves the project unchanged: no
  //     selection, a stale notehead identity, a multi-notehead selection, a
  //     Shift chord (which stays M5-phase-19b range extension), and a
  //     Control/Alt/Meta chord (no binding this phase owns). ---------------
  {
    enum class NoOpCase : std::uint8_t {
      kNoSelection,
      kStaleIdentity,
      kMultiNotehead,
      kShiftChord,
      kModifierChord,
    };

    struct NoOpSpec {
      const char* label;
      NoOpCase    kind;
    };

    const std::array<NoOpSpec, 5> kNoOpCases{{
        {"2", NoOpCase::kNoSelection},
        {"3", NoOpCase::kStaleIdentity},
        {"4", NoOpCase::kMultiNotehead},
        {"5", NoOpCase::kShiftChord},
        {"5b", NoOpCase::kModifierChord},
    }};

    for (const NoOpSpec& test_case : kNoOpCases) {
      auto fx = build_notehead_move_fixture(metrics);
      if (!fx.has_value()) {
        std::fprintf(stderr,
                     "notehead-delete-test: fixture build failed (%s)\n",
                     test_case.label);
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fx->project),
                                      std::move(fx->layout), &shell);
      handler.set_metrics(&metrics);
      shell.set_input_handler(&handler);
      handler.set_active_tool(graphscore::ActiveTool::kSelection);

      bool armed = true;
      switch (test_case.kind) {
        case NoOpCase::kNoSelection:
          break;
        case NoOpCase::kStaleIdentity:
          armed = select_noteheads(
              handler, {graphscore::NoteheadItem{
                           fx->node_id, fx->track_id, fx->stave_id, voice1,
                           graphscore::NotationEntityId::generate()}});
          break;
        case NoOpCase::kMultiNotehead:
          armed = select_noteheads(
              handler,
              {graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                        voice1, fx->first_note_id},
               graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                        voice1, fx->second_note_id}});
          break;
        case NoOpCase::kShiftChord:
        case NoOpCase::kModifierChord:
          armed = select_noteheads(
              handler,
              {graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                        voice1, fx->second_note_id}});
          break;
      }
      if (!armed) {
        std::fprintf(stderr,
                     "notehead-delete-test: selection setup rejected (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }

      const auto event_at = [&](std::size_t index) {
        const auto* lane =
            handler.project().find_node(fx->node_id)->lane(fx->track_id);
        return lane->stave(fx->stave_id)->voice(voice1).events()[index];
      };

      if (test_case.kind == NoOpCase::kShiftChord) {
        shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kDelete));
        shell.dispatch_test_key_event(
            shift_key(graphscore::KeyCode::kBackspace));
      } else if (test_case.kind == NoOpCase::kModifierChord) {
        for (const graphscore::KeyCode code :
             {graphscore::KeyCode::kDelete, graphscore::KeyCode::kBackspace}) {
          graphscore::KeyEvent control = plain_key(code);
          control.modifiers.control    = true;
          shell.dispatch_test_key_event(control);
          graphscore::KeyEvent alt = plain_key(code);
          alt.modifiers.alt        = true;
          shell.dispatch_test_key_event(alt);
          graphscore::KeyEvent meta = plain_key(code);
          meta.modifiers.meta       = true;
          shell.dispatch_test_key_event(meta);
        }
      } else {
        shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDelete));
        shell.dispatch_test_key_event(
            plain_key(graphscore::KeyCode::kBackspace));
      }

      if (!std::holds_alternative<graphscore::Note>(event_at(0)) ||
          !std::holds_alternative<graphscore::Note>(event_at(1)) ||
          handler.test_undo_stack_size() != 0u) {
        std::fprintf(stderr,
                     "notehead-delete-test: a no-op case mutated the project "
                     "(%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }
  }

  std::printf("notehead-delete-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
