// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../command_palette.hpp"
#include "../key_bindings.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace graphscore::writer_app {

// ---- M5-phase-27: nonvisual command-palette model --------------------------

namespace {

struct PaletteFixture {
  graphscore::Project        project;
  graphscore::NodeId         node_id;
  graphscore::TrackId        track_id;
  graphscore::StaveId        stave_id;
  graphscore::NotationLayout layout;
};

[[nodiscard]] std::optional<PaletteFixture> build_palette_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Palette"};
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
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
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
  return PaletteFixture{std::move(project), node_id, track_id, stave_id,
                        std::move(*layout_result.layout)};
}

[[nodiscard]] graphscore::KeyEvent primary_logical(graphscore::LogicalKey key,
                                                   PrimaryModifier primary) {
  graphscore::KeyEvent event;
  event.logical = key;
  if (primary == PrimaryModifier::kMeta) {
    event.modifiers.meta = true;
  } else {
    event.modifiers.control = true;
  }
  return event;
}

[[nodiscard]] graphscore::KeyEvent logical_key(graphscore::LogicalKey key) {
  graphscore::KeyEvent event;
  event.logical = key;
  return event;
}

[[nodiscard]] bool select_full_measure(SelectionToolHandler& handler,
                                       const PaletteFixture& fixture) {
  const auto set =
      graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
          fixture.node_id, fixture.track_id, fixture.stave_id, 0}});
  if (!set.has_value()) {
    return false;
  }
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

}  // namespace

int command_palette_test() {
  const SelfTestMetrics metrics;

  // --- test 1: Primary+K toggles the palette; the same chord closes it;
  //     Escape also dismisses it. ------------------------------------------
  {
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (1)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kK, kPlatformPrimaryModifier));
    if (!handler.command_palette_open() || !shell.test_text_input_active()) {
      std::fprintf(stderr,
                   "command-palette-test: Primary+K did not open/activate "
                   "text input (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kK, kPlatformPrimaryModifier));
    if (handler.command_palette_open() || shell.test_text_input_active()) {
      std::fprintf(stderr,
                   "command-palette-test: Primary+K did not close/deactivate "
                   "text input (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kK, kPlatformPrimaryModifier));
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEscape));
    if (handler.command_palette_open() || shell.test_text_input_active()) {
      std::fprintf(stderr,
                   "command-palette-test: Escape did not close/deactivate "
                   "text input (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Unregistering an active text consumer must also stop generation before
    // the raw handler pointer is cleared.
    handler.toggle_command_palette();
    shell.set_input_handler(nullptr);
    if (shell.test_text_input_active()) {
      std::fprintf(stderr,
                   "command-palette-test: unregistration left text input "
                   "active (1)\n");
      return 1;
    }
  }

  // --- test 2: while open, notation bindings are suppressed; composed text
  //     (the text-input channel) accumulates as filter text, while character
  //     KEY presses no longer do (§5 context 2, §4). ------------------------
  {
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (2)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    handler.toggle_command_palette();
    const auto before = handler.drag_state().committed_selection();
    // A character KEY press while the palette is open is consumed: it must
    // neither fire the `R` notation binding nor accumulate filter text (the
    // filter is fed by composed text input, a separate channel).
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kR));
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "command-palette-test: notation binding fired while palette "
                   "open (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.command_palette_filter().empty()) {
      std::fprintf(stderr,
                   "command-palette-test: key press accumulated filter (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Composed text input accumulates the filter.
    shell.dispatch_test_text_input(
        graphscore::TextInputEvent{std::string("r")});
    if (handler.command_palette_filter() != "r") {
      std::fprintf(stderr,
                   "command-palette-test: text input did not accumulate "
                   "(2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: inventory, case-insensitive search, and availability. -------
  {
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (3)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    // The inventory enumerates the expected stable rows.
    const auto& inventory = palette_inventory();
    const auto  has_name  = [&](const std::string& name) {
      return std::any_of(
          inventory.begin(), inventory.end(),
          [&](const PaletteCommand& command) { return command.name == name; });
    };
    if (!has_name("Cut") || !has_name("Copy") || !has_name("Paste") ||
        !has_name("Undo") || !has_name("Redo") || !has_name("Duration whole")) {
      std::fprintf(stderr, "command-palette-test: inventory incomplete (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Cut is unavailable with no selection, available with a full-measure
    // selection.
    if (handler.palette_command_available(PaletteCommandId::kCut)) {
      std::fprintf(
          stderr,
          "command-palette-test: Cut available with no selection (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_full_measure(handler, *fixture)) {
      std::fprintf(stderr, "command-palette-test: selection failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.palette_command_available(PaletteCommandId::kCut)) {
      std::fprintf(stderr,
                   "command-palette-test: Cut unavailable with measure (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Paste is unavailable with an empty clipboard, available after a copy.
    if (handler.palette_command_available(PaletteCommandId::kPaste)) {
      std::fprintf(
          stderr,
          "command-palette-test: Paste available with empty clipboard (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.run_palette_command(PaletteCommandId::kCopy)) {
      std::fprintf(stderr, "command-palette-test: copy execution failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.palette_command_available(PaletteCommandId::kPaste)) {
      std::fprintf(stderr,
                   "command-palette-test: Paste unavailable after copy (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Search filters name+description case-insensitively.
    handler.command_palette_set_filter("cut");
    const auto filtered = handler.command_palette_filtered();
    if (filtered.size() != 1u || filtered.front().name != "Cut") {
      std::fprintf(stderr, "command-palette-test: search filter wrong (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.command_palette_set_filter("CUT");
    if (handler.command_palette_filtered().size() != 1u) {
      std::fprintf(
          stderr, "command-palette-test: search is not case-insensitive (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: running a palette row performs the identical action: running
  //     "Cut" while it is available clears the range and populates the
  //     clipboard. ---------------------------------------------------------
  {
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (4)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, *fixture)) {
      std::fprintf(stderr, "command-palette-test: selection failed (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.run_palette_command(PaletteCommandId::kCut)) {
      std::fprintf(stderr, "command-palette-test: cut execution failed (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.clipboard_has_fragment()) {
      std::fprintf(
          stderr, "command-palette-test: cut did not populate clipboard (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: the inventory is exhaustive — exactly one row per
  //     PaletteCommandId, Backspace and Delete coalesced into one row, and
  //     the chord-less accessible range controls and M5-phase-28 structural
  //     measure-editing, tuplet, articulation, stem, dynamic, hairpin, pedal,
  //     tie, slur, and beam-override rows present. ---------------------------
  {
    const auto& inventory = palette_inventory();
    const int   count =
        static_cast<int>(PaletteCommandId::kRemoveBeamOverride) + 1;
    if (inventory.size() != static_cast<std::size_t>(count)) {
      std::fprintf(stderr,
                   "command-palette-test: inventory size %zu != %d "
                   "(5)\n",
                   inventory.size(), count);
      return 1;
    }
    std::vector<bool> seen(static_cast<std::size_t>(count), false);
    for (const PaletteCommand& row : inventory) {
      const int index = static_cast<int>(row.id);
      if (index < 0 || index >= count ||
          seen[static_cast<std::size_t>(index)]) {
        std::fprintf(stderr,
                     "command-palette-test: duplicate/out-of-range id (5)\n");
        return 1;
      }
      seen[static_cast<std::size_t>(index)] = true;
      if (row.name.empty() || row.description.empty()) {
        std::fprintf(stderr,
                     "command-palette-test: empty name/description (5)\n");
        return 1;
      }
      const bool is_accessible =
          row.id == PaletteCommandId::kAccessibleRangeStart ||
          row.id == PaletteCommandId::kAccessibleRangeEnd ||
          row.id == PaletteCommandId::kAccessibleRangeStaffScope;
      const bool is_structural =
          row.id == PaletteCommandId::kInsertMeasureBefore ||
          row.id == PaletteCommandId::kAppendMeasure ||
          row.id == PaletteCommandId::kDeleteMeasure ||
          row.id == PaletteCommandId::kCreateTriplet ||
          row.id == PaletteCommandId::kTupletRatioEntry ||
          row.id == PaletteCommandId::kRemoveTuplet ||
          row.id >= PaletteCommandId::kApplyAccent;
      if ((is_accessible || is_structural) != row.chord_hint.empty()) {
        std::fprintf(stderr,
                     "command-palette-test: chord-hint/chord-less mismatch "
                     "(5)\n");
        return 1;
      }
    }
    for (int i = 0; i < count; ++i) {
      if (!seen[static_cast<std::size_t>(i)]) {
        std::fprintf(stderr,
                     "command-palette-test: missing inventory id %d (5)\n", i);
        return 1;
      }
    }
    // Backspace and Delete are one row naming one action.
    const auto* delete_row = [&]() -> const PaletteCommand* {
      for (const auto& row : inventory) {
        if (row.id == PaletteCommandId::kDeleteNotehead) {
          return &row;
        }
      }
      return nullptr;
    }();
    if (delete_row == nullptr || delete_row->chord_hint != "Backspace/Delete") {
      std::fprintf(
          stderr, "command-palette-test: Backspace/Delete not coalesced (5)\n");
      return 1;
    }
  }

  // --- test 6: availability and disabled reasons track the same
  //     preconditions and tool gates as the chords. -------------------------
  {
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (6)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    // Cut: unavailable with no selection, available with a full measure.
    if (handler.palette_command_available(PaletteCommandId::kCut)) {
      std::fprintf(stderr, "command-palette-test: Cut available empty (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_full_measure(handler, *fixture)) {
      std::fprintf(stderr, "command-palette-test: select failed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.palette_command_available(PaletteCommandId::kCut)) {
      std::fprintf(stderr, "command-palette-test: Cut unavailable (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(std::nullopt);
    if (handler.palette_command_unavailable_reason(PaletteCommandId::kCut) !=
        "requires a full-measure or range selection") {
      std::fprintf(stderr, "command-palette-test: Cut reason wrong (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Entry-tool actions are available exactly while kNoteEntry is active.
    if (handler.palette_command_available(PaletteCommandId::kDurationWhole)) {
      std::fprintf(stderr,
                   "command-palette-test: duration available in selection "
                   "(6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kDurationWhole) !=
        "note-entry tool is not active") {
      std::fprintf(stderr, "command-palette-test: duration reason wrong (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (!handler.palette_command_available(PaletteCommandId::kDurationWhole)) {
      std::fprintf(stderr,
                   "command-palette-test: duration unavailable in note entry "
                   "(6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Range actions are available only in kSelection with a committed range.
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    if (handler.palette_command_available(PaletteCommandId::kRangeEdgeLater)) {
      std::fprintf(stderr, "command-palette-test: range available empty (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kRangeEdgeLater) != "no range selection") {
      std::fprintf(stderr, "command-palette-test: range reason wrong (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.palette_command_unavailable_reason(
            PaletteCommandId::kRangeEdgeLater) !=
        "selection tool is not active") {
      std::fprintf(stderr,
                   "command-palette-test: range tool-gate reason wrong (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Paste: unavailable with an empty clipboard.
    if (handler.palette_command_available(PaletteCommandId::kPaste)) {
      std::fprintf(stderr, "command-palette-test: Paste available empty (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: identical execution/fallback — the Entry-tool actions are a
  //     silent no-op while kSelection is active, and running a palette row
  //     performs the same action as its chord. ------------------------------
  {
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (7)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    // Entry actions in kSelection: a silent no-op with no diagnostic and no
    // palette/cursor mutation (the same no-op as pressing the chord).
    const auto diagnostics_before = handler.diagnostics().size();
    if (handler.run_palette_command(PaletteCommandId::kPitchA) ||
        handler.run_palette_command(PaletteCommandId::kDurationWhole) ||
        handler.run_palette_command(PaletteCommandId::kOctaveUp)) {
      std::fprintf(
          stderr,
          "command-palette-test: Entry action fired in selection (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.diagnostics().size() != diagnostics_before ||
        handler.armed_note_value() != graphscore::NoteValue::kQuarter ||
        handler.octave_offset() != 0) {
      std::fprintf(stderr,
                   "command-palette-test: Entry no-op mutated state (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Toggle tool via the palette (Any family): running the row is identical
    // to the `N` chord.
    if (!handler.run_palette_command(PaletteCommandId::kToggleTool) ||
        handler.active_tool() != graphscore::ActiveTool::kNoteEntry) {
      std::fprintf(stderr, "command-palette-test: toggle tool failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Step-entry pitch in kNoteEntry (Entry family): running the row commits,
    // identical to the `A` chord.
    if (!handler.run_palette_command(PaletteCommandId::kPitchA)) {
      std::fprintf(stderr, "command-palette-test: pitch commit failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* node  = handler.project().find_node(fixture->node_id);
      const auto& voice = node->lane(fixture->track_id)
                              ->stave(fixture->stave_id)
                              ->voice(voice_one());
      if (voice.events().empty()) {
        std::fprintf(stderr,
                     "command-palette-test: pitch commit no event (7)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Undo via the palette (Any family): running the row undoes the commit.
    if (!handler.run_palette_command(PaletteCommandId::kUndo)) {
      std::fprintf(stderr, "command-palette-test: undo failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* node  = handler.project().find_node(fixture->node_id);
      const auto& voice = node->lane(fixture->track_id)
                              ->stave(fixture->stave_id)
                              ->voice(voice_one());
      if (!voice.events().empty()) {
        std::fprintf(stderr, "command-palette-test: undo left events (7)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 8: composed text input routes every printable string to the
  //     palette filter — `whole` matches the duration row, `staff` matches
  //     the staff-step rows, and a non-US logical text never reaches a
  //     notation action (§4, §5 context 2). ---------------------------------
  {
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (8)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);
    handler.toggle_command_palette();

    // `whole` (a string no single LogicalKey can name) matches "Duration
    // whole" by description.
    shell.dispatch_test_text_input(
        graphscore::TextInputEvent{std::string("whole")});
    {
      const auto filtered = handler.command_palette_filtered();
      const auto matches  = [&](PaletteCommandId id) {
        return std::any_of(
            filtered.begin(), filtered.end(),
            [&](const PaletteCommand& command) { return command.id == id; });
      };
      if (filtered.empty() || !matches(PaletteCommandId::kDurationWhole)) {
        std::fprintf(stderr,
                     "command-palette-test: `whole` did not match the duration "
                     "row (8)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // `staff` matches the staff-step rows (name substring).
    handler.command_palette_set_filter(std::string("staff"));
    {
      const auto filtered = handler.command_palette_filtered();
      const auto matches  = [&](PaletteCommandId id) {
        return std::any_of(
            filtered.begin(), filtered.end(),
            [&](const PaletteCommand& command) { return command.id == id; });
      };
      if (!matches(PaletteCommandId::kStaffStepPrevious) ||
          !matches(PaletteCommandId::kStaffStepNext)) {
        std::fprintf(stderr,
                     "command-palette-test: `staff` did not match staff-step "
                     "rows (8)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // A non-US logical text (an accented letter the LogicalKey enum cannot
    // name) routes to the filter and never fires a notation action.
    const auto selection_before = handler.drag_state().committed_selection();
    handler.command_palette_set_filter(std::string());
    shell.dispatch_test_text_input(
        graphscore::TextInputEvent{std::string("\xC3\xA9")});  // "é", UTF-8
    if (handler.command_palette_filter() != "\xC3\xA9") {
      std::fprintf(stderr,
                   "command-palette-test: non-US text did not route to filter "
                   "(8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.drag_state().committed_selection() != selection_before) {
      std::fprintf(stderr,
                   "command-palette-test: non-US text fired a notation action "
                   "(8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // While the palette is CLOSED, composed text is ignored entirely (the
    // notation context consumes no text input).
    handler.close_command_palette();
    shell.dispatch_test_text_input(
        graphscore::TextInputEvent{std::string("a")});
    if (!handler.command_palette_filter().empty()) {
      std::fprintf(stderr,
                   "command-palette-test: closed palette accumulated text "
                   "(8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 9: availability mirrors the chord's exact execution
  //     precondition/fallback (§11): grace-note interval/rest conversion,
  //     multi-item staff-step sources, invalid cut/copy extraction, no legal
  //     step-entry cursor for pitch, and stale notehead entities are all
  //     unavailable exactly where their chord would no-op. ------------------
  {
    // (a) Grace-note interval and rest conversion are unavailable, while the
    //     grace notehead is still eligible for move/accidental/delete.
    {
      auto fixture = build_notehead_click_fixture(metrics);
      if (!fixture.has_value()) {
        std::fprintf(stderr,
                     "command-palette-test: grace fixture failed (9)\n");
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fixture->project),
                                      std::move(fixture->layout), &shell);
      shell.set_input_handler(&handler);
      if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                         fixture->node_id, fixture->track_id,
                                         fixture->stave_id, voice_one(),
                                         fixture->grace_id}})) {
        std::fprintf(stderr, "command-palette-test: grace select failed (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (handler.palette_command_available(
              PaletteCommandId::kIntervalAbove2) ||
          handler.palette_command_available(PaletteCommandId::kConvertToRest)) {
        std::fprintf(stderr,
                     "command-palette-test: grace note offered interval/"
                     "rest conversion (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (!handler.palette_command_available(PaletteCommandId::kMoveNoteUp) ||
          !handler.palette_command_available(PaletteCommandId::kAccidentalUp) ||
          !handler.palette_command_available(
              PaletteCommandId::kDeleteNotehead)) {
        std::fprintf(stderr,
                     "command-palette-test: grace note lost move/accidental/"
                     "delete (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }

    // (b) A multi-item caret/rest set is not a staff-step source.
    {
      auto fixture = build_palette_fixture(metrics);
      if (!fixture.has_value()) {
        std::fprintf(stderr, "command-palette-test: fixture failed (9)\n");
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fixture->project),
                                      std::move(fixture->layout), &shell);
      shell.set_input_handler(&handler);
      const auto carets = graphscore::InsertionCaretSet::create(
          {graphscore::InsertionCaretItem{fixture->node_id, fixture->track_id,
                                          fixture->stave_id, voice_one(),
                                          graphscore::Rational(0)},
           graphscore::InsertionCaretItem{fixture->node_id, fixture->track_id,
                                          fixture->stave_id, voice_one(),
                                          graphscore::Rational(1)}});
      if (!carets.has_value()) {
        std::fprintf(stderr, "command-palette-test: caret build failed (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*carets});
      if (handler.palette_command_available(PaletteCommandId::kStaffStepNext)) {
        std::fprintf(stderr,
                     "command-palette-test: multi-item caret offered staff "
                     "step (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }

    // (c) An invalid (stale) full-measure selection is not a copy/cut target.
    {
      auto fixture = build_palette_fixture(metrics);
      if (!fixture.has_value()) {
        std::fprintf(stderr, "command-palette-test: fixture failed (9)\n");
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fixture->project),
                                      std::move(fixture->layout), &shell);
      shell.set_input_handler(&handler);
      const auto measure_set =
          graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
              fixture->node_id, fixture->track_id, fixture->stave_id, 2}});
      if (!measure_set.has_value()) {
        std::fprintf(stderr, "command-palette-test: measure failed (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*measure_set});
      if (handler.palette_command_available(PaletteCommandId::kCut) ||
          handler.palette_command_available(PaletteCommandId::kCopy)) {
        std::fprintf(stderr,
                     "command-palette-test: stale measure offered cut/copy "
                     "(9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }

    // (d) With no legal step-entry cursor, pitch/rest commits are
    //     unavailable while the palette-arming rows stay available.
    {
      graphscore::Project project{graphscore::ProjectId::generate(),
                                  "NoTimeline"};
      const auto          midi_channel = graphscore::MidiChannel::create(0);
      if (!midi_channel.has_value()) {
        std::fprintf(stderr, "command-palette-test: channel failed (9)\n");
        return 1;
      }
      (void)project.add_track(
          "Track",
          graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
          *midi_channel);
      const graphscore::NodeId   node_id = project.add_node("Node");
      graphscore::NotationLayout layout;
      layout.node_id = node_id;
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(project), std::move(layout),
                                      &shell);
      shell.set_input_handler(&handler);
      handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
      if (handler.palette_command_available(PaletteCommandId::kPitchA) ||
          handler.palette_command_available(PaletteCommandId::kStepEntryRest)) {
        std::fprintf(stderr,
                     "command-palette-test: no-cursor pitch offered (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (!handler.palette_command_available(
              PaletteCommandId::kDurationWhole) ||
          handler.palette_command_unavailable_reason(
              PaletteCommandId::kPitchA) !=
              "step-entry cursor is stale or absent") {
        std::fprintf(stderr,
                     "command-palette-test: no-cursor gating wrong (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }

    // (e) A stale notehead entity is not editable.
    {
      auto fixture = build_palette_fixture(metrics);
      if (!fixture.has_value()) {
        std::fprintf(stderr, "command-palette-test: fixture failed (9)\n");
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fixture->project),
                                      std::move(fixture->layout), &shell);
      shell.set_input_handler(&handler);
      if (!select_noteheads(
              handler,
              {graphscore::NoteheadItem{
                  fixture->node_id, fixture->track_id, fixture->stave_id,
                  voice_one(), graphscore::NotationEntityId::generate()}})) {
        std::fprintf(stderr, "command-palette-test: stale select failed (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (handler.palette_command_available(PaletteCommandId::kMoveNoteUp) ||
          handler.palette_command_available(PaletteCommandId::kAccidentalUp) ||
          handler.palette_command_available(
              PaletteCommandId::kDeleteNotehead) ||
          handler.palette_command_available(
              PaletteCommandId::kIntervalAbove2)) {
        std::fprintf(stderr,
                     "command-palette-test: stale notehead offered editing "
                     "(9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }
  }

  // --- test 10: availability is exact execution eligibility for the
  //     targeted boundary families: node-end/stale step-entry cursors,
  //     invalid paste placement, octave saturation, and Entry-tool cursor
  //     staff stepping. -----------------------------------------------------
  {
    // Node-end pitch/rest commits and pastes are constructible anchors but
    // invalid domain placements, so the palette must disable them.
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (10a)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    if (!select_full_measure(handler, *fixture) ||
        !handler.run_palette_command(PaletteCommandId::kCopy)) {
      std::fprintf(stderr, "command-palette-test: copy failed (10a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto end_caret =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::Rational(2)}});
    if (!end_caret.has_value()) {
      std::fprintf(stderr, "command-palette-test: caret failed (10a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*end_caret});
    if (handler.palette_command_available(PaletteCommandId::kPaste) ||
        handler.palette_command_unavailable_reason(PaletteCommandId::kPaste) !=
            "paste placement or meter is invalid") {
      std::fprintf(stderr,
                   "command-palette-test: invalid paste offered (10a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    (void)handler.step_entry_arm_duration(graphscore::NoteValue::kWhole);
    if (!handler.step_entry_commit_pitch(graphscore::Letter::kA) ||
        !handler.step_entry_commit_pitch(graphscore::Letter::kB)) {
      std::fprintf(stderr,
                   "command-palette-test: node-end setup failed (10a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.palette_command_available(PaletteCommandId::kPitchA) ||
        handler.palette_command_available(PaletteCommandId::kStepEntryRest)) {
      std::fprintf(stderr,
                   "command-palette-test: node-end commit offered (10a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    for (int i = 0; i < 8; ++i) {
      (void)handler.step_entry_step_octave(1);
    }
    if (handler.palette_command_available(PaletteCommandId::kOctaveUp) ||
        !handler.palette_command_available(PaletteCommandId::kOctaveDown) ||
        handler.palette_command_unavailable_reason(
            PaletteCommandId::kOctaveUp) !=
            "octave reference is at the upper bound") {
      std::fprintf(stderr,
                   "command-palette-test: octave saturation wrong (10a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }
  {
    // A stale node identity cannot produce a live cursor or commit.
    auto fixture = build_palette_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (10b)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);
    const auto stale =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            graphscore::NodeId::generate(), fixture->track_id,
            fixture->stave_id, voice_one(), graphscore::Rational(0)}});
    if (!stale.has_value()) {
      std::fprintf(stderr, "command-palette-test: stale caret failed (10b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*stale});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.palette_command_available(PaletteCommandId::kPitchA) ||
        handler.palette_command_available(PaletteCommandId::kStepEntryRest)) {
      std::fprintf(stderr,
                   "command-palette-test: stale cursor commit offered (10b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }
  {
    // Entry-tool staff-step eligibility follows the app-owned cursor, even
    // with no committed Selection.
    auto fixture = build_key_selection_project(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (10c)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);
    handler.set_committed_selection(std::nullopt);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    const auto before = handler.step_entry_cursor();
    if (!before.has_value() ||
        !handler.palette_command_available(PaletteCommandId::kStaffStepNext) ||
        !handler.run_palette_command(PaletteCommandId::kStaffStepNext) ||
        handler.step_entry_cursor() == before) {
      std::fprintf(stderr,
                   "command-palette-test: Entry cursor staff step wrong "
                   "(10c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 11: every project/history-mutating row is disabled while a
  //     failed rollback has poisoned CommandHistory. Copy and palette-only
  //     note-entry controls remain governed by their own preconditions. -----
  {
    auto fixture = build_notehead_move_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "command-palette-test: fixture failed (11)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    const auto measure =
        graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, 0}});
    if (!measure.has_value()) {
      std::fprintf(stderr, "command-palette-test: measure failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*measure});
    if (!handler.run_palette_command(PaletteCommandId::kCopy)) {
      std::fprintf(stderr, "command-palette-test: copy setup failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    if (!select_noteheads(
            handler, {graphscore::NoteheadItem{
                         fixture->node_id, fixture->track_id, fixture->stave_id,
                         voice_one(), fixture->first_note_id}})) {
      std::fprintf(stderr, "command-palette-test: select failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_surface_publisher([](const graphscore::NotationLayout&) {
      return graphscore::ShellResult{
          graphscore::ShellError::kRenderingSetupFailed,
          "injected publish failure"};
    });
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
    if (handler.move_selected_notehead(
            graphscore::NoteheadStepDirection::kUp) ||
        !handler.history_unavailable()) {
      std::fprintf(stderr, "command-palette-test: poison setup failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const std::vector<PaletteCommandId> mutating_rows = {
        PaletteCommandId::kMoveNoteUp,
        PaletteCommandId::kMoveNoteDown,
        PaletteCommandId::kAccidentalDown,
        PaletteCommandId::kAccidentalUp,
        PaletteCommandId::kDeleteNotehead,
        PaletteCommandId::kConvertToRest,
        PaletteCommandId::kIntervalAbove2,
        PaletteCommandId::kIntervalAbove3,
        PaletteCommandId::kIntervalAbove4,
        PaletteCommandId::kIntervalAbove5,
        PaletteCommandId::kIntervalAbove6,
        PaletteCommandId::kIntervalAbove7,
        PaletteCommandId::kIntervalAbove8,
        PaletteCommandId::kIntervalBelow2,
        PaletteCommandId::kIntervalBelow3,
        PaletteCommandId::kIntervalBelow4,
        PaletteCommandId::kIntervalBelow5,
        PaletteCommandId::kIntervalBelow6,
        PaletteCommandId::kIntervalBelow7,
        PaletteCommandId::kIntervalBelow8,
        PaletteCommandId::kPitchA,
        PaletteCommandId::kPitchB,
        PaletteCommandId::kPitchC,
        PaletteCommandId::kPitchD,
        PaletteCommandId::kPitchE,
        PaletteCommandId::kPitchF,
        PaletteCommandId::kPitchG,
        PaletteCommandId::kCut,
        PaletteCommandId::kPaste,
        PaletteCommandId::kUndo,
        PaletteCommandId::kRedo,
        PaletteCommandId::kStepEntryRest,
        PaletteCommandId::kInsertMeasureBefore,
        PaletteCommandId::kAppendMeasure,
        PaletteCommandId::kDeleteMeasure,
    };
    for (const PaletteCommandId id : mutating_rows) {
      if (handler.palette_command_available(id) ||
          handler.palette_command_unavailable_reason(id) !=
              "command history is unavailable") {
        std::fprintf(stderr,
                     "command-palette-test: poisoned mutating row enabled or "
                     "wrong reason (%d, 11)\n",
                     static_cast<int>(id));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    handler.set_committed_selection(graphscore::Selection{*measure});
    if (!handler.palette_command_available(PaletteCommandId::kCopy) ||
        handler.palette_command_available(PaletteCommandId::kCut) ||
        handler.palette_command_available(PaletteCommandId::kPaste)) {
      std::fprintf(stderr,
                   "command-palette-test: poisoned clipboard eligibility "
                   "wrong (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (!handler.palette_command_available(PaletteCommandId::kDurationWhole) ||
        !handler.palette_command_available(PaletteCommandId::kVoice2) ||
        !handler.palette_command_available(PaletteCommandId::kCycleDots) ||
        handler.palette_command_available(PaletteCommandId::kPitchA) ||
        handler.palette_command_available(PaletteCommandId::kStepEntryRest)) {
      std::fprintf(stderr,
                   "command-palette-test: poisoned entry eligibility wrong "
                   "(11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("command-palette-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
