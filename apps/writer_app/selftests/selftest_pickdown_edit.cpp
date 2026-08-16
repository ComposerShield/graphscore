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

// ---- M5-phase-31: final pickdown via an explicit node-end duration setting,
//     with a visually distinct transition boundary. Routed exclusively
//     through the command palette (no key chord). ---------------------------

namespace {

struct PickdownFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationLayout   layout;
  graphscore::NotationEntityId pickdown_note_id;
  bool                         has_pickdown_note = false;
};

[[nodiscard]] std::optional<PickdownFixture> build_pickdown_fixture(
    const graphscore::GlyphMetrics& metrics, bool pre_set_pickdown,
    bool with_pickdown_note) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Pickdown"};
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
      1, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  if (pre_set_pickdown) {
    const auto pickdown = graphscore::Rational::create(1, 4);
    if (!pickdown.has_value() || !timeline->set_pickdown(*pickdown).ok()) {
      return std::nullopt;
    }
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  graphscore::NotationEntityId pickdown_note_id;
  bool                         has_pickdown_note = false;
  if (with_pickdown_note) {
    const auto voice = *graphscore::Voice::create(1);
    const auto quarter =
        *graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
    const auto pitch =
        *graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
    auto& content = lane->stave(stave_id)->voice(voice);
    for (std::size_t index = 0; index < 4; ++index) {
      if (!content.append(graphscore::make_note(pitch, quarter)).ok()) {
        return std::nullopt;
      }
    }
    const auto pickdown_pitch =
        *graphscore::SpelledPitch::create(graphscore::Letter::kG, 4);
    const auto pickdown_note = graphscore::make_note(pickdown_pitch, quarter);
    pickdown_note_id         = pickdown_note.id;
    if (!content.append(std::move(pickdown_note)).ok() ||
        !content.normalize(project.find_node(node_id)->timeline()->node_end())
             .ok()) {
      return std::nullopt;
    }
    has_pickdown_note = true;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }
  return PickdownFixture{std::move(project),
                         node_id,
                         track_id,
                         stave_id,
                         std::move(*layout_result.layout),
                         pickdown_note_id,
                         has_pickdown_note};
}

// Finds a LineCommand whose id ends with `suffix`, or nullptr.
[[nodiscard]] const graphscore::LineCommand* find_line(
    const graphscore::NotationLayout& layout, const std::string& suffix) {
  for (const auto& command : layout.commands) {
    const auto* line = std::get_if<graphscore::LineCommand>(&command);
    if (line != nullptr && line->id.value.size() >= suffix.size() &&
        line->id.value.compare(line->id.value.size() - suffix.size(),
                               suffix.size(), suffix) == 0) {
      return line;
    }
  }
  return nullptr;
}

// Finds a GlyphCommand whose id ends with `suffix`, or nullptr.
[[nodiscard]] const graphscore::GlyphCommand* find_glyph(
    const graphscore::NotationLayout& layout, const std::string& suffix) {
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<graphscore::GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value.size() >= suffix.size() &&
        glyph->id.value.compare(glyph->id.value.size() - suffix.size(),
                                suffix.size(), suffix) == 0) {
      return glyph;
    }
  }
  return nullptr;
}

void prepare_handler(SelectionToolHandler&           handler,
                     const graphscore::GlyphMetrics& metrics) {
  handler.set_metrics(&metrics);
  handler.warm_layout_cache();
  handler.set_surface_publisher([](const graphscore::NotationLayout&) {
    return graphscore::ShellResult{};
  });
}

}  // namespace

int pickdown_edit_test() {
  const SelfTestMetrics   metrics;
  graphscore::WriterShell shell;

  // --- test 1: set, geometry, undo/redo, clear, availability, invalid
  //     durations, and the no-pickdown no-op. --------------------------------
  {
    auto fixture = build_pickdown_fixture(metrics, false, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "pickdown-edit-test: fixture failed (1)\n");
      return 1;
    }
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare_handler(handler, metrics);

    // Set is always available (chord-less parameter handoff); clear is
    // unavailable until a pickdown exists. Assert the two independently.
    if (!handler.palette_command_available(
            PaletteCommandId::kSetPickdownDuration) ||
        handler.palette_command_available(PaletteCommandId::kClearPickdown)) {
      std::fprintf(stderr,
                   "pickdown-edit-test: set unavailable or clear available "
                   "before set (1)\n");
      return 1;
    }
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kClearPickdown) != "no pickdown to clear") {
      std::fprintf(stderr,
                   "pickdown-edit-test: clear unavailable reason wrong "
                   "before set (1)\n");
      return 1;
    }
    if (!handler
             .palette_command_unavailable_reason(
                 PaletteCommandId::kSetPickdownDuration)
             .empty()) {
      std::fprintf(stderr,
                   "pickdown-edit-test: set unavailable reason non-empty "
                   "(1)\n");
      return 1;
    }

    if (!handler.apply_pickdown_duration(*graphscore::Rational::create(1, 4))) {
      std::fprintf(stderr, "pickdown-edit-test: set failed (1)\n");
      return 1;
    }
    {
      const auto* node     = handler.project().find_node(fixture->node_id);
      const auto  pickdown = node->timeline()->pickdown_duration();
      if (!pickdown.has_value() ||
          *pickdown != *graphscore::Rational::create(1, 4)) {
        std::fprintf(stderr, "pickdown-edit-test: duration not set (1)\n");
        return 1;
      }
    }
    if (!handler.palette_command_available(
            PaletteCommandId::kSetPickdownDuration) ||
        !handler.palette_command_available(PaletteCommandId::kClearPickdown)) {
      std::fprintf(stderr,
                   "pickdown-edit-test: set or clear unavailable after set "
                   "(1)\n");
      return 1;
    }
    // Distinct transition boundary and node-end barline; the ordinary final
    // barline at the boundary is replaced.
    {
      const auto& layout = handler.layout();
      if (find_line(layout, "pickdown-boundary/first") == nullptr ||
          find_line(layout, "pickdown-boundary/second") == nullptr ||
          find_line(layout, "pickdown-end-barline") == nullptr) {
        std::fprintf(stderr,
                     "pickdown-edit-test: boundary geometry missing "
                     "(1)\n");
        return 1;
      }
      if (find_line(layout, "measure/0/end-barline") != nullptr) {
        std::fprintf(stderr,
                     "pickdown-edit-test: ordinary end-barline not "
                     "replaced (1)\n");
        return 1;
      }
      const auto* first  = find_line(layout, "pickdown-boundary/first");
      const auto* second = find_line(layout, "pickdown-boundary/second");
      const auto* end    = find_line(layout, "pickdown-end-barline");
      if (second->from.x <= first->from.x || end->from.x <= second->from.x) {
        std::fprintf(stderr,
                     "pickdown-edit-test: boundary ordering wrong "
                     "(1)\n");
        return 1;
      }
      const double measure_width = layout.systems[0].measures[0].bounds.width;
      const double region_width  = end->from.x - first->from.x;
      if (std::abs(region_width - measure_width * 0.25) > 1e-9) {
        std::fprintf(stderr, "pickdown-edit-test: pickdown width wrong (1)\n");
        return 1;
      }
      const double staff_width = layout.systems[0].staves[0].bounds.width;
      if (std::abs(staff_width - measure_width - region_width) > 1e-9) {
        std::fprintf(stderr,
                     "pickdown-edit-test: staff width not extended "
                     "(1)\n");
        return 1;
      }
    }
    // Undo removes the pickdown; redo restores it.
    if (!handler.undo_action()) {
      std::fprintf(stderr, "pickdown-edit-test: undo failed (1)\n");
      return 1;
    }
    if (handler.palette_command_available(PaletteCommandId::kClearPickdown) ||
        find_line(handler.layout(), "pickdown-boundary/first") != nullptr) {
      std::fprintf(stderr,
                   "pickdown-edit-test: undo did not clear boundary "
                   "(1)\n");
      return 1;
    }
    if (!handler.redo_action() ||
        !handler.palette_command_available(PaletteCommandId::kClearPickdown) ||
        find_line(handler.layout(), "pickdown-boundary/first") == nullptr) {
      std::fprintf(stderr, "pickdown-edit-test: redo failed (1)\n");
      return 1;
    }
    // Clear removes the region and the boundary.
    if (!handler.clear_pickdown() ||
        handler.palette_command_available(PaletteCommandId::kClearPickdown) ||
        find_line(handler.layout(), "pickdown-boundary/first") != nullptr) {
      std::fprintf(stderr, "pickdown-edit-test: clear failed (1)\n");
      return 1;
    }
    // After clearing, clear is unavailable again with its own reason.
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kClearPickdown) != "no pickdown to clear") {
      std::fprintf(stderr,
                   "pickdown-edit-test: clear unavailable reason wrong after "
                   "clear (1)\n");
      return 1;
    }
    // Clearing again is a no-op with a diagnostic.
    const auto diagnostics_before = handler.diagnostics().size();
    if (handler.clear_pickdown() ||
        handler.diagnostics().size() == diagnostics_before) {
      std::fprintf(stderr,
                   "pickdown-edit-test: repeated clear not a "
                   "diagnosed no-op (1)\n");
      return 1;
    }
    // Invalid durations are diagnosed and mutate nothing.
    const auto timeline_before = [&] {
      const auto* node = handler.project().find_node(fixture->node_id);
      return node->timeline()->pickdown_duration();
    }();
    const auto undo_before = handler.test_undo_stack_size();
    if (handler.apply_pickdown_duration(graphscore::Rational(0)) ||
        handler.apply_pickdown_duration(graphscore::Rational(1)) ||
        handler.test_undo_stack_size() != undo_before ||
        handler.project()
                .find_node(fixture->node_id)
                ->timeline()
                ->pickdown_duration() != timeline_before) {
      std::fprintf(stderr,
                   "pickdown-edit-test: invalid duration mutated "
                   "state (1)\n");
      return 1;
    }
  }

  // --- test 2: palette parameter handoff and clear route. -------------------
  {
    auto fixture = build_pickdown_fixture(metrics, false, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "pickdown-edit-test: fixture failed (2)\n");
      return 1;
    }
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare_handler(handler, metrics);

    if (!handler.palette_command_available(
            PaletteCommandId::kSetPickdownDuration) ||
        !handler.run_palette_command(PaletteCommandId::kSetPickdownDuration) ||
        !handler.pickdown_duration_entry_requested()) {
      std::fprintf(stderr,
                   "pickdown-edit-test: parameter handoff failed "
                   "(2)\n");
      return 1;
    }
    if (!handler.apply_pickdown_duration(*graphscore::Rational::create(1, 8)) ||
        handler.pickdown_duration_entry_requested()) {
      std::fprintf(stderr,
                   "pickdown-edit-test: apply after handoff failed "
                   "(2)\n");
      return 1;
    }
    {
      const auto* node = handler.project().find_node(fixture->node_id);
      if (!node->timeline()->pickdown_duration().has_value() ||
          *node->timeline()->pickdown_duration() !=
              *graphscore::Rational::create(1, 8)) {
        std::fprintf(stderr,
                     "pickdown-edit-test: handoff duration wrong "
                     "(2)\n");
        return 1;
      }
    }
    // Clear is available once the pickdown exists.
    if (!handler.palette_command_available(PaletteCommandId::kClearPickdown)) {
      std::fprintf(stderr,
                   "pickdown-edit-test: clear unavailable after set "
                   "(2)\n");
      return 1;
    }
    if (!handler.run_palette_command(PaletteCommandId::kClearPickdown) ||
        handler.palette_command_available(PaletteCommandId::kClearPickdown)) {
      std::fprintf(stderr, "pickdown-edit-test: palette clear failed (2)\n");
      return 1;
    }
    // After clearing, clear is unavailable again with its own reason.
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kClearPickdown) != "no pickdown to clear") {
      std::fprintf(stderr,
                   "pickdown-edit-test: clear unavailable reason wrong after "
                   "clear (2)\n");
      return 1;
    }
  }

  // --- test 3: a pre-configured pickdown renders its note in the pickdown
  //     region, deterministically, in node-local coordinates. ----------------
  {
    auto fixture = build_pickdown_fixture(metrics, true, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "pickdown-edit-test: fixture failed (3)\n");
      return 1;
    }
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare_handler(handler, metrics);

    const auto& layout = handler.layout();
    const auto* first  = find_line(layout, "pickdown-boundary/first");
    const auto* end    = find_line(layout, "pickdown-end-barline");
    if (first == nullptr || end == nullptr) {
      std::fprintf(stderr, "pickdown-edit-test: boundary missing (3)\n");
      return 1;
    }
    const std::string notehead_suffix =
        fixture->pickdown_note_id.to_string() + "/notehead";
    const auto* notehead = find_glyph(layout, notehead_suffix);
    if (notehead == nullptr) {
      std::fprintf(stderr,
                   "pickdown-edit-test: pickdown note not rendered "
                   "(3)\n");
      return 1;
    }
    if (!(notehead->origin.x >= first->from.x &&
          notehead->origin.x < end->from.x)) {
      std::fprintf(stderr,
                   "pickdown-edit-test: pickdown note outside region "
                   "(3)\n");
      return 1;
    }
  }

  // --- test 4: a failed layout refresh leaves project, history, and layout
  //     atomic. --------------------------------------------------------------
  {
    auto fixture = build_pickdown_fixture(metrics, false, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "pickdown-edit-test: fixture failed (4)\n");
      return 1;
    }
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    handler.warm_layout_cache();
    handler.set_surface_publisher([](const graphscore::NotationLayout&) {
      return graphscore::ShellResult{
          graphscore::ShellError::kRenderingSetupFailed,
          "injected publish failure"};
    });

    const auto undo_before = handler.test_undo_stack_size();
    if (handler.apply_pickdown_duration(*graphscore::Rational::create(1, 4)) ||
        handler.test_undo_stack_size() != undo_before ||
        handler.project()
            .find_node(fixture->node_id)
            ->timeline()
            ->pickdown_duration()
            .has_value()) {
      std::fprintf(stderr,
                   "pickdown-edit-test: failed refresh not atomic "
                   "(4)\n");
      return 1;
    }
  }

  std::printf("pickdown-edit-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
