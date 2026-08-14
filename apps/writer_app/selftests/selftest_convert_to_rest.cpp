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
namespace {
// Sets the committed selection to the given chord items. Returns false
// (leaving the handler untouched) if ChordSet::create rejects the items.
[[nodiscard]] bool select_chord(SelectionToolHandler&              handler,
                                std::vector<graphscore::ChordItem> items) {
  const auto set = graphscore::ChordSet::create(std::move(items));
  if (!set.has_value()) {
    return false;
  }
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

// The committed selection's own ChordSet, or nullptr when there is no
// committed selection or it is not that arm — the free-function counterpart
// of SelectionToolHandler::current_chord_set().
[[nodiscard]] const graphscore::ChordSet* committed_chord_set(
    const SelectionToolHandler& handler) {
  const auto& committed = handler.drag_state().committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::ChordSet>(&*committed);
}

// The committed selection's own RestSet, or nullptr when there is no
// committed selection or it is not that arm.
[[nodiscard]] const graphscore::RestSet* committed_rest_set(
    const SelectionToolHandler& handler) {
  const auto& committed = handler.drag_state().committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::RestSet>(&*committed);
}

// True if `layout` still draws `id`'s own tie curve -- a PathCommand whose
// id begins with "<id>/tie/segment/", the id shape add_span_segment
// (src/notation/notation_engraving.cpp) builds for a tied
// note/chord-pitch. Used to prove a retained-layout refresh actually re-laid
// out the measure holding a cleared tie, rather than leaving that measure's
// stale pre-edit commands (including a tie that no longer exists in the domain)
// in place.
[[nodiscard]] bool has_tie_curve_command(
    const graphscore::NotationLayout&   layout,
    const graphscore::NotationEntityId& id) {
  const std::string prefix = id.to_string() + "/tie/segment/";
  for (const auto& command : layout.commands) {
    const auto* path = std::get_if<graphscore::PathCommand>(&command);
    if (path != nullptr && path->id.value.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}
}  // namespace

int convert_to_rest_test() {
  const SelfTestMetrics metrics;

  const graphscore::Voice voice1 = voice_one();

  // --- test 1: a real click selects the second notehead, then `R` converts
  //     it to a normalized equal-duration Rest, selects the resulting Rest
  //     (RestSet on the preserved id), re-publishes a different visible
  //     surface, and undo/redo round-trips through the handler's history.
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "convert-to-rest-test: fixture build failed (1)\n");
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
          stderr, "convert-to-rest-test: initial surface publish failed (1)\n");
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
                     "convert-to-rest-test: click did not select the second "
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

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kR));
    if (!std::holds_alternative<graphscore::Rest>(event_at(1)) ||
        !std::holds_alternative<graphscore::Note>(event_at(0))) {
      std::fprintf(stderr,
                   "convert-to-rest-test: R did not replace the second "
                   "notehead with a Rest (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (graphscore::event_id(event_at(1)) != fx->second_note_id) {
      std::fprintf(stderr,
                   "convert-to-rest-test: the replacement Rest did not "
                   "preserve the source note's id (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_rest_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->second_note_id) {
        std::fprintf(stderr,
                     "convert-to-rest-test: converted rest not selected "
                     "after R (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    const auto after_surface = shell.test_snapshot_notation_surface();
    if (!after_surface.has_value() || after_surface == before_surface) {
      std::fprintf(stderr,
                   "convert-to-rest-test: visible surface not re-published "
                   "after R (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Undo/redo round-trip through the handler's history.
    if (!handler.test_undo() ||
        !std::holds_alternative<graphscore::Note>(event_at(1))) {
      std::fprintf(stderr,
                   "convert-to-rest-test: undo did not restore the notehead "
                   "(1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.test_redo() ||
        !std::holds_alternative<graphscore::Rest>(event_at(1))) {
      std::fprintf(stderr,
                   "convert-to-rest-test: redo did not re-apply the "
                   "conversion (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 2: clicking one pitch of a chord (a ChordNote) converts the
  //     WHOLE containing chord, not just that pitch, and the resulting
  //     Rest's preserved id is the CHORD's own id, not the clicked
  //     ChordNote's. ----------------------------------------------------
  {
    auto fx = build_notehead_click_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "convert-to-rest-test: fixture build failed (2)\n");
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
                     "convert-to-rest-test: click did not select the chord "
                     "notehead (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kR));
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto&             vc = lane->stave(fx->stave_id)->voice(voice1);
    const graphscore::Rest* converted =
        std::get_if<graphscore::Rest>(&vc.events()[2]);
    if (converted == nullptr || converted->id != fx->chord_id) {
      std::fprintf(stderr,
                   "convert-to-rest-test: R did not convert the whole chord "
                   "with the chord's own preserved id (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_rest_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->chord_id) {
        std::fprintf(stderr,
                     "convert-to-rest-test: converted rest not selected on "
                     "the chord's own id (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: a ChordSet selection converts the same chord, and the
  //     layout/surface actually refresh for this arm (refresh_layout must
  //     not silently no-op just because current_notehead_set() sees no
  //     NoteheadSet). ----------------------------------------------------
  {
    auto fx = build_notehead_click_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "convert-to-rest-test: fixture build failed (3)\n");
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
          stderr, "convert-to-rest-test: initial surface publish failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto before_surface = shell.test_snapshot_notation_surface();

    if (!select_chord(handler, {graphscore::ChordItem{fx->node_id, fx->track_id,
                                                      fx->stave_id, voice1,
                                                      fx->chord_id}})) {
      std::fprintf(stderr,
                   "convert-to-rest-test: ChordSet selection setup rejected "
                   "(3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_chord_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->chord_id) {
        std::fprintf(stderr,
                     "convert-to-rest-test: ChordSet selection setup did not "
                     "take effect (3)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kR));
    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto&             vc = lane->stave(fx->stave_id)->voice(voice1);
    const graphscore::Rest* converted =
        std::get_if<graphscore::Rest>(&vc.events()[2]);
    if (converted == nullptr || converted->id != fx->chord_id) {
      std::fprintf(stderr,
                   "convert-to-rest-test: ChordSet R did not convert the "
                   "chord (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_rest_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->chord_id) {
        std::fprintf(stderr,
                     "convert-to-rest-test: converted rest not selected "
                     "after ChordSet R (3)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    const auto after_surface = shell.test_snapshot_notation_surface();
    if (!after_surface.has_value() || after_surface == before_surface) {
      std::fprintf(stderr,
                   "convert-to-rest-test: visible surface not re-published "
                   "for the ChordSet arm (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: every no-op case leaves the project unchanged: no selection,
  //     a stale notehead identity, a multi-notehead selection, a multi-item
  //     ChordSet, a Shift chord (stays M5-phase-19b range extension), and a
  //     Control/Alt/Meta chord (no binding this phase owns). --------------
  {
    enum class NoOpCase : std::uint8_t {
      kNoSelection,
      kStaleIdentity,
      kMultiNotehead,
      kMultiChord,
      kShiftChord,
      kModifierChord,
    };

    struct NoOpSpec {
      const char* label;
      NoOpCase    kind;
    };

    const std::array<NoOpSpec, 6> kNoOpCases{{
        {"2", NoOpCase::kNoSelection},
        {"3", NoOpCase::kStaleIdentity},
        {"4", NoOpCase::kMultiNotehead},
        {"4b", NoOpCase::kMultiChord},
        {"5", NoOpCase::kShiftChord},
        {"5b", NoOpCase::kModifierChord},
    }};

    for (const NoOpSpec& test_case : kNoOpCases) {
      auto fx = build_notehead_move_fixture(metrics);
      if (!fx.has_value()) {
        std::fprintf(stderr,
                     "convert-to-rest-test: fixture build failed (%s)\n",
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
        case NoOpCase::kMultiChord:
          armed = select_chord(
              handler, {graphscore::ChordItem{
                            fx->node_id, fx->track_id, fx->stave_id, voice1,
                            graphscore::NotationEntityId::generate()},
                        graphscore::ChordItem{
                            fx->node_id, fx->track_id, fx->stave_id, voice1,
                            graphscore::NotationEntityId::generate()}});
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
                     "convert-to-rest-test: selection setup rejected (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }

      const auto event_at = [&](std::size_t index) {
        const auto* lane =
            handler.project().find_node(fx->node_id)->lane(fx->track_id);
        return lane->stave(fx->stave_id)->voice(voice1).events()[index];
      };
      const auto committed_before = handler.drag_state().committed_selection();

      if (test_case.kind == NoOpCase::kShiftChord) {
        shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kR));
      } else if (test_case.kind == NoOpCase::kModifierChord) {
        graphscore::KeyEvent control = plain_key(graphscore::KeyCode::kR);
        control.modifiers.control    = true;
        shell.dispatch_test_key_event(control);
        graphscore::KeyEvent alt = plain_key(graphscore::KeyCode::kR);
        alt.modifiers.alt        = true;
        shell.dispatch_test_key_event(alt);
        graphscore::KeyEvent meta = plain_key(graphscore::KeyCode::kR);
        meta.modifiers.meta       = true;
        shell.dispatch_test_key_event(meta);
      } else {
        shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kR));
      }

      if (!std::holds_alternative<graphscore::Note>(event_at(0)) ||
          !std::holds_alternative<graphscore::Note>(event_at(1)) ||
          handler.test_undo_stack_size() != 0u) {
        std::fprintf(stderr,
                     "convert-to-rest-test: a no-op case mutated the project "
                     "(%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (handler.drag_state().committed_selection() != committed_before) {
        std::fprintf(stderr,
                     "convert-to-rest-test: a no-op case changed the "
                     "committed selection (%s)\n",
                     test_case.label);
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }
  }

  // --- test 5: a ChordNote-addressed NoteheadSet selection converts the
  //     WHOLE containing chord, so the invalidation must cover every pitch
  //     of that chord -- including a sibling pitch's own cross-measure tie
  //     the clicked pitch itself was never part of. Measure 0 holds a
  //     Chord{C4, E4} with only E4 tied into measure 1's Chord{C4, E4};
  //     pressing R on measure 1's C4 ChordNote converts measure 1's whole
  //     chord and clears measure 0's E4 tie, but C4's own connected
  //     component never crosses the barline. A single-pitch invalidation
  //     scope (this finding) leaves measure 0's stale tie curve drawn. ----
  {
    const graphscore::NotationLayoutOptions narrow_options = [] {
      graphscore::NotationLayoutOptions options;
      options.system_width          = 160.0;
      options.left_margin           = 20.0;
      options.right_margin          = 20.0;
      options.minimum_measure_width = 120.0;
      options.whole_note_spacing    = 120.0;
      return options;
    }();
    auto fx = build_cross_measure_chord_tie_fixture(metrics, narrow_options);
    if (!fx.has_value()) {
      std::fprintf(stderr, "convert-to-rest-test: fixture build failed (5)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_layout_options(narrow_options);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    // Seed the retained incremental cache BEFORE the edit, exactly as run()
    // does at startup, so the R press below takes the incremental refresh
    // path rather than the first-call full rebuild (every measure, any
    // invalidation) that would otherwise mask this finding entirely.
    handler.warm_layout_cache();

    if (handler.layout().systems.size() < 2u) {
      std::fprintf(stderr,
                   "convert-to-rest-test: cross-measure chord-tie fixture "
                   "expected at least two systems (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    if (!has_tie_curve_command(handler.layout(), fx->measure0_e4_id)) {
      std::fprintf(stderr,
                   "convert-to-rest-test: fixture's own measure-0 tie curve "
                   "missing before the edit (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice1, fx->measure1_c4_id}})) {
      std::fprintf(stderr,
                   "convert-to-rest-test: NoteheadSet selection setup "
                   "rejected (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kR));

    const auto* lane =
        handler.project().find_node(fx->node_id)->lane(fx->track_id);
    const auto&             vc = lane->stave(fx->stave_id)->voice(voice1);
    const graphscore::Rest* converted =
        std::get_if<graphscore::Rest>(&vc.events()[4]);
    if (converted == nullptr || converted->id != fx->measure1_chord_id) {
      std::fprintf(stderr,
                   "convert-to-rest-test: R did not convert measure 1's "
                   "chord via its C4 ChordNote (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (has_tie_curve_command(handler.layout(), fx->measure0_e4_id)) {
      std::fprintf(stderr,
                   "convert-to-rest-test: measure 0's cleared E4 tie is "
                   "still drawn -- the ChordNote arm's invalidation did not "
                   "cover the containing chord's sibling pitch (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("convert-to-rest-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
