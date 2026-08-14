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
// Exercises WriterShell::dispatch_sdl_test_key_event feeding the production
// SDL physical-scancode and modifier-mask translation into the actual
// SelectionToolHandler (M5-phase-25): unmodified SDL digit 2 adds above, SDL
// Shift+3 adds below, and the SDL forbidden chord plus SDL digit 1 are
// no-ops -- the production SDL translation path, not merely the headless
// neutral-KeyEvent seam interval_entry_test() exercises. This is the shell
// twin of that test's digit coverage; like key_events_shell_test() it never
// calls open_window(), so no window, renderer, or SDL_Init is needed and it
// runs unconditionally on a headless host. It requires
// GRAPHSCORE_BUILD_WRITER: dispatch_sdl_test_key_event is a no-op in a
// writer-OFF build, so the CTest registration gates this test behind
// GRAPHSCORE_BUILD_WRITER rather than executing no-op assertions there.
int interval_entry_shell_test() {
  const SelfTestMetrics metrics;

  // SDL physical scancodes for the top-row digits (SDL3/SDL_scancode.h at the
  // pinned commit): 1=30 through 8=37. M5-phase-25 binds only 2..8.
  constexpr std::uint32_t kScancodeDigit1 = 30;
  constexpr std::uint32_t kScancodeDigit2 = 31;
  constexpr std::uint32_t kScancodeDigit3 = 32;

  // SDL_Keymod bitmasks (SDL3/SDL_keycode.h at the pinned commit):
  // SHIFT=0x0003, CTRL=0x00C0, ALT=0x0300, GUI=0x0C00.
  constexpr std::uint16_t kModShift = 0x0003;
  constexpr std::uint16_t kModCtrl  = 0x00C0;

  // --- test 8: SDL physical-scancode digit events drive the actual
  //     SelectionToolHandler to mutation and no-op -- the production SDL
  //     translation path, not merely the recording handler's translation. --
  {
    // Unmodified SDL digit 2 -> above: C4 -> {C4, D4}.
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr,
                   "interval-entry-shell-test: fixture build failed (8a)\n");
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
                   "interval-entry-shell-test: initial publish failed (8a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr,
                   "interval-entry-shell-test: selection rejected (8a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_sdl_test_key_event(kScancodeDigit2, 0);
    const std::optional<graphscore::Chord> chord =
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id);
    if (!chord.has_value() || chord->notes.size() != 2u ||
        chord->notes[0].id != fx->source_id ||
        chord->notes[1].pitch != spelled(graphscore::Letter::kD, 4)) {
      std::fprintf(
          stderr,
          "interval-entry-shell-test: SDL digit 2 did not add D4 (8a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "interval-entry-shell-test: SDL digit 2 no history (8a)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }
  {
    // SDL Shift+3 -> below: C4 -> {C4, A3}.
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr,
                   "interval-entry-shell-test: fixture build failed (8b)\n");
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
                   "interval-entry-shell-test: initial publish failed (8b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr,
                   "interval-entry-shell-test: selection rejected (8b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_sdl_test_key_event(kScancodeDigit3, kModShift);
    const std::optional<graphscore::Chord> chord =
        first_chord(handler.project(), fx->node_id, fx->track_id, fx->stave_id);
    if (!chord.has_value() || chord->notes.size() != 2u ||
        chord->notes[1].pitch != spelled(graphscore::Letter::kA, 3)) {
      std::fprintf(
          stderr,
          "interval-entry-shell-test: SDL Shift+3 did not add A3 (8b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }
  {
    // SDL forbidden chord (Shift+Ctrl+digit 2) -> no-op.
    auto fx = build_interval_note_fixture(metrics, 0);
    if (!fx.has_value()) {
      std::fprintf(stderr,
                   "interval-entry-shell-test: fixture build failed (8c)\n");
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
                   "interval-entry-shell-test: initial publish failed (8c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice_one(), fx->source_id}})) {
      std::fprintf(stderr,
                   "interval-entry-shell-test: selection rejected (8c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::string violation =
        no_op_violation(handler, shell, fx->node_id, fx->track_id, fx->stave_id,
                        "sdl forbidden chord", [&] {
                          shell.dispatch_sdl_test_key_event(
                              kScancodeDigit2,
                              static_cast<std::uint16_t>(kModShift | kModCtrl));
                        });
    if (!violation.empty()) {
      std::fprintf(stderr, "interval-entry-shell-test: %s (8c)\n",
                   violation.c_str());
      shell.set_input_handler(nullptr);
      return 1;
    }
    // SDL digit 1 is an unbound no-op, exactly like its neutral-KeyEvent twin.
    const std::string digit1_violation = no_op_violation(
        handler, shell, fx->node_id, fx->track_id, fx->stave_id, "sdl `1`",
        [&] { shell.dispatch_sdl_test_key_event(kScancodeDigit1, 0); });
    if (!digit1_violation.empty()) {
      std::fprintf(stderr, "interval-entry-shell-test: %s (8c)\n",
                   digit1_violation.c_str());
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("interval-entry-shell-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
