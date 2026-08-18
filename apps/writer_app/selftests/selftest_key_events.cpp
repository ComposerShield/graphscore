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
// ---- key-event delivery tests (M5-phase-19b-ii) ----------------------------
//
// This sub-phase delivers platform-neutral key events from SDL (or a test
// seam) to a registered InputHandler; it does not interpret them. These
// tests only assert that a key event arrives at the handler unchanged
// (headless seam) or correctly translated from real SDL scancode/modifier
// values (production SDL path). Interpreting keys into selection changes is
// M5-phase-19b-iii and is out of scope here.

namespace {
// Records every KeyEvent delivered to on_key_press() and every composed
// TextInputEvent delivered to on_text_input(); the pointer methods and
// on_cancel() are no-ops because this handler exists only to observe key and
// text delivery. Deliberately not SelectionToolHandler — that handler is
// untouched this sub-phase and inherits the default no-op on_key_press.
class RecordingKeyHandler final : public graphscore::InputHandler {
 public:
  void on_pointer_press(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_move(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_release(graphscore::PointerEvent /*event*/) override {}

  void on_cancel() override {}

  void on_key_press(graphscore::KeyEvent event) override {
    events.push_back(event);
  }

  void on_text_input(graphscore::TextInputEvent event) override {
    text_events.push_back(std::move(event));
  }

  std::vector<graphscore::KeyEvent>       events;
  std::vector<graphscore::TextInputEvent> text_events;
};

// Implements only the four pure-virtual pointer/cancel methods and does not
// override on_key_press, so it exercises InputHandler's default no-op
// implementation rather than shadowing it.
class DefaultKeyHandler final : public graphscore::InputHandler {
 public:
  void on_pointer_press(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_move(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_release(graphscore::PointerEvent /*event*/) override {}

  void on_cancel() override {}
};
}  // namespace

// Headless: exercises WriterShell::dispatch_test_key_event only. Works
// identically in writer-ON and writer-OFF builds (no SDL, no window).
int key_events_test() {
  graphscore::WriterShell shell;

  // --- test: no handler registered -> dispatch is a silent no-op --------
  {
    graphscore::KeyEvent event;
    event.code = graphscore::KeyCode::kLeft;
    shell.dispatch_test_key_event(event);
  }

  RecordingKeyHandler handler;
  shell.set_input_handler(&handler);

  // --- test: every KeyCode value round-trips unchanged -------------------
  constexpr std::array<graphscore::KeyCode, 35> kAllCodes{
      graphscore::KeyCode::kUnknown,       graphscore::KeyCode::kLeft,
      graphscore::KeyCode::kRight,         graphscore::KeyCode::kUp,
      graphscore::KeyCode::kDown,          graphscore::KeyCode::kHome,
      graphscore::KeyCode::kEnd,           graphscore::KeyCode::kMinus,
      graphscore::KeyCode::kEquals,        graphscore::KeyCode::kBackspace,
      graphscore::KeyCode::kDelete,        graphscore::KeyCode::kR,
      graphscore::KeyCode::kDigit1,        graphscore::KeyCode::kDigit2,
      graphscore::KeyCode::kDigit3,        graphscore::KeyCode::kDigit4,
      graphscore::KeyCode::kDigit5,        graphscore::KeyCode::kDigit6,
      graphscore::KeyCode::kDigit7,        graphscore::KeyCode::kDigit8,
      graphscore::KeyCode::kDigit9,        graphscore::KeyCode::kDigit0,
      graphscore::KeyCode::kReturn,        graphscore::KeyCode::kEscape,
      graphscore::KeyCode::kTab,           graphscore::KeyCode::kSpace,
      graphscore::KeyCode::kNumPad1,       graphscore::KeyCode::kNumPad2,
      graphscore::KeyCode::kNumPad3,       graphscore::KeyCode::kNumPad4,
      graphscore::KeyCode::kNumPad5,       graphscore::KeyCode::kNumPad6,
      graphscore::KeyCode::kNumPad7,       graphscore::KeyCode::kNumPad0,
      graphscore::KeyCode::kNumPadDecimal,
  };
  for (const graphscore::KeyCode code : kAllCodes) {
    const std::size_t    before = handler.events.size();
    graphscore::KeyEvent event;
    event.code = code;
    shell.dispatch_test_key_event(event);
    if (handler.events.size() != before + 1 ||
        handler.events.back().code != code) {
      std::fprintf(stderr,
                   "key-events-test: KeyCode %d did not round-trip through "
                   "the headless seam\n",
                   static_cast<int>(code));
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test: all four modifier flags round-trip, individually and
  //     in combination --------------------------------------------------
  const std::array<graphscore::KeyModifiers, 5> kModifierCases{{
      {true, false, false, false},
      {false, true, false, false},
      {false, false, true, false},
      {false, false, false, true},
      {true, true, true, true},
  }};
  for (const graphscore::KeyModifiers& modifiers : kModifierCases) {
    const std::size_t    before = handler.events.size();
    graphscore::KeyEvent event;
    event.code      = graphscore::KeyCode::kLeft;
    event.modifiers = modifiers;
    shell.dispatch_test_key_event(event);
    if (handler.events.size() != before + 1 ||
        handler.events.back().modifiers != modifiers) {
      std::fprintf(stderr,
                   "key-events-test: modifiers did not round-trip through "
                   "the headless seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test: after set_input_handler(nullptr), no further callbacks -----
  shell.set_input_handler(nullptr);
  {
    const std::size_t    before = handler.events.size();
    graphscore::KeyEvent event;
    event.code = graphscore::KeyCode::kEnd;
    shell.dispatch_test_key_event(event);
    if (handler.events.size() != before) {
      std::fprintf(stderr,
                   "key-events-test: handler received a key event after "
                   "unregistration\n");
      return 1;
    }
  }

  // --- test: a handler that does not override on_key_press (the default
  //     no-op) receives a key press without crashing --------------------
  {
    DefaultKeyHandler default_handler;
    shell.set_input_handler(&default_handler);
    graphscore::KeyEvent event;
    event.code = graphscore::KeyCode::kHome;
    shell.dispatch_test_key_event(event);
    shell.set_input_handler(nullptr);
  }

  // --- test: composed text input round-trips through the headless seam,
  //     including multi-character and non-US UTF-8 text ------------------
  {
    RecordingKeyHandler text_handler;
    shell.set_input_handler(&text_handler);
    const std::array<std::string_view, 3> kTexts{"whole", "staff", "\xC3\xA9"};
    for (const std::string_view text : kTexts) {
      const std::size_t before = text_handler.text_events.size();
      shell.dispatch_test_text_input(
          graphscore::TextInputEvent{std::string(text)});
      if (text_handler.text_events.size() != before + 1 ||
          text_handler.text_events.back().text != text) {
        std::fprintf(stderr,
                     "key-events-test: text input did not round-trip through "
                     "the headless seam\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("key-events-test: ok\n");
  return 0;
}

// Exercises WriterShell::dispatch_sdl_test_key_event, the production SDL
// physical-scancode and modifier-mask translation path. Deliberately does
// not call open_window(): dispatch_sdl_test_key_event builds an
// SDL_EVENT_KEY_DOWN and routes it through the production
// dispatch_sdl_event, which only touches the renderer for
// SDL_ConvertEventToRenderCoordinates — a no-op for key events, and
// unreached altogether when impl_'s renderer is null, which it always is
// without open_window(). No window, renderer, or SDL_Init is needed, so
// this test runs unconditionally rather than skipping on a headless host.
int key_events_shell_test() {
  RecordingKeyHandler     handler;
  graphscore::WriterShell shell;
  shell.set_input_handler(&handler);

  // Raw SDL_Scancode values, verified against the fetched SDL3 headers
  // (SDL3/SDL_scancode.h at the pinned commit): LEFT=80, RIGHT=79, UP=82,
  // DOWN=81, HOME=74, END=77, MINUS=45, EQUALS=46, BACKSPACE=42, DELETE=76,
  // R=21 (the USB HID keyboard usage table's row for the letter R), and the
  // top-row digits 1..8 = 30..37. SDL_SCANCODE_A=4 is a mapped character-key
  // scancode outside this minimal set and must translate to kUnknown. The
  // top-row digits 9=38 and 0=39, the editing keys RETURN=40/ESCAPE=41/
  // TAB=43/SPACE=44, and the numpad KP_1..KP_7=89..95/KP_0=98/KP_PERIOD=99
  // are now recognized physical keys (M5-phase-27's action table names them).
  constexpr std::uint32_t kScancodeLeft      = 80;
  constexpr std::uint32_t kScancodeRight     = 79;
  constexpr std::uint32_t kScancodeUp        = 82;
  constexpr std::uint32_t kScancodeDown      = 81;
  constexpr std::uint32_t kScancodeHome      = 74;
  constexpr std::uint32_t kScancodeEnd       = 77;
  constexpr std::uint32_t kScancodeMinus     = 45;
  constexpr std::uint32_t kScancodeEquals    = 46;
  constexpr std::uint32_t kScancodeBackspace = 42;
  constexpr std::uint32_t kScancodeDelete    = 76;
  constexpr std::uint32_t kScancodeR         = 21;
  constexpr std::uint32_t kScancodeDigit1    = 30;
  constexpr std::uint32_t kScancodeDigit2    = 31;
  constexpr std::uint32_t kScancodeDigit3    = 32;
  constexpr std::uint32_t kScancodeDigit4    = 33;
  constexpr std::uint32_t kScancodeDigit5    = 34;
  constexpr std::uint32_t kScancodeDigit6    = 35;
  constexpr std::uint32_t kScancodeDigit7    = 36;
  constexpr std::uint32_t kScancodeDigit8    = 37;
  constexpr std::uint32_t kScancodeDigit9    = 38;
  constexpr std::uint32_t kScancodeDigit0    = 39;
  constexpr std::uint32_t kScancodeReturn    = 40;
  constexpr std::uint32_t kScancodeEscape    = 41;
  constexpr std::uint32_t kScancodeTab       = 43;
  constexpr std::uint32_t kScancodeSpace     = 44;
  constexpr std::uint32_t kScancodeKp1       = 89;
  constexpr std::uint32_t kScancodeKp2       = 90;
  constexpr std::uint32_t kScancodeKp3       = 91;
  constexpr std::uint32_t kScancodeKp4       = 92;
  constexpr std::uint32_t kScancodeKp5       = 93;
  constexpr std::uint32_t kScancodeKp6       = 94;
  constexpr std::uint32_t kScancodeKp7       = 95;
  constexpr std::uint32_t kScancodeKp0       = 98;
  constexpr std::uint32_t kScancodeKpPeriod  = 99;
  constexpr std::uint32_t kScancodeA         = 4;

  struct ScancodeCase {
    std::uint32_t       scancode;
    graphscore::KeyCode expected;
  };

  constexpr std::array<ScancodeCase, 34> kMappedScancodes{{
      {kScancodeLeft, graphscore::KeyCode::kLeft},
      {kScancodeRight, graphscore::KeyCode::kRight},
      {kScancodeUp, graphscore::KeyCode::kUp},
      {kScancodeDown, graphscore::KeyCode::kDown},
      {kScancodeHome, graphscore::KeyCode::kHome},
      {kScancodeEnd, graphscore::KeyCode::kEnd},
      {kScancodeMinus, graphscore::KeyCode::kMinus},
      {kScancodeEquals, graphscore::KeyCode::kEquals},
      {kScancodeBackspace, graphscore::KeyCode::kBackspace},
      {kScancodeDelete, graphscore::KeyCode::kDelete},
      {kScancodeR, graphscore::KeyCode::kR},
      {kScancodeDigit1, graphscore::KeyCode::kDigit1},
      {kScancodeDigit2, graphscore::KeyCode::kDigit2},
      {kScancodeDigit3, graphscore::KeyCode::kDigit3},
      {kScancodeDigit4, graphscore::KeyCode::kDigit4},
      {kScancodeDigit5, graphscore::KeyCode::kDigit5},
      {kScancodeDigit6, graphscore::KeyCode::kDigit6},
      {kScancodeDigit7, graphscore::KeyCode::kDigit7},
      {kScancodeDigit8, graphscore::KeyCode::kDigit8},
      {kScancodeDigit9, graphscore::KeyCode::kDigit9},
      {kScancodeDigit0, graphscore::KeyCode::kDigit0},
      {kScancodeReturn, graphscore::KeyCode::kReturn},
      {kScancodeEscape, graphscore::KeyCode::kEscape},
      {kScancodeTab, graphscore::KeyCode::kTab},
      {kScancodeSpace, graphscore::KeyCode::kSpace},
      {kScancodeKp1, graphscore::KeyCode::kNumPad1},
      {kScancodeKp2, graphscore::KeyCode::kNumPad2},
      {kScancodeKp3, graphscore::KeyCode::kNumPad3},
      {kScancodeKp4, graphscore::KeyCode::kNumPad4},
      {kScancodeKp5, graphscore::KeyCode::kNumPad5},
      {kScancodeKp6, graphscore::KeyCode::kNumPad6},
      {kScancodeKp7, graphscore::KeyCode::kNumPad7},
      {kScancodeKp0, graphscore::KeyCode::kNumPad0},
      {kScancodeKpPeriod, graphscore::KeyCode::kNumPadDecimal},
  }};

  for (const ScancodeCase& test_case : kMappedScancodes) {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(test_case.scancode, 0);
    if (handler.events.size() != before + 1 ||
        handler.events.back().code != test_case.expected) {
      std::fprintf(stderr,
                   "key-events-shell-test: scancode %u did not translate to "
                   "the expected KeyCode\n",
                   test_case.scancode);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // Unmapped scancode -> kUnknown (the out-of-scope character key `A`, whose
  // PHYSICAL scancode is not a positional KeyCode; the letter is surfaced
  // through the logical key instead).
  for (const std::uint32_t unmapped : {kScancodeA}) {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(unmapped, 0);
    if (handler.events.size() != before + 1 ||
        handler.events.back().code != graphscore::KeyCode::kUnknown) {
      std::fprintf(stderr,
                   "key-events-shell-test: unmapped scancode %u did not "
                   "translate to kUnknown\n",
                   unmapped);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // Logical letter-mnemonic keycodes (SDL_Keycode == lowercase ASCII): the
  // third dispatch parameter is the layout-mapped keycode, and the app binds
  // letters by it, independent of the physical scancode (§4). Exercise every
  // mnemonic in the keyboard workflow, plus an unmapped letter.
  struct LogicalCase {
    std::uint32_t          keycode;
    graphscore::LogicalKey expected;
  };

  constexpr std::array<LogicalCase, 15> kLogicalCases{{
      {0x61, graphscore::LogicalKey::kA},        // 'a'
      {0x62, graphscore::LogicalKey::kB},        // 'b'
      {0x63, graphscore::LogicalKey::kC},        // 'c'
      {0x64, graphscore::LogicalKey::kD},        // 'd'
      {0x65, graphscore::LogicalKey::kE},        // 'e'
      {0x66, graphscore::LogicalKey::kF},        // 'f'
      {0x67, graphscore::LogicalKey::kG},        // 'g'
      {0x6e, graphscore::LogicalKey::kN},        // 'n'
      {0x72, graphscore::LogicalKey::kR},        // 'r'
      {0x78, graphscore::LogicalKey::kX},        // 'x'
      {0x76, graphscore::LogicalKey::kV},        // 'v'
      {0x7a, graphscore::LogicalKey::kZ},        // 'z'
      {0x6b, graphscore::LogicalKey::kK},        // 'k'
      {0x74, graphscore::LogicalKey::kUnknown},  // 't'
      {0x79, graphscore::LogicalKey::kUnknown},  // 'y'
  }};
  for (const LogicalCase& test_case : kLogicalCases) {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(kScancodeA, 0, test_case.keycode);
    if (handler.events.size() != before + 1 ||
        handler.events.back().logical != test_case.expected) {
      std::fprintf(stderr,
                   "key-events-shell-test: keycode 0x%02X did not translate "
                   "to the expected LogicalKey\n",
                   test_case.keycode);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // The repeat flag surfaces verbatim: dispatch_sdl_test_key_event never
  // sets it (an initial press), and the seam's third parameter does not
  // either, so `repeat` is false on every event above. A repeat is only
  // distinguishable through a real SDL repeat event; the headless KeyEvent
  // seam carries it directly, so this test only asserts the default is false.
  {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(kScancodeLeft, 0);
    if (handler.events.size() != before + 1 || handler.events.back().repeat) {
      std::fprintf(stderr,
                   "key-events-shell-test: repeat flag was not false on an "
                   "initial press\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // Composed text input through the PRODUCTION SDL path: SDL_EVENT_TEXT_INPUT
  // is translated to TextInputEvent and delivered verbatim, including the
  // multi-character `whole`/`staff` filters and a non-US UTF-8 text that no
  // LogicalKey can name (§4, §5).
  {
    const std::array<std::string_view, 3> kTexts{"whole", "staff", "\xC3\xA9"};
    for (const std::string_view text : kTexts) {
      const std::size_t before = handler.text_events.size();
      shell.dispatch_sdl_test_text_input(text);
      if (handler.text_events.size() != before + 1 ||
          handler.text_events.back().text != text) {
        std::fprintf(stderr,
                     "key-events-shell-test: text input did not translate "
                     "through the production SDL path\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }

  // Raw SDL_Keymod bitmasks, verified against the fetched SDL3 headers
  // (SDL3/SDL_keycode.h at the pinned commit): SDL_KMOD_SHIFT=0x0003
  // (LSHIFT 0x0001 | RSHIFT 0x0002), SDL_KMOD_CTRL=0x00C0 (LCTRL 0x0040 |
  // RCTRL 0x0080), SDL_KMOD_ALT=0x0300 (LALT 0x0100 | RALT 0x0200),
  // SDL_KMOD_GUI=0x0C00 (LGUI 0x0400 | RGUI 0x0800).
  constexpr std::uint16_t kModShift = 0x0003;
  constexpr std::uint16_t kModCtrl  = 0x00C0;
  constexpr std::uint16_t kModAlt   = 0x0300;
  constexpr std::uint16_t kModGui   = 0x0C00;

  struct ModifierCase {
    std::uint16_t            mods;
    graphscore::KeyModifiers expected;
  };

  const std::array<ModifierCase, 5> kModifierCases{{
      {kModShift, {true, false, false, false}},
      {kModCtrl, {false, true, false, false}},
      {kModAlt, {false, false, true, false}},
      {kModGui, {false, false, false, true}},
      {static_cast<std::uint16_t>(kModShift | kModCtrl | kModAlt | kModGui),
       {true, true, true, true}},
  }};

  for (const ModifierCase& test_case : kModifierCases) {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(kScancodeLeft, test_case.mods);
    if (handler.events.size() != before + 1 ||
        handler.events.back().modifiers != test_case.expected) {
      std::fprintf(stderr,
                   "key-events-shell-test: modifier mask 0x%04X did not "
                   "translate correctly\n",
                   test_case.mods);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.set_input_handler(nullptr);
  std::printf("key-events-shell-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
