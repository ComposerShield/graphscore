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

// ---- notehead move tests (M5-phase-20) -------------------------------------
//
// Exercises SelectionToolHandler's unmodified Up/Down dispatch: with exactly
// one selected notehead, the notehead moves one diatonic staff step, its
// accidental is preserved, the same notehead identity stays selected, and the
// retained layout is refreshed. No selection, a non-notehead selection, a
// multi-notehead selection, a stale notehead, and Shift chords remain no-ops
// (Shift stays M5-phase-19 range extension).

namespace {
[[nodiscard]] int check_1(const graphscore::GlyphMetrics& metrics) {
  // --- test 1: a real click on the ordinary notehead selects it, then
  //     unmodified Up/Down moves it, retains identity/selection, re-publishes
  //     a different visible surface, and issues the post-edit audition. ----
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (1)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  handler.set_surface_publisher(
      [&shell](const graphscore::NotationLayout& layout) {
        return publish_headless_test_surface(layout, &shell);
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1 = voice_one();
  // Publish the initial surface so the test can observe a change rather
  // than an empty-surface-to-non-empty transition.
  if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
    std::fprintf(stderr,
                 "notehead-move-test: initial surface publish failed\n");
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
                   "notehead-move-test: click did not select the ordinary "
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
  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
  const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);
  if (first_pitch() != c4) {
    std::fprintf(stderr, "notehead-move-test: expected C4 before move (1)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != d4) {
    std::fprintf(stderr, "notehead-move-test: Up did not move C4 to D4 (1)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: selection changed after Up (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }
  // Visible surface re-published: different surface bytes than before.
  const auto after_surface = shell.test_snapshot_notation_surface();
  if (!after_surface.has_value() || after_surface == before_surface) {
    std::fprintf(stderr,
                 "notehead-move-test: visible surface not re-published "
                 "after Up (1)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  // Audition: the post-edit sounding pitch (D4 = MIDI 62) on the track.
  {
    const auto& audition = handler.last_audition();
    if (!audition.has_value() || audition->track_id != fx->track_id ||
        audition->pitches.size() != 1u || audition->pitches[0].value() != 62) {
      std::fprintf(stderr, "notehead-move-test: no D4 audition after Up (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDown));
  if (first_pitch() != c4) {
    std::fprintf(stderr,
                 "notehead-move-test: Down did not move D4 back to C4 (1)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_note_id) {
      std::fprintf(stderr,
                   "notehead-move-test: selection changed after Down (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_1b(const graphscore::GlyphMetrics& metrics) {
  // --- test 1b: clicking one chord notehead selects and moves only that
  //     notehead; the other chord notehead is untouched. ------------------
  auto fx = build_notehead_click_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (1b)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
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
                   "notehead-move-test: click did not select the chord "
                   "notehead (1b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
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
  if (notehead_pitch(fx->chord_note_id) != spelled(graphscore::Letter::kF, 4) ||
      notehead_pitch(fx->chord_other_id) !=
          spelled(graphscore::Letter::kG, 4)) {
    std::fprintf(stderr,
                 "notehead-move-test: chord notehead move wrong (1b)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_1c(const graphscore::GlyphMetrics& metrics) {
  // --- test 1c: clicking a grace notehead selects and moves it. ----------
  auto fx = build_notehead_click_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (1c)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
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
                   "notehead-move-test: click did not select the grace "
                   "notehead (1c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  const auto grace_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice_one());
    return vc.grace_groups()[0].notes[0].pitch;
  };
  if (grace_pitch() != spelled(graphscore::Letter::kG, 4)) {
    std::fprintf(stderr,
                 "notehead-move-test: Up did not move the grace note (1c)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_1d(const graphscore::GlyphMetrics& metrics) {
  // --- test 1d: the production default project is an incomplete voice (two
  //     quarter notes, no trailing rests). Moving a notehead must change only
  //     its pitch and must not materialize unrelated rests. ----------------
  auto dp = build_default_project(metrics);
  if (!dp.has_value()) {
    std::fprintf(stderr, "notehead-move-test: default project failed (1d)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(dp->project), std::move(dp->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  handler.set_surface_publisher(
      [&shell](const graphscore::NotationLayout& layout) {
        return publish_headless_test_surface(layout, &shell);
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1 = voice_one();
  const auto*             lane =
      handler.project().find_node(dp->node_id)->lane(dp->track_id);
  const graphscore::NotationEntityId first_id = graphscore::event_id(
      lane->stave(dp->stave_id)->voice(voice1).events()[0]);
  const std::size_t before_count =
      lane->stave(dp->stave_id)->voice(voice1).events().size();
  if (before_count != 2u) {
    std::fprintf(
        stderr, "notehead-move-test: default project expected 2 events (1d)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  const graphscore::NotationPoint point =
      notehead_origin(handler.layout(), first_id);
  click_at(shell, point.x, point.y);
  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

  const auto& vc = lane->stave(dp->stave_id)->voice(voice1);
  const bool  has_rest =
      std::ranges::any_of(vc.events(), [](const graphscore::VoiceEvent& event) {
        return std::holds_alternative<graphscore::Rest>(event);
      });
  if (vc.events().size() != 2u || has_rest) {
    std::fprintf(stderr,
                 "notehead-move-test: move in the incomplete default project "
                 "changed rhythm or created rests (1d)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_1e(const graphscore::GlyphMetrics& metrics) {
  // --- test 1e: a cross-measure tie chain. Clicking either endpoint and
  //     pressing Up must move the whole chain, and the incremental layout
  //     refresh must re-layout BOTH measures so neither endpoint's retained
  //     surface stays stale. One-measure-per-system options place the two
  //     tied measures in two systems, so a single-measure invalidation would
  //     visibly leave the remote system stale. ----------------------------
  const graphscore::NotationLayoutOptions narrow_options = [] {
    graphscore::NotationLayoutOptions options;
    options.system_width          = 160.0;
    options.left_margin           = 20.0;
    options.right_margin          = 20.0;
    options.minimum_measure_width = 120.0;
    options.whole_note_spacing    = 120.0;
    return options;
  }();
  auto fx = build_cross_measure_tie_fixture(metrics, narrow_options);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (1e)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  handler.set_layout_options(narrow_options);
  handler.set_surface_publisher(
      [&shell](const graphscore::NotationLayout& layout) {
        return publish_headless_test_surface(layout, &shell);
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  handler.warm_layout_cache();

  if (handler.layout().systems.size() < 2u) {
    std::fprintf(stderr,
                 "notehead-move-test: cross-measure fixture expected at "
                 "least two systems (1e)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
    std::fprintf(stderr,
                 "notehead-move-test: initial surface publish failed (1e)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  const auto before_surface = shell.test_snapshot_notation_surface();

  // Both endpoints start at C4, so their notehead glyphs share one y.
  const double before_a = notehead_origin(handler.layout(), fx->first_id).y;
  const double before_b = notehead_origin(handler.layout(), fx->second_id).y;

  const graphscore::NotationPoint point =
      notehead_origin(handler.layout(), fx->first_id);
  click_at(shell, point.x, point.y);
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_id) {
      std::fprintf(stderr,
                   "notehead-move-test: click did not select the tied "
                   "notehead (1e)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

  const double after_a = notehead_origin(handler.layout(), fx->first_id).y;
  const double after_b = notehead_origin(handler.layout(), fx->second_id).y;
  const double delta_a = before_a - after_a;
  const double delta_b = before_b - after_b;
  // Both chain endpoints must have moved up by the same diatonic step. A
  // stale remote measure would leave the far notehead's glyph at its old
  // C4 position, giving delta_b == 0 and failing the first check.
  if (delta_a <= 0.0 || delta_b <= 0.0) {
    std::fprintf(stderr,
                 "notehead-move-test: a cross-measure tie endpoint did not "
                 "move up (1e)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  if (std::abs(delta_a - delta_b) > 1e-6) {
    std::fprintf(stderr,
                 "notehead-move-test: cross-measure tie endpoints moved by "
                 "different amounts (1e)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  // The visible surface was re-published with different content.
  const auto after_surface = shell.test_snapshot_notation_surface();
  if (!after_surface.has_value() || after_surface == before_surface) {
    std::fprintf(stderr,
                 "notehead-move-test: visible surface not re-published for "
                 "the cross-measure move (1e)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_1f(const graphscore::GlyphMetrics& metrics) {
  // --- test 1f: the FIRST local move under a production-equivalent startup
  //     (cache seeded exactly as run() seeds it) rebuilds only the affected
  //     system, not every system. ------------------------------------------
  const graphscore::NotationLayoutOptions narrow_options = [] {
    graphscore::NotationLayoutOptions options;
    options.system_width          = 160.0;
    options.left_margin           = 20.0;
    options.right_margin          = 20.0;
    options.minimum_measure_width = 120.0;
    options.whole_note_spacing    = 120.0;
    return options;
  }();
  auto fx = build_two_system_local_fixture(metrics, narrow_options);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (1f)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  handler.set_layout_options(narrow_options);
  // run() seeds the cache at startup; reproduce that seeding here so the
  // first move below is the handler's first edit, not a test-only warm.
  handler.warm_layout_cache();
  handler.set_surface_publisher(
      [&shell](const graphscore::NotationLayout& layout) {
        return publish_headless_test_surface(layout, &shell);
      });
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  if (handler.layout().systems.size() < 2u) {
    std::fprintf(stderr,
                 "notehead-move-test: two-system fixture expected at least "
                 "two systems (1f)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  const graphscore::NotationPoint point =
      notehead_origin(handler.layout(), fx->first_id);
  click_at(shell, point.x, point.y);
  {
    const auto* set = committed_notehead_set(handler);
    if (set == nullptr || set->items().size() != 1u ||
        set->items()[0].entity != fx->first_id) {
      std::fprintf(stderr,
                   "notehead-move-test: click did not select the measure-0 "
                   "note (1f)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

  // The first edit rebuilt only system 0 and reused system 1. A cold-cache
  // full reset would show every system in rebuilt_systems instead.
  const auto& work = handler.test_last_layout_work();
  if (work.rebuilt_systems != (std::vector<std::size_t>{0}) ||
      work.reused_systems != (std::vector<std::size_t>{1})) {
    std::fprintf(stderr,
                 "notehead-move-test: first local move rebuilt %zu system(s) "
                 "and reused %zu, expected rebuild {0} / reuse {1} (1f)\n",
                 work.rebuilt_systems.size(), work.reused_systems.size());
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_2(const graphscore::GlyphMetrics& metrics) {
  // --- test 2: no selection -> Up is a no-op. ----------------------------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (2)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
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

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != c4 ||
      handler.drag_state().committed_selection().has_value()) {
    std::fprintf(stderr,
                 "notehead-move-test: Up with no selection mutated (2)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_3(const graphscore::GlyphMetrics& metrics) {
  // --- test 3: stale notehead identity -> Up is a no-op. -----------------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (3)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1 = voice_one();
  if (!select_noteheads(handler,
                        {graphscore::NoteheadItem{
                            fx->node_id, fx->track_id, fx->stave_id, voice1,
                            graphscore::NotationEntityId::generate()}})) {
    std::fprintf(stderr, "notehead-move-test: stale selection rejected\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  const auto first_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    return std::get<graphscore::Note>(vc.events().front()).pitch;
  };
  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != c4) {
    std::fprintf(stderr,
                 "notehead-move-test: Up with stale notehead mutated (3)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_4(const graphscore::GlyphMetrics& metrics) {
  // --- test 4: multi-notehead selection -> Up is a no-op. ----------------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (4)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1 = voice_one();
  if (!select_noteheads(
          handler,
          {graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                    voice1, fx->first_note_id},
           graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                    voice1, fx->second_note_id}})) {
    std::fprintf(stderr,
                 "notehead-move-test: multi-notehead selection rejected\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  const auto first_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    return std::get<graphscore::Note>(vc.events().front()).pitch;
  };
  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (first_pitch() != c4) {
    std::fprintf(stderr,
                 "notehead-move-test: Up with multi-notehead selection "
                 "mutated (4)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_5(const graphscore::GlyphMetrics& metrics) {
  // --- test 5: Shift+Up with a notehead selection stays range extension
  //     (a no-op without a range set); the notehead does not move. --------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (5)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
                                  &shell);
  handler.set_metrics(&metrics);
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const graphscore::Voice voice1 = voice_one();
  if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                     fx->node_id, fx->track_id, fx->stave_id,
                                     voice1, fx->first_note_id}})) {
    std::fprintf(stderr, "notehead-move-test: notehead selection rejected\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  const auto first_pitch = [&]() {
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
    return std::get<graphscore::Note>(vc.events().front()).pitch;
  };
  const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);

  shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
  if (first_pitch() != c4) {
    std::fprintf(stderr,
                 "notehead-move-test: Shift+Up moved the notehead (5)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}

[[nodiscard]] int check_6(const graphscore::GlyphMetrics& metrics) {
  // --- test 6: unmodified Up with a committed range selection is a no-op
  //     that leaves the range selection intact. ---------------------------
  auto fx = build_notehead_move_fixture(metrics);
  if (!fx.has_value()) {
    std::fprintf(stderr, "notehead-move-test: fixture build failed (6)\n");
    return 1;
  }
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fx->project), std::move(fx->layout),
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
                 "notehead-move-test: range selection setup failed (6)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }

  shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
  if (handler.drag_state().committed_selection() != before) {
    std::fprintf(stderr,
                 "notehead-move-test: Up with a range selection changed the "
                 "selection (6)\n");
    shell.set_input_handler(nullptr);
    return 1;
  }
  shell.set_input_handler(nullptr);
  return 0;
}
}  // namespace

int notehead_move_test() {
  const SelfTestMetrics metrics;
  if (const int rc = check_1(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_1b(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_1c(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_1d(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_1e(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_1f(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_2(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_3(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_4(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_5(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_6(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_notehead_move_7(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_notehead_move_7b(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_notehead_move_8(metrics); rc != 0) {
    return rc;
  }
  if (const int rc = check_notehead_move_9(metrics); rc != 0) {
    return rc;
  }
  std::printf("notehead-move-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
