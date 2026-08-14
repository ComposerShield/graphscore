// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <optional>

namespace graphscore::writer_app {

// Provisional keyboard range-extension step for Shift+Left/Right
// (M5-phase-19b-iii): one quarter of a whole note. Superseded by
// M5-phase-27's platform-normalized action table, which chooses the actual
// step per action (diatonic step, beat, measure, ...); defined once here
// rather than scattered as a repeated literal.
constexpr graphscore::Rational kProvisionalRangeExtensionStep =
    *graphscore::Rational::create(1, 4);

// Which platform modifier the action table's "Primary" chord means
// (docs/plan/05-notation-editor.md M5-phase-24: "Primary is Command on
// macOS and Control on Windows/Linux"). KeyModifiers deliberately does NOT
// collapse `meta` and `control` into a Primary bit -- its own comment names
// that a product decision for the action table rather than something the
// shell should bake in -- so the decision is made here, at the application
// assembly layer that owns the bindings.
enum class PrimaryModifier : std::uint8_t { kMeta, kControl };

// The one conditionally-compiled value in this file. It selects the DEFAULT
// mapping only: every rule below is written against the parameter, never
// against the macro, so a single headless test asserts both platforms'
// mappings on any host.
#if defined(__APPLE__)
constexpr PrimaryModifier kPlatformPrimaryModifier = PrimaryModifier::kMeta;
#else
constexpr PrimaryModifier kPlatformPrimaryModifier = PrimaryModifier::kControl;
#endif

// True for an EXACT Primary chord: `primary`'s own modifier is held while
// shift, alt, and the other of control/meta are all clear. The exactness
// matches the unmodified and Shift branches of on_key_press, which likewise
// refuse to fire on a superset chord (M5-phase-20/21/23's discipline).
[[nodiscard]] constexpr bool is_primary_chord(
    graphscore::KeyModifiers modifiers, PrimaryModifier primary) {
  if (modifiers.shift || modifiers.alt) {
    return false;
  }
  return primary == PrimaryModifier::kMeta
             ? modifiers.meta && !modifiers.control
             : modifiers.control && !modifiers.meta;
}

// The diatonic interval number a digit KeyCode names, or nullopt when the
// code is not a bound digit. `2` through `8` are one letter step through
// seven letter steps (an octave); `1` is deliberately NOT a binding
// (M5-phase-25's no-op), so it maps to nullopt exactly like every non-digit
// code, leaving the handler's default no-op behavior.
[[nodiscard]] constexpr std::optional<std::uint8_t> digit_interval(
    graphscore::KeyCode code) noexcept {
  switch (code) {
    case graphscore::KeyCode::kDigit2:
      return 2;
    case graphscore::KeyCode::kDigit3:
      return 3;
    case graphscore::KeyCode::kDigit4:
      return 4;
    case graphscore::KeyCode::kDigit5:
      return 5;
    case graphscore::KeyCode::kDigit6:
      return 6;
    case graphscore::KeyCode::kDigit7:
      return 7;
    case graphscore::KeyCode::kDigit8:
      return 8;
    case graphscore::KeyCode::kUnknown:
    default:
      return std::nullopt;
  }
}

}  // namespace graphscore::writer_app
