// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>

namespace graphscore {

class Rational {
 public:
  constexpr Rational() = default;

  constexpr Rational(std::int64_t num, std::int64_t den)
      : num_(num), den_(den) {}

  [[nodiscard]] constexpr std::int64_t numerator() const noexcept {
    return num_;
  }

  [[nodiscard]] constexpr std::int64_t denominator() const noexcept {
    return den_;
  }

  [[nodiscard]] constexpr double to_double() const noexcept {
    return den_ != 0 ? static_cast<double>(num_) / static_cast<double>(den_)
                     : 0.0;
  }

  [[nodiscard]] constexpr auto operator<=>(const Rational&) const = default;

 private:
  std::int64_t num_ = 0;
  std::int64_t den_ = 1;
};

}  // namespace graphscore
