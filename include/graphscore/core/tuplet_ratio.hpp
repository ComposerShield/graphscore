// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

#include <graphscore/core/rational.hpp>

namespace graphscore {

// A single-level arbitrary N:M tuplet ratio: `played` notes are performed
// in the time normally occupied by `normal` notes of the same base value
// (for example, a triplet is played=3, normal=2; a quintuplet is
// played=5, normal=4). Both fields must be strictly positive; nested or
// multi-level tuplets are out of "0.1.0" notation scope.
class TupletRatio {
 public:
  constexpr TupletRatio() = default;

  [[nodiscard]] static constexpr std::optional<TupletRatio> create(
      std::uint16_t played, std::uint16_t normal) noexcept {
    if (played == 0 || normal == 0)
      return std::nullopt;
    return TupletRatio(played, normal);
  }

  [[nodiscard]] constexpr std::uint16_t played() const noexcept {
    return played_;
  }

  [[nodiscard]] constexpr std::uint16_t normal() const noexcept {
    return normal_;
  }

  // The exact scale factor applied to a plain duration's resolved length:
  // `normal` notes' worth of time divided across `played` notes.
  [[nodiscard]] constexpr Rational factor() const noexcept {
    return Rational(normal_) / Rational(played_);
  }

  [[nodiscard]] constexpr bool operator==(const TupletRatio&) const = default;

 private:
  constexpr TupletRatio(std::uint16_t played, std::uint16_t normal) noexcept
      : played_(played), normal_(normal) {}

  std::uint16_t played_ = 1;
  std::uint16_t normal_ = 1;
};

}  // namespace graphscore
