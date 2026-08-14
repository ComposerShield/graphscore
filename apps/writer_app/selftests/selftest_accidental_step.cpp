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
// ---- M5-phase-21: accidental step ----------------------------------------
//
// Exercises SelectionToolHandler's unmodified `-`/`=` dispatch: with exactly
// one selected notehead, its accidental steps one rung down/up the
// double-flat .. double-sharp ladder, its letter/octave and identity are
// preserved, the same notehead identity stays selected, the retained layout
// is refreshed and re-published, and the post-edit pitch is auditioned.
// Either end of the ladder is a hard reject that builds no command, records
// no history entry, and issues no audition. No selection, a non-notehead
// selection, a multi-notehead selection, a stale notehead, and Shift chords
// remain no-ops.
int accidental_step_test() {
  const SelfTestMetrics metrics;

  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
  const graphscore::SpelledPitch c_sharp4 =
      spelled(graphscore::Letter::kC, 4, graphscore::Accidental::kSharp);
  const graphscore::SpelledPitch c_double_sharp4 =
      spelled(graphscore::Letter::kC, 4, graphscore::Accidental::kDoubleSharp);
  const graphscore::SpelledPitch c_flat4 =
      spelled(graphscore::Letter::kC, 4, graphscore::Accidental::kFlat);
  const graphscore::SpelledPitch c_double_flat4 =
      spelled(graphscore::Letter::kC, 4, graphscore::Accidental::kDoubleFlat);

  // --- test 1: a real click selects the notehead, then unmodified `=`/`-`
  //     step its accidental, retain identity/selection, re-publish a
  //     different visible surface, and audition the post-edit pitch. ------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "accidental-step-test: fixture build failed (1)\n");
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

    const graphscore::Voice voice1 = voice_one();
    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "accidental-step-test: initial surface publish failed\n");
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
                     "accidental-step-test: click did not select the "
                     "notehead (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    const auto first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "accidental-step-test: expected C4 before the step (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
    if (first_pitch() != c_sharp4) {
      std::fprintf(stderr,
                   "accidental-step-test: `=` did not raise C4 to C#4 (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "accidental-step-test: selection changed after `=` (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    const auto after_surface = shell.test_snapshot_notation_surface();
    if (!after_surface.has_value() || after_surface == before_surface) {
      std::fprintf(stderr,
                   "accidental-step-test: visible surface not re-published "
                   "after `=` (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Audition: the post-edit sounding pitch (C#4 = MIDI 61) on the track.
    {
      const auto& audition = handler.last_audition();
      if (!audition.has_value() || audition->track_id != fx->track_id ||
          audition->pitches.size() != 1u ||
          audition->pitches[0].value() != 61) {
        std::fprintf(stderr,
                     "accidental-step-test: no C#4 audition after `=` (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kMinus));
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "accidental-step-test: `-` did not lower C#4 back to "
                   "C4 (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kMinus));
    if (first_pitch() != c_flat4) {
      std::fprintf(stderr,
                   "accidental-step-test: `-` did not lower C4 to Cb4 (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto& audition = handler.last_audition();
      if (!audition.has_value() || audition->pitches.size() != 1u ||
          audition->pitches[0].value() != 59) {
        std::fprintf(stderr,
                     "accidental-step-test: no Cb4 audition after `-` (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "accidental-step-test: selection changed after `-` (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1b/1c: the ladder ends are hard rejects, never clamps. `=` on a
  //     double-sharp and `-` on a double-flat build no command, so the
  //     project, the history, and the last audition are all untouched. -----
  {
    struct LadderEndCase {
      const char*              label;
      graphscore::KeyCode      code;
      graphscore::SpelledPitch end;       // pitch two steps from natural
      int                      end_midi;  // its sounding pitch
    };

    const std::array<LadderEndCase, 2> kLadderEnds{{
        {"1b", graphscore::KeyCode::kEquals, c_double_sharp4, 62},
        {"1c", graphscore::KeyCode::kMinus, c_double_flat4, 58},
    }};

    for (const LadderEndCase& test_case : kLadderEnds) {
      auto fx = build_notehead_move_fixture(metrics);
      if (!fx.has_value()) {
        std::fprintf(stderr,
                     "accidental-step-test: fixture build failed (%s)\n",
                     test_case.label);
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fx->project),
                                      std::move(fx->layout), &shell);
      handler.set_metrics(&metrics);
      handler.set_surface_publisher(
          [&shell](const graphscore::NotationLayout& layout) {
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

      if (!select_noteheads(
              handler,
              {graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                        voice1, fx->first_note_id}})) {
        std::fprintf(stderr, "accidental-step-test: selection rejected (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }

      // Two steps reach the end of the ladder from a natural.
      shell.dispatch_test_key_event(plain_key(test_case.code));
      shell.dispatch_test_key_event(plain_key(test_case.code));
      if (first_pitch() != test_case.end ||
          handler.test_undo_stack_size() != 2u) {
        std::fprintf(stderr,
                     "accidental-step-test: two steps did not reach the "
                     "ladder end (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      const auto before_surface = shell.test_snapshot_notation_surface();

      // The third step is off the ladder: rejected outright.
      shell.dispatch_test_key_event(plain_key(test_case.code));
      if (first_pitch() != test_case.end) {
        std::fprintf(stderr,
                     "accidental-step-test: a step past the ladder end "
                     "changed the pitch (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (handler.test_undo_stack_size() != 2u ||
          handler.test_redo_stack_size() != 0u) {
        std::fprintf(stderr,
                     "accidental-step-test: a step past the ladder end "
                     "recorded history (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (shell.test_snapshot_notation_surface() != before_surface) {
        std::fprintf(stderr,
                     "accidental-step-test: a step past the ladder end "
                     "re-published the surface (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      // No new audition was issued: the last one is still the successful
      // step's post-edit pitch.
      {
        const auto& audition = handler.last_audition();
        if (!audition.has_value() || audition->pitches.size() != 1u ||
            audition->pitches[0].value() != test_case.end_midi) {
          std::fprintf(stderr,
                       "accidental-step-test: a step past the ladder end "
                       "issued an audition (%s)\n",
                       test_case.label);
          shell.set_input_handler(nullptr);
          return 1;
        }
      }
      shell.set_input_handler(nullptr);
    }
  }

  // --- test 1d: clicking one chord notehead steps only that notehead's
  //     accidental; the other chord notehead is untouched. ----------------
  {
    auto fx = build_notehead_click_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "accidental-step-test: fixture build failed (1d)\n");
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

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->chord_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->chord_note_id) {
        std::fprintf(stderr,
                     "accidental-step-test: click did not select the chord "
                     "notehead (1d)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
    const auto chord = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice_one());
      for (const auto& event : vc.events()) {
        if (const auto* c = std::get_if<graphscore::Chord>(&event)) {
          if (c->id == fx->chord_id) {
            return *c;
          }
        }
      }
      return graphscore::Chord{};
    };
    const graphscore::Chord after = chord();
    const auto notehead_pitch     = [&after](graphscore::NotationEntityId id) {
      for (const auto& note : after.notes) {
        if (note.id == id) {
          return note.pitch;
        }
      }
      return graphscore::SpelledPitch{};
    };
    if (notehead_pitch(fx->chord_note_id) !=
            spelled(graphscore::Letter::kE, 4,
                    graphscore::Accidental::kSharp) ||
        notehead_pitch(fx->chord_other_id) !=
            spelled(graphscore::Letter::kG, 4)) {
      std::fprintf(stderr,
                   "accidental-step-test: chord notehead step wrong (1d)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1e: clicking a grace notehead steps its accidental. ----------
  {
    auto fx = build_notehead_click_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "accidental-step-test: fixture build failed (1e)\n");
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

    const graphscore::NotationPoint point =
        grace_notehead_origin(handler.layout(), fx->grace_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->grace_id) {
        std::fprintf(stderr,
                     "accidental-step-test: click did not select the grace "
                     "notehead (1e)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kMinus));
    const auto grace_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice_one());
      return vc.grace_groups()[0].notes[0].pitch;
    };
    if (grace_pitch() !=
        spelled(graphscore::Letter::kF, 4, graphscore::Accidental::kFlat)) {
      std::fprintf(stderr,
                   "accidental-step-test: `-` did not step the grace "
                   "note (1e)\n");
      shell.set_input_handler(nullptr);
      return 1;
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
                     "accidental-step-test: fixture build failed (%s)\n",
                     test_case.label);
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fx->project),
                                      std::move(fx->layout), &shell);
      handler.set_metrics(&metrics);
      shell.set_input_handler(&handler);
      handler.set_active_tool(graphscore::ActiveTool::kSelection);

      const graphscore::Voice voice1 = voice_one();
      bool                    armed  = true;
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
                                        voice1, fx->first_note_id}});
          break;
      }
      if (!armed) {
        std::fprintf(stderr,
                     "accidental-step-test: selection setup rejected (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }

      const auto first_pitch = [&]() {
        const auto* lane =
            handler.project().find_node(fx->node_id)->lane(fx->track_id);
        const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
        return std::get<graphscore::Note>(vc.events().front()).pitch;
      };

      if (test_case.kind == NoOpCase::kShiftChord) {
        shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kEquals));
        shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kMinus));
      } else if (test_case.kind == NoOpCase::kModifierChord) {
        for (const graphscore::KeyCode code :
             {graphscore::KeyCode::kEquals, graphscore::KeyCode::kMinus}) {
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
        shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
        shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kMinus));
      }
      if (first_pitch() != c4 || handler.test_undo_stack_size() != 0u ||
          handler.last_audition().has_value()) {
        std::fprintf(stderr,
                     "accidental-step-test: a no-op case mutated the "
                     "project (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (test_case.kind == NoOpCase::kNoSelection &&
          handler.drag_state().committed_selection().has_value()) {
        std::fprintf(stderr,
                     "accidental-step-test: a key press created a "
                     "selection (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }
  }

  // --- test 6: unmodified `=` with a committed range selection is a no-op
  //     that leaves the range selection intact. ---------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "accidental-step-test: fixture build failed (6)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
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
                   "accidental-step-test: range selection setup failed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "accidental-step-test: `=` with a range selection changed "
                   "the selection (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: a failing surface publisher rolls the step back completely:
  //     project, layout, surface, selection/highlight, history, and audition
  //     stay unchanged, and the next step succeeds. -------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "accidental-step-test: fixture build failed (7)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
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

    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(
          stderr, "accidental-step-test: initial surface publish failed (7)\n");
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
        std::fprintf(
            stderr,
            "accidental-step-test: click did not select the note (7)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    const graphscore::SpelledPitch before_pitch = first_pitch();
    const auto        before_surface   = shell.test_snapshot_notation_surface();
    const auto        before_highlight = shell.test_snapshot_highlight_rects();
    const auto        before_layout    = handler.layout();
    const bool        before_audition  = handler.last_audition().has_value();
    const std::size_t before_undo      = handler.test_undo_stack_size();
    const std::size_t before_redo      = handler.test_redo_stack_size();

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));

    if (first_pitch() != before_pitch || first_pitch() != c4) {
      std::fprintf(stderr,
                   "accidental-step-test: project mutated on publish "
                   "failure (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.layout() != before_layout) {
      std::fprintf(stderr,
                   "accidental-step-test: layout committed on publish "
                   "failure (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_notation_surface() != before_surface) {
      std::fprintf(stderr,
                   "accidental-step-test: surface changed on publish "
                   "failure (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_highlight_rects() != before_highlight) {
      std::fprintf(stderr,
                   "accidental-step-test: highlight changed on publish "
                   "failure (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.test_undo_stack_size() != before_undo ||
        handler.test_redo_stack_size() != before_redo) {
      std::fprintf(stderr,
                   "accidental-step-test: history changed on publish "
                   "failure (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.last_audition().has_value() != before_audition) {
      std::fprintf(stderr,
                   "accidental-step-test: audition changed on publish "
                   "failure (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "accidental-step-test: selection changed on publish "
                     "failure (7)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // The next step (publisher now healthy) must succeed.
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
    if (first_pitch() != c_sharp4 || handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "accidental-step-test: step after rollback did not "
                   "commit (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 8/9: rollback failure. A persistent undo failure poisons the
  //     history and blocks further mutation; a one-shot failure recovers and
  //     the next step succeeds. -------------------------------------------
  {
    struct RollbackCase {
      const char* label;
      int         fail_times;
    };

    const std::array<RollbackCase, 2> kRollbackCases{{
        {"8", 1'000'000},
        {"9", 1},
    }};

    for (const RollbackCase& test_case : kRollbackCases) {
      auto fx = build_notehead_move_fixture(metrics);
      if (!fx.has_value()) {
        std::fprintf(stderr,
                     "accidental-step-test: fixture build failed (%s)\n",
                     test_case.label);
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fx->project),
                                      std::move(fx->layout), &shell);
      handler.set_metrics(&metrics);
      bool fail_next_publish = true;
      handler.set_surface_publisher(
          [&shell,
           &fail_next_publish](const graphscore::NotationLayout& layout) {
            if (fail_next_publish) {
              fail_next_publish = false;
              return graphscore::ShellResult{
                  graphscore::ShellError::kRenderingSetupFailed,
                  "injected publish failure"};
            }
            return publish_headless_test_surface(layout, &shell);
          });
      const int fail_times = test_case.fail_times;
      handler.set_accidental_command_factory(
          [fail_times](const graphscore::Project&          project,
                       const graphscore::NoteheadItem&     item,
                       graphscore::AccidentalStepDirection direction) {
            auto command = graphscore::make_step_accidental_command(
                project, item, direction);
            if (command == nullptr) {
              return std::unique_ptr<graphscore::Command>{};
            }
            return std::unique_ptr<graphscore::Command>(
                new FailUndoCommand(std::move(command), fail_times));
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

      if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
        std::fprintf(stderr,
                     "accidental-step-test: initial surface publish failed "
                     "(%s)\n",
                     test_case.label);
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
                       "accidental-step-test: click did not select the note "
                       "(%s)\n",
                       test_case.label);
          shell.set_input_handler(nullptr);
          return 1;
        }
      }

      shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));

      // The surface is the last successfully published one either way.
      if (shell.test_snapshot_notation_surface() != before_surface) {
        std::fprintf(stderr,
                     "accidental-step-test: surface changed on rollback "
                     "failure (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }

      if (test_case.fail_times > 1) {
        // Persistent: the rollback never completed, so the project stays at
        // the post-edit spelling and the handler is unavailable.
        if (first_pitch() != c_sharp4 || !handler.history_unavailable()) {
          std::fprintf(stderr,
                       "accidental-step-test: persistent rollback failure did "
                       "not poison the history (%s)\n",
                       test_case.label);
          shell.set_input_handler(nullptr);
          return 1;
        }
        // Further mutation is blocked.
        shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
        if (first_pitch() != c_sharp4) {
          std::fprintf(stderr,
                       "accidental-step-test: a blocked step mutated the "
                       "project (%s)\n",
                       test_case.label);
          shell.set_input_handler(nullptr);
          return 1;
        }
      } else {
        // One-shot: recovered, so the project is back at C4 and the next
        // step commits normally.
        if (first_pitch() != c4 || handler.history_unavailable()) {
          std::fprintf(stderr,
                       "accidental-step-test: one-shot rollback failure did "
                       "not recover (%s)\n",
                       test_case.label);
          shell.set_input_handler(nullptr);
          return 1;
        }
        shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
        if (first_pitch() != c_sharp4 || handler.test_undo_stack_size() != 1u) {
          std::fprintf(stderr,
                       "accidental-step-test: step after one-shot rollback "
                       "recovery did not succeed (%s)\n",
                       test_case.label);
          shell.set_input_handler(nullptr);
          return 1;
        }
      }
      shell.set_input_handler(nullptr);
    }
  }

  std::printf("accidental-step-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
