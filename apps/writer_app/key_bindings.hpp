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

// The top-row digit 0-9 a physical digit KeyCode names, or nullopt for a
// non-digit code. Used by the Alt+digit voice shortcuts (Alt+`1`..`4`,
// action table §7.5) and by the explicit-no-op remainder cells for the
// unbound top-row `9`/`0` (§7.7).
[[nodiscard]] constexpr std::optional<std::uint8_t> top_row_digit(
    graphscore::KeyCode code) noexcept {
  switch (code) {
    case graphscore::KeyCode::kDigit0:
      return 0;
    case graphscore::KeyCode::kDigit1:
      return 1;
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
    case graphscore::KeyCode::kDigit9:
      return 9;
    case graphscore::KeyCode::kUnknown:
    default:
      return std::nullopt;
  }
}

// The note value a numpad digit arms (KP_`1`..KP_`7` = Whole..SixtyFourth,
// action table §7.6), or nullopt for a non-duration numpad key. Numpad keys
// are the physical, Num Lock-independent key family of the Unmodified chord
// class; they never collide with the top-row interval digits (§9).
[[nodiscard]] constexpr std::optional<graphscore::NoteValue> numpad_duration(
    graphscore::KeyCode code) noexcept {
  switch (code) {
    case graphscore::KeyCode::kNumPad1:
      return graphscore::NoteValue::kWhole;
    case graphscore::KeyCode::kNumPad2:
      return graphscore::NoteValue::kHalf;
    case graphscore::KeyCode::kNumPad3:
      return graphscore::NoteValue::kQuarter;
    case graphscore::KeyCode::kNumPad4:
      return graphscore::NoteValue::kEighth;
    case graphscore::KeyCode::kNumPad5:
      return graphscore::NoteValue::kSixteenth;
    case graphscore::KeyCode::kNumPad6:
      return graphscore::NoteValue::kThirtySecond;
    case graphscore::KeyCode::kNumPad7:
      return graphscore::NoteValue::kSixtyFourth;
    case graphscore::KeyCode::kUnknown:
    default:
      return std::nullopt;
  }
}

// The diatonic Letter a logical letter mnemonic names (`A`-`G` = pitch,
// action table §7.1), or nullopt for any other logical key.
[[nodiscard]] constexpr std::optional<graphscore::Letter> logical_pitch_letter(
    graphscore::LogicalKey key) noexcept {
  switch (key) {
    case graphscore::LogicalKey::kA:
      return graphscore::Letter::kA;
    case graphscore::LogicalKey::kB:
      return graphscore::Letter::kB;
    case graphscore::LogicalKey::kC:
      return graphscore::Letter::kC;
    case graphscore::LogicalKey::kD:
      return graphscore::Letter::kD;
    case graphscore::LogicalKey::kE:
      return graphscore::Letter::kE;
    case graphscore::LogicalKey::kF:
      return graphscore::Letter::kF;
    case graphscore::LogicalKey::kG:
      return graphscore::Letter::kG;
    case graphscore::LogicalKey::kUnknown:
    default:
      return std::nullopt;
  }
}

// The five disjoint bound chord classes plus the explicit unbound remainder
// (docs/plan/05-notation-editor-action-table.md §3). Because the five
// classes are defined by disjoint conditions, one key event matches at most
// one class and the evaluation order is irrelevant; everything outside the
// five classes maps to kUnbound (the explicit no-op remainder).
enum class ChordClass : std::uint8_t {
  kUnmodified,
  kShift,
  kPrimary,
  kShiftPrimary,
  kAlt,
  kUnbound,
};

[[nodiscard]] constexpr ChordClass classify_chord(
    graphscore::KeyModifiers modifiers, PrimaryModifier primary) {
  const bool shift       = modifiers.shift;
  const bool control     = modifiers.control;
  const bool alt         = modifiers.alt;
  const bool meta        = modifiers.meta;
  const bool primary_set = primary == PrimaryModifier::kMeta ? meta : control;
  const bool non_primary_set =
      primary == PrimaryModifier::kMeta ? control : meta;

  if (!shift && !control && !alt && !meta) {
    return ChordClass::kUnmodified;
  }
  if (shift && !control && !alt && !meta) {
    return ChordClass::kShift;
  }
  if (alt && !shift && !control && !meta) {
    return ChordClass::kAlt;
  }
  if (primary_set && !shift && !alt && !non_primary_set) {
    return ChordClass::kPrimary;
  }
  if (primary_set && shift && !alt && !non_primary_set) {
    return ChordClass::kShiftPrimary;
  }
  return ChordClass::kUnbound;
}

}  // namespace graphscore::writer_app
