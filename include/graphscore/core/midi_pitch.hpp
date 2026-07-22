// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace graphscore {

// MIDI note number, legal range [0, 127] per the MIDI 1.0 specification.
class MidiPitch {
 public:
  static constexpr std::uint8_t kMin = 0;
  static constexpr std::uint8_t kMax = 127;

  constexpr MidiPitch() = default;

  [[nodiscard]] static constexpr std::optional<MidiPitch> create(
      std::uint8_t value) noexcept {
    if (value > kMax)
      return std::nullopt;
    return MidiPitch(value);
  }

  [[nodiscard]] constexpr std::uint8_t value() const noexcept { return value_; }

  [[nodiscard]] constexpr auto operator<=>(const MidiPitch&) const = default;

 private:
  constexpr explicit MidiPitch(std::uint8_t value) noexcept : value_(value) {}

  std::uint8_t value_ = kMin;
};

}  // namespace graphscore
