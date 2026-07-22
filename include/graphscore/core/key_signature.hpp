// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

namespace graphscore {

enum class Mode : std::uint8_t {
  kMajor = 0,
  kMinor,
};

// Standard key signature through seven sharps/flats, represented as a
// signed count of fifths: negative is flats, positive is sharps.
class KeySignature {
 public:
  static constexpr std::int8_t kMinFifths = -7;
  static constexpr std::int8_t kMaxFifths = 7;

  constexpr KeySignature() = default;

  [[nodiscard]] static constexpr std::optional<KeySignature> create(
      std::int8_t fifths, Mode mode = Mode::kMajor) noexcept {
    if (fifths < kMinFifths || fifths > kMaxFifths)
      return std::nullopt;
    return KeySignature(fifths, mode);
  }

  [[nodiscard]] constexpr std::int8_t fifths() const noexcept {
    return fifths_;
  }

  [[nodiscard]] constexpr Mode mode() const noexcept { return mode_; }

  [[nodiscard]] bool operator==(const KeySignature&) const = default;

 private:
  constexpr KeySignature(std::int8_t fifths, Mode mode) noexcept
      : fifths_(fifths), mode_(mode) {}

  std::int8_t fifths_ = 0;
  Mode        mode_   = Mode::kMajor;
};

}  // namespace graphscore
