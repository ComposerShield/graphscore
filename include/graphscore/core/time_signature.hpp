// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

namespace graphscore {

// Musical time signature. denominator must be a power of two (1 = whole
// note through 128 = 128th note); numerator is bounded generously enough
// to cover any practical meter without becoming an unbounded integer.
class TimeSignature {
 public:
  static constexpr std::uint8_t  kMinNumerator   = 1;
  static constexpr std::uint8_t  kMaxNumerator   = 64;
  static constexpr std::uint16_t kMinDenominator = 1;
  static constexpr std::uint16_t kMaxDenominator = 128;

  constexpr TimeSignature() = default;

  [[nodiscard]] static constexpr std::optional<TimeSignature> create(
      std::uint8_t numerator, std::uint16_t denominator) noexcept {
    if (numerator < kMinNumerator || numerator > kMaxNumerator)
      return std::nullopt;
    if (denominator < kMinDenominator || denominator > kMaxDenominator)
      return std::nullopt;
    if ((denominator & (denominator - 1)) != 0)
      return std::nullopt;
    return TimeSignature(numerator, denominator);
  }

  [[nodiscard]] constexpr std::uint8_t numerator() const noexcept {
    return numerator_;
  }

  [[nodiscard]] constexpr std::uint16_t denominator() const noexcept {
    return denominator_;
  }

  [[nodiscard]] bool operator==(const TimeSignature&) const = default;

 private:
  constexpr TimeSignature(std::uint8_t  numerator,
                          std::uint16_t denominator) noexcept
      : numerator_(numerator), denominator_(denominator) {}

  std::uint8_t  numerator_   = 4;
  std::uint16_t denominator_ = 4;
};

}  // namespace graphscore
