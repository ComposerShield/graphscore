// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace graphscore {

// MIDI channel, stored as the raw protocol nibble in [0, 15] — the 4-bit
// channel field carried in a MIDI 1.0 status byte and in the runtime's C
// ABI wire representation. Human-facing "Channel 1"-"Channel 16" labels are
// a presentation-layer +1 offset applied where channels are displayed, not
// part of this value type.
class MidiChannel {
 public:
  static constexpr std::uint8_t kMin = 0;
  static constexpr std::uint8_t kMax = 15;

  constexpr MidiChannel() = default;

  [[nodiscard]] static constexpr std::optional<MidiChannel> create(
      std::uint8_t value) noexcept {
    if (value > kMax)
      return std::nullopt;
    return MidiChannel(value);
  }

  [[nodiscard]] constexpr std::uint8_t value() const noexcept { return value_; }

  [[nodiscard]] constexpr auto operator<=>(const MidiChannel&) const = default;

 private:
  constexpr explicit MidiChannel(std::uint8_t value) noexcept : value_(value) {}

  std::uint8_t value_ = kMin;
};

}  // namespace graphscore
