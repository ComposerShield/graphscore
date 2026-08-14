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
[[nodiscard]] int check_notehead_move_7(
    const graphscore::GlyphMetrics& metrics) {
  // --- test 7: a failing surface publisher rolls the move back completely:
  //     project, layout, surface, selection/highlight, history, and audition
  //     stay unchanged, and the next move succeeds. ------------------------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (7)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  bool fail_next_publish = true;
  handler.set_surface_publisher(
      [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
        if (fail_next_publish) {
          fail_next_publish = false;
          return graphscore::ShellResult{
              graphscore::ShellError::kRenderingSetupFailed,
              "injected publish failure"};
        }
        return publish_headless_test_surface(layout, &shell);
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1      = voice_one();
  const auto              first_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    return std::get<graphscore::Note>(vc.events().front()).pitch;
  };
  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
  const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

  // Publish the initial surface so a failed move can be observed as a
  // non-change (not an empty->non-empty transition).
  if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
    std::fprintf(stderr,
                 "notehead-move-test: initial surface publish failed (7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  const graphscore::NotationPoint point =
      notehead_origin(handler.layout(), fx->first_note_id);
  click_at(shell, point.x, point.y);
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: click did not select the note (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // Pre-move snapshots of every coherent observable.
  const graphscore::SpelledPitch before_pitch = first_pitch();
  const auto        before_surface   = shell.test_snapshot_notation_surface();
  const auto        before_highlight = shell.test_snapshot_highlight_rects();
  const auto        before_layout    = handler.layout();
  const bool        before_audition  = handler.last_audition().has_value();
  const std::size_t before_undo      = handler.test_undo_stack_size();
  const std::size_t before_redo      = handler.test_redo_stack_size();

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

  // The publish failed, so every observable must be unchanged.
  if (first_pitch() != before_pitch) {
    std::fprintf(stderr,
                 "notehead-move-test: project mutated on publish failure "
                 "(7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.layout() != before_layout) {
    std::fprintf(stderr,
                 "notehead-move-test: layout committed on publish failure "
                 "(7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (shell.test_snapshot_notation_surface() != before_surface) {
    std::fprintf(stderr,
                 "notehead-move-test: surface changed on publish failure "
                 "(7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (shell.test_snapshot_highlight_rects() != before_highlight) {
    std::fprintf(stderr,
                 "notehead-move-test: highlight changed on publish failure "
                 "(7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.test_undo_stack_size() != before_undo ||
      handler.test_redo_stack_size() != before_redo) {
    std::fprintf(stderr,
                 "notehead-move-test: history changed on publish failure "
                 "(7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.last_audition().has_value() != before_audition) {
    std::fprintf(stderr,
                 "notehead-move-test: audition changed on publish failure "
                 "(7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: selection changed on publish failure "
                   "(7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }
  if (first_pitch() != c4) {
    std::fprintf(stderr,
                 "notehead-move-test: pitch not C4 after rollback (7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  // The next move (publisher now healthy) must succeed and move the note.
  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != d4) {
    std::fprintf(stderr,
                 "notehead-move-test: move after rollback did not move "
                 "the note (7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.test_undo_stack_size() != 1u) {
    std::fprintf(stderr,
                 "notehead-move-test: successful move after rollback not "
                 "recorded in history (7)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_notehead_move_7b(
    const graphscore::GlyphMetrics& metrics) {
  // --- test 7b: a failed publication preserves PRE-EXISTING redo history.
  //     A prior move is undone (leaving a redo entry), then a move whose
  //     publish fails must restore the exact project, undo/redo stacks,
  //     surface, highlight, layout, selection, and audition; the pre-existing
  //     redo must remain executable afterward, and a successful retry must
  //     commit normally (clearing redo). -----------------------------------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (7b)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  bool fail_next_publish = false;
  handler.set_surface_publisher(
      [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
        if (fail_next_publish) {
          fail_next_publish = false;
          return graphscore::ShellResult{
              graphscore::ShellError::kRenderingSetupFailed,
              "injected publish failure"};
        }
        return publish_headless_test_surface(layout, &shell);
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1      = voice_one();
  const auto              first_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    return std::get<graphscore::Note>(vc.events().front()).pitch;
  };
  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
  const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

  // Publish the initial surface, select the notehead, and make one
  // successful move (C4 -> D4).
  if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
    std::fprintf(stderr,
                 "notehead-move-test: initial surface publish failed (7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  const graphscore::NotationPoint point =
      notehead_origin(handler.layout(), fx->first_note_id);
  click_at(shell, point.x, point.y);
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: click did not select the note (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }
  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != d4 || handler.test_undo_stack_size() != 1u ||
      handler.test_redo_stack_size() != 0u) {
    std::fprintf(stderr,
                 "notehead-move-test: setup move did not commit (7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  // Undo it, leaving a non-empty redo stack (the exact state the earlier
  // execute_new-clears-redo path would have destroyed).
  if (!handler.test_undo() || first_pitch() != c4 ||
      handler.test_undo_stack_size() != 0u ||
      handler.test_redo_stack_size() != 1u) {
    std::fprintf(stderr,
                 "notehead-move-test: setup undo did not leave redo (7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  // Snapshot every coherent observable before the failing move.
  const auto before_surface   = shell.test_snapshot_notation_surface();
  const auto before_highlight = shell.test_snapshot_highlight_rects();
  const auto before_layout    = handler.layout();
  const bool before_audition  = handler.last_audition().has_value();

  // Arm the publisher failure and move; the transaction must abort.
  fail_next_publish = true;
  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

  // The publish failed, so every observable must be unchanged — including
  // the pre-existing redo entry, which execute_new() would have cleared.
  if (first_pitch() != c4) {
    std::fprintf(stderr,
                 "notehead-move-test: project mutated on publish failure "
                 "(7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.layout() != before_layout) {
    std::fprintf(stderr,
                 "notehead-move-test: layout committed on publish failure "
                 "(7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (shell.test_snapshot_notation_surface() != before_surface) {
    std::fprintf(stderr,
                 "notehead-move-test: surface changed on publish failure "
                 "(7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (shell.test_snapshot_highlight_rects() != before_highlight) {
    std::fprintf(stderr,
                 "notehead-move-test: highlight changed on publish failure "
                 "(7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.last_audition().has_value() != before_audition) {
    std::fprintf(stderr,
                 "notehead-move-test: audition changed on publish failure "
                 "(7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.test_undo_stack_size() != 0u ||
      handler.test_redo_stack_size() != 1u) {
    std::fprintf(stderr,
                 "notehead-move-test: history (including redo) changed on "
                 "publish failure (7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: selection changed on publish failure "
                   "(7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // The pre-existing redo entry must still be executable.
  if (!handler.test_redo() || first_pitch() != d4) {
    std::fprintf(stderr,
                 "notehead-move-test: redo not executable after failed "
                 "publish (7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  // Re-establish a redo entry, then a healthy retry must commit normally
  // (clearing redo, exactly execute_new's success path).
  if (!handler.test_undo() || handler.test_redo_stack_size() != 1u) {
    std::fprintf(stderr, "notehead-move-test: re-establish redo failed (7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != d4 || handler.test_undo_stack_size() != 1u ||
      handler.test_redo_stack_size() != 0u) {
    std::fprintf(stderr,
                 "notehead-move-test: retry after rollback did not commit "
                 "and clear redo (7b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_notehead_move_8(
    const graphscore::GlyphMetrics& metrics) {
  // --- test 8: a persistent rollback failure poisons the history and blocks
  //     further mutation: the authoritative project stays at the post-edit
  //     pitch (the rollback never completed), the visible surface stays the
  //     last successfully published one, and the handler is unavailable. ----
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (8)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  bool fail_next_publish = true;
  handler.set_surface_publisher(
      [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
        if (fail_next_publish) {
          fail_next_publish = false;
          return graphscore::ShellResult{
              graphscore::ShellError::kRenderingSetupFailed,
              "injected publish failure"};
        }
        return publish_headless_test_surface(layout, &shell);
      });
  // Every move command's undo fails persistently (1,000,000 is effectively
  // unbounded for this fixture), so the rollback can never complete.
  handler.set_move_command_factory(
      [](const graphscore::Project&        project,
         const graphscore::NoteheadItem&   item,
         graphscore::NoteheadStepDirection direction) {
        auto command =
            graphscore::make_move_notehead_command(project, item, direction);
        if (command == nullptr) {
          return std::unique_ptr<graphscore::Command>{};
        }
        return std::unique_ptr<graphscore::Command>(
            new FailUndoCommand(std::move(command), 1'000'000));
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1      = voice_one();
  const auto              first_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    return std::get<graphscore::Note>(vc.events().front()).pitch;
  };
  const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

  if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
    std::fprintf(stderr,
                 "notehead-move-test: initial surface publish failed (8)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  const auto before_surface = shell.test_snapshot_notation_surface();

  const graphscore::NotationPoint point =
      notehead_origin(handler.layout(), fx->first_note_id);
  click_at(shell, point.x, point.y);
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: click did not select the note (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

  // The rollback never completed, so the project stays at the post-edit
  // pitch; the surface is unchanged and the handler is unavailable.
  if (first_pitch() != d4) {
    std::fprintf(stderr,
                 "notehead-move-test: project not at post-edit pitch after "
                 "persistent rollback failure (8)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (shell.test_snapshot_notation_surface() != before_surface) {
    std::fprintf(stderr,
                 "notehead-move-test: surface changed on persistent rollback "
                 "failure (8)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (!handler.history_unavailable()) {
    std::fprintf(stderr,
                 "notehead-move-test: handler not unavailable after "
                 "persistent rollback failure (8)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  // Further mutation is blocked: another Up leaves the project untouched.
  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != d4) {
    std::fprintf(stderr,
                 "notehead-move-test: blocked move mutated the project (8)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_notehead_move_9(
    const graphscore::GlyphMetrics& metrics) {
  // --- test 9: a one-shot rollback failure recovers coherently: the
  //     authoritative project/history are restored, the cache/layout/highlight
  //     stay coherent, the surface stays the last successfully published one,
  //     and the next move succeeds. ----------------------------------------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (9)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  bool fail_next_publish = true;
  handler.set_surface_publisher(
      [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
        if (fail_next_publish) {
          fail_next_publish = false;
          return graphscore::ShellResult{
              graphscore::ShellError::kRenderingSetupFailed,
              "injected publish failure"};
        }
        return publish_headless_test_surface(layout, &shell);
      });
  // The move command's undo fails exactly once, then delegates.
  handler.set_move_command_factory(
      [](const graphscore::Project&        project,
         const graphscore::NoteheadItem&   item,
         graphscore::NoteheadStepDirection direction) {
        auto command =
            graphscore::make_move_notehead_command(project, item, direction);
        if (command == nullptr) {
          return std::unique_ptr<graphscore::Command>{};
        }
        return std::unique_ptr<graphscore::Command>(
            new FailUndoCommand(std::move(command), 1));
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1      = voice_one();
  const auto              first_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    return std::get<graphscore::Note>(vc.events().front()).pitch;
  };
  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
  const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

  if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
    std::fprintf(stderr,
                 "notehead-move-test: initial surface publish failed (9)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  const auto before_surface = shell.test_snapshot_notation_surface();

  const graphscore::NotationPoint point =
      notehead_origin(handler.layout(), fx->first_note_id);
  click_at(shell, point.x, point.y);
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: click did not select the note (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

  // The rollback failed once then recovered: the project is back at C4, the
  // handler is available again, and the surface is unchanged.
  if (first_pitch() != c4) {
    std::fprintf(stderr,
                 "notehead-move-test: project not restored after one-shot "
                 "rollback recovery (9)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (handler.history_unavailable()) {
    std::fprintf(stderr,
                 "notehead-move-test: handler still unavailable after "
                 "one-shot rollback recovery (9)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (shell.test_snapshot_notation_surface() != before_surface) {
    std::fprintf(stderr,
                 "notehead-move-test: surface changed after one-shot rollback "
                 "recovery (9)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  // The next move (publisher now healthy) succeeds and commits normally.
  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != d4 || handler.test_undo_stack_size() != 1u) {
    std::fprintf(stderr,
                 "notehead-move-test: move after one-shot rollback recovery "
                 "did not succeed (9)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

}  // namespace graphscore::writer_app
