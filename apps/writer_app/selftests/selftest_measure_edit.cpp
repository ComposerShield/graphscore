// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../command_palette.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {

// ---- M5-phase-28b: structural measure editing (insert-before, append,
//     delete), routed exclusively through the command palette -- these
//     actions carry no key chord (docs/plan/05-notation-editor-action-table.md
//     §11). ---------------------------------------------------------------

namespace {

// A single-track, single-stave fixture with `measure_count` empty measures
// and no voice content: the minimal project a measure-structure edit needs,
// mirroring tests/notation/measure_edit_test.cpp's own base fixture. A
// `measure_count` of 1 is the minimal project that trips
// make_delete_measure_command's sole-measure guard.
struct MeasureFixture {
  graphscore::Project        project;
  graphscore::NodeId         node_id;
  graphscore::TrackId        track_id;
  graphscore::StaveId        stave_id;
  graphscore::NotationLayout layout;
};

[[nodiscard]] std::optional<MeasureFixture> build_measure_fixture(
    const graphscore::GlyphMetrics& metrics, std::size_t measure_count) {
  graphscore::Project project{graphscore::ProjectId::generate(), "MeasureEdit"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      measure_count,
      graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }
  return MeasureFixture{std::move(project), node_id, track_id, stave_id,
                        std::move(*layout_result.layout)};
}

// A two-measure fixture (2/4 then 4/4) carrying a pickdown region the domain
// only revalidates at DeleteMeasureCommand::execute() time
// (NodeTimeline::set_pickdown requires the pickdown strictly shorter than
// the timeline's own last main-region measure): deleting measure index 1
// leaves the 2/4 measure as the new last measure, whose length (1/2) is no
// longer greater than the retained pickdown (3/4), so the delete is valid at
// the notation layer (aligned, in range, not the sole measure) but invalid
// at the domain's own execute() -- mirroring
// tests/domain/measure_command_test.cpp's
// DeleteMeasureCommandTest.InvalidatedPickdownRejectsAtomically fixture.
[[nodiscard]] std::optional<MeasureFixture> build_pickdown_measure_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(),
                              "MeasurePickdown"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto short_signature = graphscore::TimeSignature::create(2, 4);
  const auto long_signature  = graphscore::TimeSignature::create(4, 4);
  if (!short_signature.has_value() || !long_signature.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures{
      graphscore::Measure{*short_signature, graphscore::KeySignature{}},
      graphscore::Measure{*long_signature, graphscore::KeySignature{}}};
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  const auto pickdown_duration = graphscore::Rational::create(3, 4);
  if (!pickdown_duration.has_value() ||
      !timeline->set_pickdown(*pickdown_duration).ok()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }
  return MeasureFixture{std::move(project), node_id, track_id, stave_id,
                        std::move(*layout_result.layout)};
}

[[nodiscard]] bool select_full_measure(SelectionToolHandler& handler,
                                       graphscore::NodeId    node_id,
                                       graphscore::TrackId   track_id,
                                       graphscore::StaveId   stave_id,
                                       std::size_t           measure_index) {
  const auto set =
      graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
          node_id, track_id, stave_id, measure_index}});
  if (!set.has_value()) {
    return false;
  }
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

[[nodiscard]] std::size_t node_measure_count(
    const SelectionToolHandler& handler, graphscore::NodeId node_id) {
  const auto* node = handler.project().find_node(node_id);
  if (node == nullptr || node->timeline() == nullptr) {
    return 0;
  }
  return node->timeline()->measures().measure_count();
}

// The committed selection's own FullMeasureSet, or nullptr when there is no
// committed selection or it is not that arm -- the free-function
// counterpart of the handler's own private current_measure_set().
[[nodiscard]] const graphscore::FullMeasureSet* committed_measure_set(
    const SelectionToolHandler& handler) {
  const auto& committed = handler.drag_state().committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::FullMeasureSet>(&*committed);
}

}  // namespace

int measure_edit_test() {
  const SelfTestMetrics metrics;

  // --- test 1: insert-before via the palette grows the measure count by one
  //     and remaps the committed selection to measure_index + 1, then a
  //     delete of a NON-last measure exercises the unclamped remap. ---------
  {
    auto fixture = build_measure_fixture(metrics, 2);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "measure-edit-test: fixture failed (1)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, fixture->node_id, fixture->track_id,
                             fixture->stave_id, 0)) {
      std::fprintf(stderr, "measure-edit-test: select failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.palette_command_available(
            PaletteCommandId::kInsertMeasureBefore)) {
      std::fprintf(stderr, "measure-edit-test: insert unavailable (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::size_t count_before =
        node_measure_count(handler, fixture->node_id);
    if (!handler.run_palette_command(PaletteCommandId::kInsertMeasureBefore)) {
      std::fprintf(stderr, "measure-edit-test: insert execution failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (node_measure_count(handler, fixture->node_id) != count_before + 1) {
      std::fprintf(stderr,
                   "measure-edit-test: insert did not grow count (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* result = committed_measure_set(handler);
      if (result == nullptr || result->items().size() != 1u ||
          result->items().front().measure_index != 1u ||
          result->items().front().track != fixture->track_id) {
        std::fprintf(stderr,
                     "measure-edit-test: insert selection remap wrong (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // The node now has 3 measures (0, 1, 2). Delete the FIRST one -- a
    // non-last delete, exercising the unclamped remap (new_count - 1 = 1,
    // so measure_index 0 is not clamped).
    if (!select_full_measure(handler, fixture->node_id, fixture->track_id,
                             fixture->stave_id, 0)) {
      std::fprintf(stderr, "measure-edit-test: re-select failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.palette_command_available(PaletteCommandId::kDeleteMeasure)) {
      std::fprintf(stderr, "measure-edit-test: delete unavailable (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.run_palette_command(PaletteCommandId::kDeleteMeasure)) {
      std::fprintf(stderr, "measure-edit-test: delete execution failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (node_measure_count(handler, fixture->node_id) != count_before) {
      std::fprintf(stderr,
                   "measure-edit-test: delete did not shrink count (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* result = committed_measure_set(handler);
      if (result == nullptr || result->items().size() != 1u ||
          result->items().front().measure_index != 0u) {
        std::fprintf(stderr,
                     "measure-edit-test: delete selection remap wrong (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 2: append via the palette targets the node's own end regardless
  //     of the selected measure, then a delete of the now-LAST measure
  //     exercises the clamped remap. ------------------------------------
  {
    auto fixture = build_measure_fixture(metrics, 2);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "measure-edit-test: fixture failed (2)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, fixture->node_id, fixture->track_id,
                             fixture->stave_id, 0)) {
      std::fprintf(stderr, "measure-edit-test: select failed (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::size_t count_before =
        node_measure_count(handler, fixture->node_id);
    if (!handler.palette_command_available(PaletteCommandId::kAppendMeasure)) {
      std::fprintf(stderr, "measure-edit-test: append unavailable (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.run_palette_command(PaletteCommandId::kAppendMeasure)) {
      std::fprintf(stderr, "measure-edit-test: append execution failed (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (node_measure_count(handler, fixture->node_id) != count_before + 1) {
      std::fprintf(stderr,
                   "measure-edit-test: append did not grow count (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      // The appended measure's index is the PRE-edit measure_count()
      // (count_before), regardless of the selected measure's own index.
      const auto* result = committed_measure_set(handler);
      if (result == nullptr || result->items().size() != 1u ||
          result->items().front().measure_index != count_before) {
        std::fprintf(stderr,
                     "measure-edit-test: append selection remap wrong (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Delete the new LAST measure: exercises the clamp
    // (new_count - 1 = count_before - 1) rather than the unclamped index.
    if (!select_full_measure(handler, fixture->node_id, fixture->track_id,
                             fixture->stave_id, count_before)) {
      std::fprintf(stderr, "measure-edit-test: re-select failed (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.run_palette_command(PaletteCommandId::kDeleteMeasure)) {
      std::fprintf(stderr, "measure-edit-test: delete execution failed (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (node_measure_count(handler, fixture->node_id) != count_before) {
      std::fprintf(stderr,
                   "measure-edit-test: delete did not shrink count (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* result = committed_measure_set(handler);
      if (result == nullptr || result->items().size() != 1u ||
          result->items().front().measure_index != count_before - 1u) {
        std::fprintf(
            stderr,
            "measure-edit-test: clamped delete selection remap wrong (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: the sole-measure delete guard makes "Delete measure"
  //     unavailable with an accurate reason, while "Insert measure before"
  //     and "Append measure" remain available -- delete and insert must be
  //     able to differ. ---------------------------------------------------
  {
    auto fixture = build_measure_fixture(metrics, 1);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "measure-edit-test: fixture failed (3)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, fixture->node_id, fixture->track_id,
                             fixture->stave_id, 0)) {
      std::fprintf(stderr, "measure-edit-test: select failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.palette_command_available(PaletteCommandId::kDeleteMeasure)) {
      std::fprintf(stderr,
                   "measure-edit-test: sole-measure delete offered (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kDeleteMeasure) !=
        "cannot delete the node's only measure") {
      std::fprintf(stderr,
                   "measure-edit-test: sole-measure delete reason wrong "
                   "(3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.palette_command_available(
            PaletteCommandId::kInsertMeasureBefore) ||
        !handler.palette_command_available(PaletteCommandId::kAppendMeasure)) {
      std::fprintf(stderr,
                   "measure-edit-test: insert/append unavailable while "
                   "delete is (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Growing past one measure makes the delete row available again.
    if (!handler.run_palette_command(PaletteCommandId::kAppendMeasure)) {
      std::fprintf(stderr, "measure-edit-test: append setup failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_full_measure(handler, fixture->node_id, fixture->track_id,
                             fixture->stave_id, 0) ||
        !handler.palette_command_available(PaletteCommandId::kDeleteMeasure)) {
      std::fprintf(stderr,
                   "measure-edit-test: delete stayed unavailable after "
                   "growth (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: a delete the domain only rejects at
  //     CommandHistory::begin_transaction time -- here, a pickdown
  //     invalidated by the new last measure's shorter length -- must return
  //     false, post a diagnostic, and leave the project and the committed
  //     selection exactly as they were. The fixture is proved to reach that
  //     branch, rather than an earlier notation-layer rejection, by
  //     asserting make_delete_measure_command still builds a command for it.
  // -----------------------------------------------------------------------
  {
    auto fixture = build_pickdown_measure_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "measure-edit-test: fixture failed (4)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, fixture->node_id, fixture->track_id,
                             fixture->stave_id, 1)) {
      std::fprintf(stderr, "measure-edit-test: select failed (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr, "measure-edit-test: no committed selection (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const graphscore::Selection selection_before = *committed;

    // Proof: the notation layer still builds a command for this selection
    // (aligned, in range, not the sole measure), so a false return from
    // run_palette_command below can only come from the execute()-time
    // rejection inside CommandHistory::begin_transaction, not an earlier
    // nullptr from make_delete_measure_command itself.
    if (graphscore::make_delete_measure_command(handler.project(),
                                                selection_before) == nullptr) {
      std::fprintf(stderr,
                   "measure-edit-test: fixture builds no command, wrong "
                   "rejection path (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const graphscore::Node original =
        *handler.project().find_node(fixture->node_id);
    const std::size_t diagnostics_before = handler.diagnostics().size();
    if (handler.run_palette_command(PaletteCommandId::kDeleteMeasure)) {
      std::fprintf(stderr,
                   "measure-edit-test: execute-time rejection wrongly "
                   "succeeded (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.diagnostics().size() != diagnostics_before + 1) {
      std::fprintf(stderr, "measure-edit-test: no diagnostic posted (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The diagnostic text itself is the proof this reached the
    // !transaction.active() branch specifically -- every other rejection in
    // delete_measure() (ineligible/misaligned selection, an invalidation
    // failure, a layout refresh failure, a commit failure) posts different
    // wording.
    if (handler.diagnostics().back() !=
        "delete measure: rejected (tuplet boundary, invalid pickdown, or "
        "history is full)") {
      std::fprintf(stderr,
                   "measure-edit-test: wrong rejection branch reached (4): "
                   "%s\n",
                   handler.diagnostics().back().c_str());
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (*handler.project().find_node(fixture->node_id) != original) {
      std::fprintf(stderr, "measure-edit-test: project mutated (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.drag_state().committed_selection().has_value() ||
        *handler.drag_state().committed_selection() != selection_before) {
      std::fprintf(stderr, "measure-edit-test: selection mutated (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: a misaligned FullMeasureSet whose FRONT item's node happens
  //     to carry only one measure must report the alignment reason, not the
  //     sole-measure reason -- the two rejections must never be conflated.
  // -----------------------------------------------------------------------
  {
    auto sole_fixture  = build_measure_fixture(metrics, 1);
    auto other_fixture = build_measure_fixture(metrics, 2);
    if (!sole_fixture.has_value() || !other_fixture.has_value()) {
      std::fprintf(stderr, "measure-edit-test: fixture failed (5)\n");
      return 1;
    }
    graphscore::Project      project       = std::move(sole_fixture->project);
    const graphscore::NodeId other_node_id = project.add_node("Other");
    auto* lane = project.find_node(other_node_id)->lane(sole_fixture->track_id);
    lane->ensure_stave(sole_fixture->stave_id);
    std::vector<graphscore::StaveDefinition> stave_defs;
    stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
    const auto time_sig = graphscore::TimeSignature::create(4, 4);
    if (!time_sig.has_value()) {
      std::fprintf(stderr, "measure-edit-test: time sig failed (5)\n");
      return 1;
    }
    std::vector<graphscore::Measure> measures(
        2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
    auto other_timeline =
        graphscore::NodeTimeline::create(std::move(measures), stave_defs);
    if (!other_timeline.has_value()) {
      std::fprintf(stderr, "measure-edit-test: other timeline failed (5)\n");
      return 1;
    }
    project.find_node(other_node_id)->set_timeline(std::move(*other_timeline));

    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(project),
                                    std::move(sole_fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    const auto misaligned = graphscore::FullMeasureSet::create(
        {graphscore::FullMeasureItem{sole_fixture->node_id,
                                     sole_fixture->track_id,
                                     sole_fixture->stave_id, 0},
         graphscore::FullMeasureItem{other_node_id, sole_fixture->track_id,
                                     sole_fixture->stave_id, 0}});
    if (!misaligned.has_value()) {
      std::fprintf(stderr, "measure-edit-test: misaligned set failed (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*misaligned});

    if (handler.palette_command_available(PaletteCommandId::kDeleteMeasure)) {
      std::fprintf(stderr,
                   "measure-edit-test: misaligned delete wrongly offered "
                   "(5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kDeleteMeasure) !=
        "requires an aligned full-measure selection") {
      std::fprintf(stderr,
                   "measure-edit-test: misaligned delete reason "
                   "misattributed to sole-measure guard (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("measure-edit-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
