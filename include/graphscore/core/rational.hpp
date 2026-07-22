// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <numeric>
#include <optional>

namespace graphscore {

// Explicit numerator/denominator pair for serialization boundaries. Carries
// no invariants of its own; Rational::from_components validates and reduces.
struct RationalComponents {
  std::int64_t numerator   = 0;
  std::int64_t denominator = 1;
};

// Exact rational number for musical positions and durations. Always stored
// reduced to lowest terms with a positive denominator, so equality and
// ordering never depend on floating-point comparison.
class Rational {
 public:
  constexpr Rational() = default;

  // A whole number is always a valid rational (denominator 1), so this
  // constructor needs no validated-construction factory.
  constexpr explicit Rational(std::int64_t whole) noexcept
      : num_(whole), den_(1) {}

  // Fails only when denominator is zero. On success the result is reduced
  // to lowest terms with a canonical positive denominator.
  [[nodiscard]] static constexpr std::optional<Rational> create(
      std::int64_t numerator, std::int64_t denominator) noexcept {
    if (denominator == 0)
      return std::nullopt;
    return Rational(numerator, denominator, kReduce);
  }

  [[nodiscard]] static constexpr std::optional<Rational> from_components(
      const RationalComponents& components) noexcept {
    return create(components.numerator, components.denominator);
  }

  [[nodiscard]] constexpr RationalComponents to_components() const noexcept {
    return RationalComponents{num_, den_};
  }

  [[nodiscard]] constexpr std::int64_t numerator() const noexcept {
    return num_;
  }

  [[nodiscard]] constexpr std::int64_t denominator() const noexcept {
    return den_;
  }

  // Approximate value for display/estimation only; never use for structural
  // score comparisons.
  [[nodiscard]] constexpr double to_double() const noexcept {
    return static_cast<double>(num_) / static_cast<double>(den_);
  }

  [[nodiscard]] constexpr Rational operator+(
      const Rational& other) const noexcept {
    return Rational(num_ * other.den_ + other.num_ * den_, den_ * other.den_,
                    kReduce);
  }

  [[nodiscard]] constexpr Rational operator-(
      const Rational& other) const noexcept {
    return Rational(num_ * other.den_ - other.num_ * den_, den_ * other.den_,
                    kReduce);
  }

  [[nodiscard]] constexpr Rational operator*(
      const Rational& other) const noexcept {
    return Rational(num_ * other.num_, den_ * other.den_, kReduce);
  }

  // Precondition: other is non-zero. Dividing by a zero rational is
  // undefined, exactly as dividing an integer by zero is.
  [[nodiscard]] constexpr Rational operator/(
      const Rational& other) const noexcept {
    return Rational(num_ * other.den_, den_ * other.num_, kReduce);
  }

  [[nodiscard]] constexpr Rational operator-() const noexcept {
    return Rational(-num_, den_, kReduce);
  }

  // Cross-multiplies rather than comparing raw fields, so values with
  // different denominators (e.g. 1/2 and 2/4) order and compare correctly.
  [[nodiscard]] constexpr std::strong_ordering operator<=>(
      const Rational& other) const noexcept {
    return num_ * other.den_ <=> other.num_ * den_;
  }

  [[nodiscard]] constexpr bool operator==(
      const Rational& other) const noexcept {
    return num_ * other.den_ == other.num_ * den_;
  }

 private:
  enum ReduceTag { kReduce };

  constexpr Rational(std::int64_t numerator, std::int64_t denominator,
                     ReduceTag) noexcept {
    if (denominator < 0) {
      numerator   = -numerator;
      denominator = -denominator;
    }
    const std::int64_t divisor = std::gcd(numerator, denominator);
    num_                       = numerator / divisor;
    den_                       = denominator / divisor;
  }

  std::int64_t num_ = 0;
  std::int64_t den_ = 1;
};

}  // namespace graphscore
