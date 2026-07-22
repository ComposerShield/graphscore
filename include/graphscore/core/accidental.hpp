// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

namespace graphscore {

// Note-spelling accidental, double-flat through double-sharp. The
// underlying value is the semitone offset from the natural pitch.
enum class Accidental : std::int8_t {
  kDoubleFlat  = -2,
  kFlat        = -1,
  kNatural     = 0,
  kSharp       = 1,
  kDoubleSharp = 2,
};

[[nodiscard]] constexpr std::int8_t to_semitone_offset(
    Accidental accidental) noexcept {
  return static_cast<std::int8_t>(accidental);
}

// Explicit, validated conversion from a serialized semitone offset.
[[nodiscard]] constexpr std::optional<Accidental> accidental_from_offset(
    std::int8_t offset) noexcept {
  switch (offset) {
    case -2:
      return Accidental::kDoubleFlat;
    case -1:
      return Accidental::kFlat;
    case 0:
      return Accidental::kNatural;
    case 1:
      return Accidental::kSharp;
    case 2:
      return Accidental::kDoubleSharp;
    default:
      return std::nullopt;
  }
}

}  // namespace graphscore
