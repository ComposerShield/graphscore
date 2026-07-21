// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace graphscore {

class Uuid {
 public:
  static constexpr std::size_t kSize = 16;

  Uuid() = default;

  explicit Uuid(const std::array<std::uint8_t, kSize>& bytes) : bytes_(bytes) {}

  [[nodiscard]] constexpr const std::array<std::uint8_t, kSize>& bytes()
      const noexcept {
    return bytes_;
  }

  [[nodiscard]] bool operator==(const Uuid& other) const noexcept {
    return bytes_ == other.bytes_;
  }

  [[nodiscard]] bool operator!=(const Uuid& other) const noexcept {
    return !(*this == other);
  }

  [[nodiscard]] std::string to_string() const;

  static Uuid generate();

 private:
  std::array<std::uint8_t, kSize> bytes_{};
};

}  // namespace graphscore
