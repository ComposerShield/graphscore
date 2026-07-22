// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

namespace graphscore {

// Standard pitched clef. A grand staff is expressed as a pair of staves
// (typically kTreble over kBass), not a distinct clef value.
enum class Clef : std::uint8_t {
  kTreble = 0,
  kBass,
  kAlto,
  kTenor,
};

[[nodiscard]] constexpr std::uint8_t to_index(Clef clef) noexcept {
  return static_cast<std::uint8_t>(clef);
}

// Explicit, validated conversion from a serialized clef index.
[[nodiscard]] constexpr std::optional<Clef> clef_from_index(
    std::uint8_t index) noexcept {
  switch (index) {
    case 0:
      return Clef::kTreble;
    case 1:
      return Clef::kBass;
    case 2:
      return Clef::kAlto;
    case 3:
      return Clef::kTenor;
    default:
      return std::nullopt;
  }
}

}  // namespace graphscore
