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
// ---- M5-phase-25: key-spelled diatonic interval entry ----------------------

int interval_entry_test() {
  const SelfTestMetrics metrics;

  // Every unmodified digit 2..8 adds a diatonic interval ABOVE C4 in C major;
  // the same digit with an exact Shift adds it BELOW. Table-driven so each
  // digit is dispatched and each below-direction interval (4..7 included) is
  // asserted, rather than spot-checking a few.
  struct DigitCase {
    graphscore::KeyCode code;
    graphscore::Letter  above_letter;
    std::int8_t         above_octave;
    graphscore::Letter  below_letter;
    std::int8_t         below_octave;
  };

  constexpr std::array<DigitCase, 7> kDigits{{
      {graphscore::KeyCode::kDigit2, graphscore::Letter::kD, 4,
       graphscore::Letter::kB, 3},
      {graphscore::KeyCode::kDigit3, graphscore::Letter::kE, 4,
       graphscore::Letter::kA, 3},
      {graphscore::KeyCode::kDigit4, graphscore::Letter::kF, 4,
       graphscore::Letter::kG, 3},
      {graphscore::KeyCode::kDigit5, graphscore::Letter::kG, 4,
       graphscore::Letter::kF, 3},
      {graphscore::KeyCode::kDigit6, graphscore::Letter::kA, 4,
       graphscore::Letter::kE, 3},
      {graphscore::KeyCode::kDigit7, graphscore::Letter::kB, 4,
       graphscore::Letter::kD, 3},
      {graphscore::KeyCode::kDigit8, graphscore::Letter::kC, 5,
       graphscore::Letter::kC, 3},
  }};

  // Every forbidden modifier mask a digit must ignore: anything that is not an
  // exact unmodified chord (above) or an exact Shift-only chord (below). This
  // enumerates the Shift+Alt, Shift+Meta, Ctrl+Alt, Ctrl+Meta, Alt+Meta, and
  // larger superset combinations the review called out.
  constexpr std::array<graphscore::KeyModifiers, 14> kForbiddenModifiers{{
      {false, true, false, false},  // control
      {false, false, true, false},  // alt
      {false, false, false, true},  // meta
      {true, true, false, false},   // shift+control
      {true, false, true, false},   // shift+alt
      {true, false, false, true},   // shift+meta
      {false, true, true, false},   // control+alt
      {false, true, false, true},   // control+meta
      {false, false, true, true},   // alt+meta
      {true, true, true, false},    // shift+control+alt
      {true, true, false, true},    // shift+control+meta
      {true, false, true, true},    // shift+alt+meta
      {false, true, true, true},    // control+alt+meta
      {true, true, true, true},     // shift+control+alt+meta
  }};

  const auto key_with = [](graphscore::KeyCode      code,
                           graphscore::KeyModifiers modifiers) {
    graphscore::KeyEvent event;
    event.code      = code;
    event.modifiers = modifiers;
    return event;
  };

  // --- test 1: every unmodified `2`..`8` adds the correct diatonic interval
  //     ABOVE C4 (digit `8` is the octave); the inserted notehead becomes
  //     selected; the surface is re-published; the resulting chord auditions.
  for (const DigitCase& digit : kDigits) {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (1)\n");
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
      std::fprintf(stderr, "interval-entry-test: initial publish failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->source_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->source_id) {
        std::fprintf(
            stderr, "interval-entry-test: click did not select the note (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    const auto before_surface = shell.test_snapshot_notation_surface();
    shell.dispatch_test_key_event(plain_key(digit.code));

    const std::optional<graphscore::Chord> chord =
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id);
    if (!chord.has_value() || chord->notes.size() != 2u ||
        chord->notes[0].id != fx->source_id ||
        chord->notes[0].pitch != spelled(graphscore::Letter::kC, 4) ||
        chord->notes[1].pitch !=
            spelled(digit.above_letter, digit.above_octave)) {
      std::fprintf(stderr,
                   "interval-entry-test: digit above target wrong (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != chord->notes[1].id) {
        std::fprintf(
            stderr,
            "interval-entry-test: inserted notehead not selected (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr, "interval-entry-test: no history entry (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Visible surface re-published: different raster bytes than before.
    const auto after_surface = shell.test_snapshot_notation_surface();
    if (!after_surface.has_value() || after_surface == before_surface) {
      std::fprintf(stderr,
                   "interval-entry-test: surface not re-published (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Audition: the source C4 (MIDI 60) plus the new pitch, ascending.
    {
      const auto& audition = handler.last_audition();
      const std::optional<graphscore::MidiPitch> target_midi =
          spelled(digit.above_letter, digit.above_octave).to_midi_pitch();
      if (!audition.has_value() || !target_midi.has_value() ||
          audition->pitches.size() != 2u ||
          audition->pitches[0].value() != 60 ||
          audition->pitches[1].value() != target_midi->value()) {
        std::fprintf(stderr, "interval-entry-test: wrong audition (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- `1` is a complete no-op: project/voice content, committed selection,
  //     layout, surface, highlight, history depth, and the audition hook are
  //     all byte-for-byte unchanged. ---------------------------------------
  {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (`1`)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "interval-entry-test: initial publish failed (`1`)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (`1`)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::string violation = no_op_violation(
        handler, shell, fx->node_id, fx->track_id, fx->stave_id, "`1`", [&] {
          shell.dispatch_test_key_event(
              plain_key(graphscore::KeyCode::kDigit1));
        });
    if (!violation.empty()) {
      std::fprintf(stderr, "interval-entry-test: %s (`1`)\n",
                   violation.c_str());
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- A repeated interval that would duplicate an existing spelled pitch is
  //     an atomic no-op: full state preserved and the chord stays two notes.
  {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (dup)\n");
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
      std::fprintf(stderr,
                   "interval-entry-test: initial publish failed (dup)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (dup)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    // Re-select the original source C4: `2` above it duplicates D4.
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr,
                   "interval-entry-test: re-select C4 rejected (dup)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::string violation =
        no_op_violation(handler, shell, fx->node_id, fx->track_id, fx->stave_id,
                        "duplicate interval", [&] {
                          shell.dispatch_test_key_event(
                              plain_key(graphscore::KeyCode::kDigit2));
                        });
    if (!violation.empty()) {
      std::fprintf(stderr, "interval-entry-test: %s (dup)\n",
                   violation.c_str());
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::optional<graphscore::Chord> chord =
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id);
    if (!chord.has_value() || chord->notes.size() != 2u) {
      std::fprintf(stderr,
                   "interval-entry-test: duplicate changed the chord\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- Undo/redo round-trips a successful interval through the handler's
  //     history. -----------------------------------------------------------
  {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr,
                   "interval-entry-test: fixture build failed (undo)\n");
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
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (undo)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    if (!first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id)
             .has_value()) {
      std::fprintf(stderr, "interval-entry-test: no chord to undo (undo)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.test_undo() || handler.test_undo_stack_size() != 0u ||
        handler.test_redo_stack_size() != 1u) {
      std::fprintf(stderr, "interval-entry-test: undo failed (undo)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id)
            .has_value()) {
      std::fprintf(stderr, "interval-entry-test: undo kept the chord (undo)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.test_redo() || handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr, "interval-entry-test: redo failed (undo)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id)
             .has_value()) {
      std::fprintf(stderr, "interval-entry-test: redo lost the chord (undo)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 2: every exact Shift-only `2`..`8` adds the correct diatonic
  //     interval BELOW C4, and the inserted notehead becomes selected. -----
  for (const DigitCase& digit : kDigits) {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (2)\n");
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
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(shift_key(digit.code));
    const std::optional<graphscore::Chord> chord =
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id);
    if (!chord.has_value() || chord->notes.size() != 2u ||
        chord->notes[0].id != fx->source_id ||
        chord->notes[0].pitch != spelled(graphscore::Letter::kC, 4) ||
        chord->notes[1].pitch !=
            spelled(digit.below_letter, digit.below_octave)) {
      std::fprintf(stderr,
                   "interval-entry-test: digit below target wrong (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != chord->notes[1].id) {
        std::fprintf(stderr,
                     "interval-entry-test: below insert not selected (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3a: `8` actually dispatches the octave through the app (the
  //     prior block claimed `8` but pressed `2`): `8` above C4 is C5. ------
  {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (3a)\n");
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
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (3a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit8));
    const std::optional<graphscore::Chord> octave =
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id);
    if (!octave.has_value() || octave->notes.size() != 2u ||
        octave->notes[1].pitch != spelled(graphscore::Letter::kC, 5)) {
      std::fprintf(stderr,
                   "interval-entry-test: `8` did not add the octave (3a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3b: key-signature spelling reaches the app: in E major
  //     (4 sharps), `2` above C4 is D#4. ----------------------------------
  {
    auto fx = build_interval_note_fixture(metrics, 4);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (3b)\n");
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
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (3b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    const std::optional<graphscore::Chord> chord =
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id);
    if (!chord.has_value() || chord->notes.size() != 2u ||
        chord->notes[1].pitch != spelled(graphscore::Letter::kD, 4,
                                         graphscore::Accidental::kSharp)) {
      std::fprintf(stderr,
                   "interval-entry-test: key signature not spelled (3b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: a selected ChordNote grows its chord; every existing
  //     notehead is preserved. --------------------------------------------
  {
    auto fx = build_interval_chord_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (4)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->source_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->source_id) {
        std::fprintf(stderr,
                     "interval-entry-test: chord notehead click failed (4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit4));
    const auto chord = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice_one());
      return std::get<graphscore::Chord>(vc.events().front());
    }();
    if (chord.notes.size() != 3u || chord.notes[0].id != fx->source_id ||
        chord.notes[0].pitch != spelled(graphscore::Letter::kC, 4) ||
        chord.notes[1].pitch != spelled(graphscore::Letter::kE, 4) ||
        chord.notes[2].pitch != spelled(graphscore::Letter::kF, 4)) {
      std::fprintf(stderr, "interval-entry-test: chord growth wrong (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != chord.notes[2].id) {
        std::fprintf(stderr,
                     "interval-entry-test: grown notehead not selected (4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: every rejection/no-op family leaves the complete observable
  //     state unchanged: no selection, a stale identity, a range selection,
  //     and every forbidden modifier mask on a valid notehead selection. ----
  {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (5)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();

    // No selection.
    {
      const std::string violation =
          no_op_violation(handler, shell, fx->node_id, fx->track_id,
                          fx->stave_id, "no selection", [&] {
                            shell.dispatch_test_key_event(
                                plain_key(graphscore::KeyCode::kDigit2));
                          });
      if (!violation.empty()) {
        std::fprintf(stderr, "interval-entry-test: %s (5)\n",
                     violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Stale identity.
    if (!select_noteheads(
            handler, {graphscore::NoteheadItem{
                         fx->node_id, fx->track_id, fx->stave_id, voice_one(),
                         graphscore::NotationEntityId::generate()}})) {
      std::fprintf(stderr,
                   "interval-entry-test: stale selection rejected (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const std::string violation =
          no_op_violation(handler, shell, fx->node_id, fx->track_id,
                          fx->stave_id, "stale identity", [&] {
                            shell.dispatch_test_key_event(
                                plain_key(graphscore::KeyCode::kDigit2));
                          });
      if (!violation.empty()) {
        std::fprintf(stderr, "interval-entry-test: %s (5)\n",
                     violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Range selection: unmodified and Shift digits stay no-ops.
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    if (!handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr, "interval-entry-test: range setup failed (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const std::string violation =
          no_op_violation(handler, shell, fx->node_id, fx->track_id,
                          fx->stave_id, "range unmodified", [&] {
                            shell.dispatch_test_key_event(
                                plain_key(graphscore::KeyCode::kDigit2));
                          });
      if (!violation.empty()) {
        std::fprintf(stderr, "interval-entry-test: %s (5)\n",
                     violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    {
      const std::string violation =
          no_op_violation(handler, shell, fx->node_id, fx->track_id,
                          fx->stave_id, "range shift", [&] {
                            shell.dispatch_test_key_event(
                                shift_key(graphscore::KeyCode::kDigit2));
                          });
      if (!violation.empty()) {
        std::fprintf(stderr, "interval-entry-test: %s (5)\n",
                     violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Forbidden modifier masks on a valid single notehead selection: every
    // combination that is not exact unmodified (above) or exact Shift-only
    // (below) must be a no-op.
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    for (const graphscore::KeyModifiers mods : kForbiddenModifiers) {
      const std::string violation =
          no_op_violation(handler, shell, fx->node_id, fx->track_id,
                          fx->stave_id, "forbidden modifier mask", [&] {
                            shell.dispatch_test_key_event(
                                key_with(graphscore::KeyCode::kDigit2, mods));
                          });
      if (!violation.empty()) {
        std::fprintf(stderr, "interval-entry-test: %s (5)\n",
                     violation.c_str());
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 6: a failing surface publisher rolls the interval back
  //     completely (project, layout, surface, selection, history, audition),
  //     and the next interval succeeds. ------------------------------------
  {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (6)\n");
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

    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr, "interval-entry-test: initial publish failed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto before_surface   = shell.test_snapshot_notation_surface();
    const auto before_highlight = shell.test_snapshot_highlight_rects();
    const auto before_layout    = handler.layout();
    const auto before_selection = handler.drag_state().committed_selection();

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    const auto first_event_is_note = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice_one());
      return std::holds_alternative<graphscore::Note>(vc.events().front());
    };
    if (!first_event_is_note() || handler.layout() != before_layout ||
        shell.test_snapshot_notation_surface() != before_surface ||
        shell.test_snapshot_highlight_rects() != before_highlight ||
        handler.drag_state().committed_selection() != before_selection ||
        handler.test_undo_stack_size() != 0u ||
        handler.test_redo_stack_size() != 0u) {
      std::fprintf(stderr,
                   "interval-entry-test: rollback left state changed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.history_unavailable()) {
      std::fprintf(stderr,
                   "interval-entry-test: rollback poisoned history (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // The next interval (publisher now healthy) succeeds.
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    if (first_event_is_note() || handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "interval-entry-test: retry after rollback failed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: a failed surface refresh preserves an existing redo stack and
  //     leaves the history unpoisoned; the preserved redo stays executable. -
  {
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr, "interval-entry-test: fixture build failed (7)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
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
    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr, "interval-entry-test: initial publish failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: selection rejected (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Build a redo stack: the interval succeeds, then is undone.
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    if (!first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id)
             .has_value() ||
        handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr, "interval-entry-test: redo setup failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.test_undo() || handler.test_undo_stack_size() != 0u ||
        handler.test_redo_stack_size() != 1u) {
      std::fprintf(stderr, "interval-entry-test: undo for redo failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Re-select the source notehead (undo left the selection pointing at the
    // now-removed inserted notehead), then attempt an interval whose surface
    // publication fails.
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr, "interval-entry-test: re-select rejected (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    fail_next_publish                           = true;
    const graphscore::VoiceContent before_voice = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      return lane->stave(fx->stave_id)->voice(voice_one());
    }();
    const auto before_layout    = handler.layout();
    const auto before_selection = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));

    // The injected publication failure must actually have been exercised, or
    // the "rollback" below would prove nothing: the one-shot flag is consumed
    // by the failing publisher, and the abort restores the complete
    // pre-dispatch voice content (not merely "still a Note"), while the
    // layout/selection and the existing redo stack survive intact.
    const graphscore::VoiceContent after_voice = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      return lane->stave(fx->stave_id)->voice(voice_one());
    }();
    if (fail_next_publish || !(after_voice == before_voice) ||
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id)
            .has_value() ||
        handler.layout() != before_layout ||
        handler.drag_state().committed_selection() != before_selection ||
        handler.test_undo_stack_size() != 0u ||
        handler.test_redo_stack_size() != 1u || handler.history_unavailable()) {
      std::fprintf(stderr,
                   "interval-entry-test: redo not preserved through failed "
                   "refresh (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // The preserved redo is still executable: it re-applies the first chord.
    if (!handler.test_redo() || handler.test_undo_stack_size() != 1u ||
        handler.test_redo_stack_size() != 0u ||
        !first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id)
             .has_value()) {
      std::fprintf(stderr,
                   "interval-entry-test: preserved redo not executable (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("interval-entry-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
