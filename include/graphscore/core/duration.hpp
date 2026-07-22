// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/tempo.hpp>
#include <graphscore/core/tuplet_ratio.hpp>

namespace graphscore {

// A fully resolved notated duration: a base note value (whole through
// sixty-fourth, NoteValue defined alongside Tempo), 0-2 augmentation dots,
// and an optional single-level N:M TupletRatio. Duration always resolves
// to an exact whole-note Rational length; it never approximates with
// floating point.
class Duration {
 public:
  static constexpr std::uint8_t kMaxDots = 2;

  constexpr Duration() = default;

  // TupletRatio::create already rejects nonsensical ratios (e.g. 0:x), so
  // any `tuplet` passed here is already structurally valid; this factory
  // only needs to validate `dots`.
  [[nodiscard]] static constexpr std::optional<Duration> create(
      NoteValue base, std::uint8_t dots,
      std::optional<TupletRatio> tuplet = std::nullopt) noexcept {
    if (dots > kMaxDots)
      return std::nullopt;
    return Duration(base, dots, tuplet);
  }

  [[nodiscard]] constexpr NoteValue base() const noexcept { return base_; }

  [[nodiscard]] constexpr std::uint8_t dots() const noexcept { return dots_; }

  [[nodiscard]] constexpr const std::optional<TupletRatio>& tuplet()
      const noexcept {
    return tuplet_;
  }

  // The exact whole-note length: the base value's power-of-two fraction,
  // with each dot adding half of the previous increment, then scaled by
  // the tuplet factor if one is present.
  [[nodiscard]] constexpr Rational resolved() const noexcept {
    Rational length    = base_length(base_);
    Rational increment = length;
    for (std::uint8_t i = 0; i < dots_; ++i) {
      increment = increment / Rational(2);
      length    = length + increment;
    }
    if (tuplet_.has_value())
      length = length * tuplet_->factor();
    return length;
  }

  [[nodiscard]] constexpr bool operator==(const Duration&) const = default;

 private:
  constexpr Duration(NoteValue base, std::uint8_t dots,
                     std::optional<TupletRatio> tuplet) noexcept
      : base_(base), dots_(dots), tuplet_(tuplet) {}

  // Whole-note length of an undotted, un-tupleted `base` value: whole is
  // 1/1, half is 1/2, ..., sixty-fourth is 1/64.
  [[nodiscard]] static constexpr Rational base_length(NoteValue base) noexcept {
    const std::int64_t denominator = std::int64_t{1}
                                     << static_cast<std::uint8_t>(base);
    return Rational(1) / Rational(denominator);
  }

  NoteValue                  base_ = NoteValue::kQuarter;
  std::uint8_t               dots_ = 0;
  std::optional<TupletRatio> tuplet_;
};

}  // namespace graphscore
