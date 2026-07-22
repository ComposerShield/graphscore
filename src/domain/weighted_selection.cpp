// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/weighted_selection.hpp>

#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace graphscore {

namespace {

// Every arithmetic step this file performs while building a group's common
// denominator is checked for 64-bit-unsigned overflow (see
// weighted_selection.hpp's "Overflow" section); these three helpers are the
// only place that checking happens. __builtin_*_overflow is a Clang/
// AppleClang/clang-cl extension (available on every toolchain this project
// supports; see AGENTS.md's C++23 requirement); <stdckdint.h>'s
// ckd_add/ckd_mul, standardized in C23 and not yet in a shipped C++
// standard, is the portable successor once it is available here.

[[nodiscard]] std::optional<std::uint64_t> checked_mul(std::uint64_t lhs,
                                                       std::uint64_t rhs) {
  std::uint64_t result = 0;
  if (__builtin_mul_overflow(lhs, rhs, &result))
    return std::nullopt;
  return result;
}

[[nodiscard]] std::optional<std::uint64_t> checked_add(std::uint64_t lhs,
                                                       std::uint64_t rhs) {
  std::uint64_t result = 0;
  if (__builtin_add_overflow(lhs, rhs, &result))
    return std::nullopt;
  return result;
}

[[nodiscard]] std::optional<std::uint64_t> checked_lcm(std::uint64_t lhs,
                                                       std::uint64_t rhs) {
  const std::uint64_t divisor = std::gcd(lhs, rhs);
  return checked_mul(lhs / divisor, rhs);
}

// One eligible (strictly positive weight) candidate's bucket width over the
// group's common denominator, in the group's own sequence order.
struct EligibleShare {
  ConnectorId   connector;
  std::uint64_t numerator;
};

struct GroupComputation {
  WeightGroupValidity validity    = WeightGroupValidity::kNoEligibleCandidates;
  std::uint64_t       denominator = 0;
  std::vector<EligibleShare> shares;
};

[[nodiscard]] GroupComputation compute_group(
    std::span<const WeightedCandidate> candidates) {
  for (const WeightedCandidate& candidate : candidates) {
    if (candidate.weight.numerator() < 0)
      return GroupComputation{WeightGroupValidity::kInvalidTotal, 0, {}};
  }

  std::uint64_t denominator = 1;
  for (const WeightedCandidate& candidate : candidates) {
    if (candidate.weight.numerator() == 0)
      continue;
    const std::optional<std::uint64_t> folded =
        checked_lcm(denominator,
                    static_cast<std::uint64_t>(candidate.weight.denominator()));
    if (!folded.has_value())
      return GroupComputation{WeightGroupValidity::kDenominatorOverflow, 0, {}};
    denominator = *folded;
  }

  std::vector<EligibleShare> shares;
  shares.reserve(candidates.size());
  std::uint64_t total = 0;
  for (const WeightedCandidate& candidate : candidates) {
    if (candidate.weight.numerator() == 0)
      continue;

    const std::uint64_t factor =
        denominator /
        static_cast<std::uint64_t>(candidate.weight.denominator());
    const std::optional<std::uint64_t> share = checked_mul(
        static_cast<std::uint64_t>(candidate.weight.numerator()), factor);
    if (!share.has_value())
      return GroupComputation{WeightGroupValidity::kDenominatorOverflow, 0, {}};

    const std::optional<std::uint64_t> running_total =
        checked_add(total, *share);
    if (!running_total.has_value())
      return GroupComputation{WeightGroupValidity::kDenominatorOverflow, 0, {}};
    total = *running_total;

    shares.push_back(EligibleShare{candidate.connector, *share});
  }

  if (shares.empty())
    return GroupComputation{WeightGroupValidity::kNoEligibleCandidates, 0, {}};
  if (total != denominator)
    return GroupComputation{WeightGroupValidity::kInvalidTotal, 0, {}};
  return GroupComputation{WeightGroupValidity::kValid, denominator,
                          std::move(shares)};
}

}  // namespace

WeightGroupValidity validate_weight_group(
    std::span<const WeightedCandidate> candidates) {
  return compute_group(candidates).validity;
}

std::optional<std::uint64_t> weight_group_roll_bound(
    std::span<const WeightedCandidate> candidates) {
  const GroupComputation computation = compute_group(candidates);
  if (computation.validity != WeightGroupValidity::kValid)
    return std::nullopt;
  return computation.denominator;
}

std::optional<ConnectorId> select_weighted_random(
    std::span<const WeightedCandidate> candidates, std::uint64_t roll) {
  const GroupComputation computation = compute_group(candidates);
  if (computation.validity != WeightGroupValidity::kValid)
    return std::nullopt;

  std::uint64_t cumulative = 0;
  for (const EligibleShare& share : computation.shares) {
    cumulative += share.numerator;
    if (roll < cumulative)
      return share.connector;
  }
  return std::nullopt;
}

}  // namespace graphscore
