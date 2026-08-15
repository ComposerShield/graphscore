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

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {

// ---- M5-phase-27: chord classes, collision rule, repeat policy, and the
//     explicit no-op remainder (action table §3/§6/§7.7/§9/§12). -----------

namespace {

[[nodiscard]] graphscore::KeyModifiers mods(bool shift, bool control, bool alt,
                                            bool meta) {
  graphscore::KeyModifiers modifiers;
  modifiers.shift   = shift;
  modifiers.control = control;
  modifiers.alt     = alt;
  modifiers.meta    = meta;
  return modifiers;
}

[[nodiscard]] graphscore::KeyEvent key_event(graphscore::KeyCode      code,
                                             graphscore::KeyModifiers modifiers,
                                             bool                     repeat,
                                             graphscore::LogicalKey   logical) {
  graphscore::KeyEvent event;
  event.code      = code;
  event.modifiers = modifiers;
  event.repeat    = repeat;
  event.logical   = logical;
  return event;
}

[[nodiscard]] graphscore::KeyModifiers primary_modifiers() {
  return (kPlatformPrimaryModifier == PrimaryModifier::kMeta)
             ? mods(false, false, false, true)
             : mods(false, true, false, false);
}

// The non-Primary modifier on this host (Control on macOS, Meta elsewhere),
// which the action table leaves unbound (§2).
[[nodiscard]] graphscore::KeyModifiers non_primary_modifiers() {
  return (kPlatformPrimaryModifier == PrimaryModifier::kMeta)
             ? mods(false, true, false, false)
             : mods(false, false, false, true);
}

// The voice-1 content of the handler's project for the given track/stave.
[[nodiscard]] const graphscore::VoiceContent& voice_of(
    const SelectionToolHandler& handler, graphscore::NodeId node,
    graphscore::TrackId track, graphscore::StaveId stave) {
  return handler.project().find_node(node)->lane(track)->stave(stave)->voice(
      voice_one());
}

// The first Note event's pitch in the addressed voice, or nullopt when the
// first event is not a Note.
[[nodiscard]] std::optional<graphscore::SpelledPitch> first_note_pitch(
    const SelectionToolHandler& handler, graphscore::NodeId node,
    graphscore::TrackId track, graphscore::StaveId stave) {
  const auto& events = voice_of(handler, node, track, stave).events();
  if (events.empty()) {
    return std::nullopt;
  }
  if (const auto* note = std::get_if<graphscore::Note>(&events.front())) {
    return note->pitch;
  }
  return std::nullopt;
}

[[nodiscard]] bool select_first_notehead(SelectionToolHandler&      handler,
                                         const KeySelectionProject& fixture,
                                         std::size_t track_index) {
  const auto& events =
      voice_of(handler, fixture.node_id, fixture.track_ids[track_index],
               fixture.stave_ids[track_index])
          .events();
  if (events.empty()) {
    return false;
  }
  const auto note_id = graphscore::event_id(events.front());
  return select_noteheads(
      handler, {graphscore::NoteheadItem{
                   fixture.node_id, fixture.track_ids[track_index],
                   fixture.stave_ids[track_index], voice_one(), note_id}});
}

}  // namespace

int action_table_test() {
  // --- test 1: the five disjoint bound chord classes plus the explicit
  //     unbound remainder, asserted for BOTH platform Primary mappings. ----
  {
    struct ChordCase {
      const char*              label;
      graphscore::KeyModifiers modifiers;
      PrimaryModifier          primary;
      ChordClass               expected;
    };

    const std::array<ChordCase, 22> kCases{{
        {"no modifier", mods(false, false, false, false),
         PrimaryModifier::kMeta, ChordClass::kUnmodified},
        {"shift", mods(true, false, false, false), PrimaryModifier::kMeta,
         ChordClass::kShift},
        {"alt", mods(false, false, true, false), PrimaryModifier::kMeta,
         ChordClass::kAlt},
        {"meta (macOS Primary)", mods(false, false, false, true),
         PrimaryModifier::kMeta, ChordClass::kPrimary},
        {"control (macOS non-Primary)", mods(false, true, false, false),
         PrimaryModifier::kMeta, ChordClass::kUnbound},
        {"shift+meta (macOS)", mods(true, false, false, true),
         PrimaryModifier::kMeta, ChordClass::kShiftPrimary},
        {"shift+control (macOS)", mods(true, true, false, false),
         PrimaryModifier::kMeta, ChordClass::kUnbound},
        {"shift+alt", mods(true, false, true, false), PrimaryModifier::kMeta,
         ChordClass::kUnbound},
        {"control+alt+meta", mods(false, true, true, true),
         PrimaryModifier::kMeta, ChordClass::kUnbound},
        {"control (Windows Primary)", mods(false, true, false, false),
         PrimaryModifier::kControl, ChordClass::kPrimary},
        {"meta (Windows non-Primary)", mods(false, false, false, true),
         PrimaryModifier::kControl, ChordClass::kUnbound},
        {"shift+control (Windows)", mods(true, true, false, false),
         PrimaryModifier::kControl, ChordClass::kShiftPrimary},
        {"shift+meta (Windows)", mods(true, false, false, true),
         PrimaryModifier::kControl, ChordClass::kUnbound},
        {"alt+meta (macOS)", mods(false, false, true, true),
         PrimaryModifier::kMeta, ChordClass::kUnbound},
        {"alt+control (Windows)", mods(false, true, true, false),
         PrimaryModifier::kControl, ChordClass::kUnbound},
        {"alt+control (macOS)", mods(false, true, true, false),
         PrimaryModifier::kMeta, ChordClass::kUnbound},
        {"alt+meta (Windows)", mods(false, false, true, true),
         PrimaryModifier::kControl, ChordClass::kUnbound},
        {"shift+alt+meta", mods(true, false, true, true),
         PrimaryModifier::kMeta, ChordClass::kUnbound},
        {"shift+alt+control", mods(true, true, true, false),
         PrimaryModifier::kControl, ChordClass::kUnbound},
        {"all four", mods(true, true, true, true), PrimaryModifier::kMeta,
         ChordClass::kUnbound},
        {"all four (Windows)", mods(true, true, true, true),
         PrimaryModifier::kControl, ChordClass::kUnbound},
        {"control+meta (both non-shift)", mods(false, true, false, true),
         PrimaryModifier::kMeta, ChordClass::kUnbound},
    }};
    for (const ChordCase& test_case : kCases) {
      if (classify_chord(test_case.modifiers, test_case.primary) !=
          test_case.expected) {
        std::fprintf(stderr, "action-table-test: chord class %s\n",
                     test_case.label);
        return 1;
      }
    }
  }

  // --- test 2: the collision-free interval rule (§9): the top-row digits,
  //     the numpad family, and the Alt+digit voice chords are disjoint key
  //     families, so no duration/voice shortcut overlaps the interval keys.
  {
    if (digit_interval(graphscore::KeyCode::kDigit3) !=
        std::optional<std::uint8_t>(3)) {
      std::fprintf(stderr, "action-table-test: top-row 3 is not interval 3\n");
      return 1;
    }
    if (digit_interval(graphscore::KeyCode::kNumPad3).has_value()) {
      std::fprintf(stderr,
                   "action-table-test: numpad 3 collides with interval\n");
      return 1;
    }
    if (numpad_duration(graphscore::KeyCode::kNumPad3) !=
        std::optional<graphscore::NoteValue>(graphscore::NoteValue::kQuarter)) {
      std::fprintf(stderr, "action-table-test: numpad 3 is not quarter\n");
      return 1;
    }
    if (numpad_duration(graphscore::KeyCode::kDigit3).has_value()) {
      std::fprintf(stderr,
                   "action-table-test: top-row 3 collides with duration\n");
      return 1;
    }
    if (top_row_digit(graphscore::KeyCode::kDigit3) !=
        std::optional<std::uint8_t>(3)) {
      std::fprintf(stderr,
                   "action-table-test: top-row digit 3 not recognized\n");
      return 1;
    }
    // `1` stays a no-op interval, never a duration or voice.
    if (digit_interval(graphscore::KeyCode::kDigit1).has_value() ||
        numpad_duration(graphscore::KeyCode::kDigit1).has_value()) {
      std::fprintf(stderr, "action-table-test: digit 1 is not a no-op\n");
      return 1;
    }
  }

  // --- test 3: every repeat-once binding suppresses an auto-repeat press
  //     (§6): `N`, the palette toggle Primary+K (open and close), cut, paste,
  //     and the dot cycle. ------------------------------------------------
  {
    const SelfTestMetrics metrics;
    auto                  project = build_key_selection_project(metrics);
    if (!project.has_value()) {
      std::fprintf(stderr, "action-table-test: fixture failed (3)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(project->project),
                                    std::move(project->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    // N: a repeated press does not toggle the tool.
    shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                            mods(false, false, false, false),
                                            true, graphscore::LogicalKey::kN));
    if (handler.active_tool() != graphscore::ActiveTool::kSelection) {
      std::fprintf(stderr,
                   "action-table-test: repeated N toggled the tool (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Primary+K open: a repeated press does not open the palette.
    shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                            primary_modifiers(), true,
                                            graphscore::LogicalKey::kK));
    if (handler.command_palette_open()) {
      std::fprintf(stderr,
                   "action-table-test: repeated Primary+K opened (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Primary+K close: once the palette is open, a repeated press must not
    // immediately close it (the toggle fires once per physical key-down).
    handler.toggle_command_palette();
    shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                            primary_modifiers(), true,
                                            graphscore::LogicalKey::kK));
    if (!handler.command_palette_open()) {
      std::fprintf(stderr,
                   "action-table-test: repeated Primary+K closed the palette "
                   "(3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.close_command_palette();

    // Cut (repeat-once): a repeated Primary+X must not re-fire the
    // destructive cut. Select a full measure first, so a non-suppressed cut
    // would actually mutate.
    const auto measure_set = graphscore::FullMeasureSet::create(
        {graphscore::FullMeasureItem{project->node_id, project->track_ids[0],
                                     project->stave_ids[0], 0}});
    if (!measure_set.has_value()) {
      std::fprintf(stderr, "action-table-test: measure select failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*measure_set});
    const auto voice_before =
        voice_of(handler, project->node_id, project->track_ids[0],
                 project->stave_ids[0]);
    shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                            primary_modifiers(), true,
                                            graphscore::LogicalKey::kX));
    if (!(voice_of(handler, project->node_id, project->track_ids[0],
                   project->stave_ids[0]) == voice_before) ||
        handler.clipboard_has_fragment()) {
      std::fprintf(stderr, "action-table-test: repeated cut fired (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Paste (repeat-once): a repeated Primary+V with an empty clipboard is
    // already a no-op; prove the repeat is suppressed by showing it posts no
    // diagnostic (the initial press would). Copy first, then repeat-paste.
    handler.set_committed_selection(graphscore::Selection{*measure_set});
    shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                            primary_modifiers(), false,
                                            graphscore::LogicalKey::kC));
    if (!handler.clipboard_has_fragment()) {
      std::fprintf(stderr, "action-table-test: copy setup failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto caret =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            project->node_id, project->track_ids[0], project->stave_ids[0],
            voice_one(), graphscore::Rational(1)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "action-table-test: caret setup failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    const auto diagnostics_before = handler.diagnostics().size();
    shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                            primary_modifiers(), true,
                                            graphscore::LogicalKey::kV));
    if (handler.diagnostics().size() != diagnostics_before) {
      std::fprintf(stderr,
                   "action-table-test: repeated paste posted a diagnostic "
                   "(3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Dot cycle (repeat-once): a repeated KP_DECIMAL must not advance the
    // armed dot count.
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    shell.dispatch_test_key_event(key_event(
        graphscore::KeyCode::kNumPadDecimal, mods(false, false, false, false),
        true, graphscore::LogicalKey::kUnknown));
    if (handler.armed_dots() != 0) {
      std::fprintf(stderr, "action-table-test: repeated dot cycle fired (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: repeat-safe actions re-fire on an auto-repeat press, one
  //     representative per distinct action family/state mutation. Each case
  //     builds a fresh fixture so mutations never bleed between families.
  {
    const SelfTestMetrics metrics;
    // Builds a fresh fixture and handler, runs `body`, and reports whether
    // it passed. The fixture's ids (node/track/stave) remain valid after the
    // project/layout move into the handler.
    const auto run_case = [&](bool (*body)(graphscore::WriterShell&,
                                           SelectionToolHandler&,
                                           const KeySelectionProject&)) {
      auto project = build_key_selection_project(metrics);
      if (!project.has_value()) {
        std::fprintf(stderr, "action-table-test: fixture build failed (4)\n");
        return false;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(project->project),
                                      std::move(project->layout), &shell);
      handler.set_metrics(&metrics);
      shell.set_input_handler(&handler);
      const bool ok = body(shell, handler, *project);
      shell.set_input_handler(nullptr);
      return ok;
    };

    // (a) Notehead move: a repeated Up re-fires the diatonic pitch move.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          if (!select_first_notehead(handler, fixture, 0)) {
            std::fprintf(stderr, "action-table-test: select failed (4a)\n");
            return false;
          }
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kUp, mods(false, false, false, false), true,
              graphscore::LogicalKey::kUnknown));
          if (first_note_pitch(handler, fixture.node_id, fixture.track_ids[0],
                               fixture.stave_ids[0]) !=
              spelled(graphscore::Letter::kD, 4)) {
            std::fprintf(
                stderr, "action-table-test: repeated move did not fire (4a)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (b) Accidental step: a repeated `=` re-fires the accidental ladder.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          if (!select_first_notehead(handler, fixture, 0)) {
            std::fprintf(stderr, "action-table-test: select failed (4b)\n");
            return false;
          }
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kEquals, mods(false, false, false, false),
              true, graphscore::LogicalKey::kUnknown));
          const auto& events =
              voice_of(handler, fixture.node_id, fixture.track_ids[0],
                       fixture.stave_ids[0])
                  .events();
          const auto* note = std::get_if<graphscore::Note>(&events.front());
          if (note == nullptr ||
              note->pitch.accidental() != graphscore::Accidental::kSharp) {
            std::fprintf(
                stderr,
                "action-table-test: repeated accidental did not fire (4b)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (c) Interval add: a repeated `2` re-fires the chord growth.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          if (!select_first_notehead(handler, fixture, 0)) {
            std::fprintf(stderr, "action-table-test: select failed (4c)\n");
            return false;
          }
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kDigit2, mods(false, false, false, false),
              true, graphscore::LogicalKey::kUnknown));
          const auto& events =
              voice_of(handler, fixture.node_id, fixture.track_ids[0],
                       fixture.stave_ids[0])
                  .events();
          if (events.empty() ||
              !std::holds_alternative<graphscore::Chord>(events.front())) {
            std::fprintf(
                stderr,
                "action-table-test: repeated interval did not fire (4c)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (d) Range extension: a repeated Shift+Right re-fires the edge move.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          const auto range = graphscore::ArbitraryRangeSet::create(
              {graphscore::ArbitraryRangeItem{
                  fixture.node_id, fixture.track_ids[0], fixture.stave_ids[0],
                  voice_one(),
                  graphscore::MusicalSpan{graphscore::Rational(0),
                                          graphscore::Rational(1)}}});
          if (!range.has_value()) {
            std::fprintf(stderr,
                         "action-table-test: range build failed (4d)\n");
            return false;
          }
          handler.set_committed_selection(graphscore::Selection{*range});
          const graphscore::MeasureScope scope{fixture.track_ids[0],
                                               fixture.stave_ids[0]};
          std::ignore = handler.extend_range_staff_scope(scope, scope);
          const graphscore::Rational end_before =
              committed_range_set(handler)->items().front().span.end;
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kRight, mods(true, false, false, false),
              true, graphscore::LogicalKey::kUnknown));
          if (committed_range_set(handler)->items().front().span.end ==
              end_before) {
            std::fprintf(
                stderr,
                "action-table-test: repeated range extension did not fire "
                "(4d)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (e) Staff step: a repeated Primary+Down re-fires the staff relocation.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          if (!select_first_notehead(handler, fixture, 0)) {
            std::fprintf(stderr, "action-table-test: select failed (4e)\n");
            return false;
          }
          shell.dispatch_test_key_event(
              key_event(graphscore::KeyCode::kDown, primary_modifiers(), true,
                        graphscore::LogicalKey::kUnknown));
          const auto* set = committed_notehead_set(handler);
          if (set == nullptr || set->items().size() != 1u ||
              set->items().front().track != fixture.track_ids[1]) {
            std::fprintf(
                stderr,
                "action-table-test: repeated staff step did not fire (4e)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (f) Copy: a repeated Primary+C re-fires the extraction.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          const auto measure_set =
              graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
                  fixture.node_id, fixture.track_ids[0], fixture.stave_ids[0],
                  0}});
          if (!measure_set.has_value()) {
            std::fprintf(stderr,
                         "action-table-test: measure select failed (4f)\n");
            return false;
          }
          handler.set_committed_selection(graphscore::Selection{*measure_set});
          shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                                  primary_modifiers(), true,
                                                  graphscore::LogicalKey::kC));
          if (!handler.clipboard_has_fragment()) {
            std::fprintf(
                stderr, "action-table-test: repeated copy did not fire (4f)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (g) Undo: a repeated Primary+Z re-fires the history undo. Mutate first,
    // then undo the mutation with an auto-repeat press.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          if (!select_first_notehead(handler, fixture, 0)) {
            std::fprintf(stderr, "action-table-test: select failed (4g)\n");
            return false;
          }
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kUp, mods(false, false, false, false), false,
              graphscore::LogicalKey::kUnknown));
          const auto pitch_after_move =
              first_note_pitch(handler, fixture.node_id, fixture.track_ids[0],
                               fixture.stave_ids[0]);
          shell.dispatch_test_key_event(key_event(graphscore::KeyCode::kUnknown,
                                                  primary_modifiers(), true,
                                                  graphscore::LogicalKey::kZ));
          if (first_note_pitch(handler, fixture.node_id, fixture.track_ids[0],
                               fixture.stave_ids[0]) == pitch_after_move) {
            std::fprintf(
                stderr, "action-table-test: repeated undo did not fire (4g)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (h) Step-entry commit: a repeated `A` re-fires the commit.
    if (!run_case([](graphscore::WriterShell&   shell,
                     SelectionToolHandler&      handler,
                     const KeySelectionProject& fixture) {
          handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kUnknown, mods(false, false, false, false),
              true, graphscore::LogicalKey::kA));
          if (voice_of(handler, fixture.node_id, fixture.track_ids[0],
                       fixture.stave_ids[0])
                  .events()
                  .empty()) {
            std::fprintf(
                stderr,
                "action-table-test: repeated step commit did not fire (4h)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (i) Voice arming: a repeated Alt+2 re-fires the voice arming.
    if (!run_case([](graphscore::WriterShell& shell,
                     SelectionToolHandler&    handler,
                     const KeySelectionProject& /*fixture*/) {
          handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kDigit2, mods(false, false, true, false),
              true, graphscore::LogicalKey::kUnknown));
          const auto voice2 = graphscore::Voice::create(2);
          if (!voice2.has_value() || handler.armed_voice() != *voice2) {
            std::fprintf(
                stderr,
                "action-table-test: repeated voice arm did not fire (4i)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (j) Duration arming: a repeated KP_1 re-fires the duration arming.
    if (!run_case([](graphscore::WriterShell& shell,
                     SelectionToolHandler&    handler,
                     const KeySelectionProject& /*fixture*/) {
          handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kNumPad1, mods(false, false, false, false),
              true, graphscore::LogicalKey::kUnknown));
          if (handler.armed_note_value() != graphscore::NoteValue::kWhole) {
            std::fprintf(
                stderr,
                "action-table-test: repeated duration arm did not fire (4j)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }

    // (k) Octave step: a repeated Alt+Up re-fires the octave reference.
    if (!run_case([](graphscore::WriterShell& shell,
                     SelectionToolHandler&    handler,
                     const KeySelectionProject& /*fixture*/) {
          handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
          shell.dispatch_test_key_event(key_event(
              graphscore::KeyCode::kUp, mods(false, false, true, false), true,
              graphscore::LogicalKey::kUnknown));
          if (handler.octave_offset() != 1) {
            std::fprintf(
                stderr,
                "action-table-test: repeated octave step did not fire (4k)\n");
            return false;
          }
          return true;
        })) {
      return 1;
    }
  }

  // --- test 5: the explicit no-op remainder (§7.7/§12): top-row 9/0,
  //     Shift+1, Shift+letters/symbols, the non-Primary modifier, mixed
  //     modifiers, Primary+Y, and Return/Escape/Tab/Space in the notation
  //     context all leave the project byte-for-byte unchanged. --------------
  {
    const SelfTestMetrics metrics;
    auto                  project = build_key_selection_project(metrics);
    if (!project.has_value()) {
      std::fprintf(stderr, "action-table-test: fixture failed (5)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(project->project),
                                    std::move(project->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    const auto check_noop = [&](const char* what, graphscore::KeyCode code,
                                graphscore::KeyModifiers modifiers,
                                graphscore::LogicalKey   logical) {
      return no_op_violation(handler, shell, project->node_id,
                             project->track_ids[0], project->stave_ids[0], what,
                             [&] {
                               shell.dispatch_test_key_event(
                                   key_event(code, modifiers, false, logical));
                             });
    };

    // Top-row 9/0 (unmodified) are unbound no-ops.
    for (const auto code :
         {graphscore::KeyCode::kDigit9, graphscore::KeyCode::kDigit0}) {
      const std::string violation =
          check_noop("top-row 9/0", code, mods(false, false, false, false),
                     graphscore::LogicalKey::kUnknown);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Shift+1 is a no-op.
    {
      const std::string violation = check_noop(
          "Shift+1", graphscore::KeyCode::kDigit1,
          mods(true, false, false, false), graphscore::LogicalKey::kUnknown);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Shift+letter (R) and Shift+symbol (- and =) are no-ops.
    {
      const std::string violation = check_noop(
          "Shift+R", graphscore::KeyCode::kR, mods(true, false, false, false),
          graphscore::LogicalKey::kR);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    for (const auto code :
         {graphscore::KeyCode::kMinus, graphscore::KeyCode::kEquals}) {
      const std::string violation =
          check_noop("Shift+symbol", code, mods(true, false, false, false),
                     graphscore::LogicalKey::kUnknown);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // The non-Primary modifier carries no binding: a chord using it with a
    // bound letter (X, the cut mnemonic) is a no-op on this host's mapping,
    // even when a valid full-measure selection is committed (a mis-routed
    // cut would clear it).
    {
      const auto measure_set = graphscore::FullMeasureSet::create(
          {graphscore::FullMeasureItem{project->node_id, project->track_ids[0],
                                       project->stave_ids[0], 0}});
      if (!measure_set.has_value()) {
        std::fprintf(stderr, "action-table-test: measure select failed (5)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*measure_set});
      const std::string violation =
          check_noop("non-Primary+X", graphscore::KeyCode::kUnknown,
                     non_primary_modifiers(), graphscore::LogicalKey::kX);
      if (!violation.empty() || handler.clipboard_has_fragment()) {
        std::fprintf(stderr, "action-table-test: non-Primary+X fired (5)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Mixed modifiers (Shift+Alt) are no-ops.
    {
      const std::string violation = check_noop(
          "Shift+Alt+Left", graphscore::KeyCode::kLeft,
          mods(true, false, true, false), graphscore::LogicalKey::kUnknown);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Primary+Alt is also a mixed-modifier no-op.
    {
      graphscore::KeyModifiers primary_alt = primary_modifiers();
      primary_alt.alt                      = true;
      const std::string violation =
          check_noop("Primary+Alt+Left", graphscore::KeyCode::kLeft,
                     primary_alt, graphscore::LogicalKey::kUnknown);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Primary+Y is deliberately unbound (a no-op, §7.4): `y` is not a
    // recognized logical letter, so Primary+Y falls through to nothing.
    {
      const std::string violation =
          check_noop("Primary+Y", graphscore::KeyCode::kUnknown,
                     primary_modifiers(), graphscore::LogicalKey::kUnknown);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Return/Escape/Tab/Space in the notation context are no-ops (Escape
    // dismisses the palette only in the palette context, never here).
    for (const auto code :
         {graphscore::KeyCode::kReturn, graphscore::KeyCode::kEscape,
          graphscore::KeyCode::kTab, graphscore::KeyCode::kSpace}) {
      const std::string violation =
          check_noop("editing key", code, mods(false, false, false, false),
                     graphscore::LogicalKey::kUnknown);
      if (!violation.empty()) {
        std::fprintf(stderr, "action-table-test: %s (5)\n", violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.set_input_handler(nullptr);
  }

  std::printf("action-table-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
