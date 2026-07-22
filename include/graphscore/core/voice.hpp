// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace graphscore {

// One of up to four explicit voices per staff, numbered 1-4 to match
// conventional notation-software labeling ("Voice 1" .. "Voice 4").
class Voice {
 public:
  static constexpr std::uint8_t kMin = 1;
  static constexpr std::uint8_t kMax = 4;

  constexpr Voice() = default;

  [[nodiscard]] static constexpr std::optional<Voice> create(
      std::uint8_t index) noexcept {
    if (index < kMin || index > kMax)
      return std::nullopt;
    return Voice(index);
  }

  [[nodiscard]] constexpr std::uint8_t index() const noexcept { return index_; }

  [[nodiscard]] constexpr auto operator<=>(const Voice&) const = default;

 private:
  constexpr explicit Voice(std::uint8_t index) noexcept : index_(index) {}

  std::uint8_t index_ = kMin;
};

}  // namespace graphscore
