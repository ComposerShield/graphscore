// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace graphscore {

// MIDI note-on/note-off velocity, legal range [0, 127] per the MIDI 1.0
// specification.
class MidiVelocity {
 public:
  static constexpr std::uint8_t kMin = 0;
  static constexpr std::uint8_t kMax = 127;

  constexpr MidiVelocity() = default;

  [[nodiscard]] static constexpr std::optional<MidiVelocity> create(
      std::uint8_t value) noexcept {
    if (value > kMax)
      return std::nullopt;
    return MidiVelocity(value);
  }

  [[nodiscard]] constexpr std::uint8_t value() const noexcept { return value_; }

  [[nodiscard]] constexpr auto operator<=>(const MidiVelocity&) const = default;

 private:
  constexpr explicit MidiVelocity(std::uint8_t value) noexcept
      : value_(value) {}

  std::uint8_t value_ = kMin;
};

}  // namespace graphscore
