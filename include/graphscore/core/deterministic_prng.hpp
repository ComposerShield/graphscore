// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace graphscore {

// A small, fully specified, host-seeded pseudorandom generator for
// deterministic playback selection (docs/plan/README.md: "Random groups
// use deterministic host-seeded randomness"; docs/plan/02-domain-model.md:
// "deterministic PRNG reset"). This is SplitMix64 (Steele, Lea, and Flood,
// "Fast Splittable Pseudorandom Number Generators", 2014; the reference
// implementation is public domain), reproduced here verbatim:
//
//   state += 0x9E3779B97F4A7C15;
//   z = state;
//   z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
//   z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
//   return z ^ (z >> 31);
//
// SplitMix64 is chosen, over a larger generator such as xoshiro256**, for
// exactly one reason: its entire definition is three fixed-width 64-bit
// multiply/xor-shift steps over unsigned integers, with no
// platform-defined or implementation-defined behavior anywhere (no
// std::mt19937, no std::random_device, no reliance on the width of
// unsigned long or on signed overflow). That makes it the smallest
// specification a second, independent implementation -- the writer's, or
// the runtime's, in a different language or compiler -- can transcribe
// and be guaranteed to reproduce byte-for-byte identical output from the
// same seed, which is exactly the milestone's acceptance bar: "complete
// enough to implement independently in writer/runtime and compare with
// shared golden vectors" (docs/plan/02-domain-model.md, Acceptance
// Criteria). It is not cryptographically secure and is not intended to
// be.
//
// next_below(bound) reduces next_u64() by exact integer modulo, not
// rejection sampling. This introduces a modulo bias against 2^64 whenever
// `bound` does not evenly divide it. For the small common denominators
// realistic weighted-random authoring produces (weighted_selection.hpp),
// that bias is negligible; for a pathological `bound` approaching 2^63 (the
// group's exact common denominator is permitted to grow that large before
// weighted_selection.hpp reports WeightGroupValidity::kDenominatorOverflow),
// the bias is not negligible at all. This class does not promise uniformity
// in that case -- the guarantee it promises, and the only one the
// milestone's acceptance bar actually requires, is bit-exact reproducibility
// across independent implementations of the same modulo reduction, which
// holds for every `bound` regardless of size. The simpler, unambiguous
// modulo definition is chosen over rejection sampling specifically because
// it is what makes that guarantee possible to transcribe exactly.
class DeterministicPrng {
 public:
  constexpr explicit DeterministicPrng(std::uint64_t seed) noexcept
      : state_(seed) {}

  // Reseeds this generator in place. After this call, the sequence
  // next_u64()/next_below() produces from this point on is bit-for-bit
  // identical to a freshly constructed DeterministicPrng(seed)'s sequence,
  // regardless of how many values this instance had already produced
  // before the call -- the "deterministic PRNG reset" the plan requires.
  constexpr void reset(std::uint64_t seed) noexcept { state_ = seed; }

  // Advances the generator by exactly one SplitMix64 step (see this
  // class's overview above for the exact arithmetic) and returns the raw
  // 64-bit output.
  [[nodiscard]] constexpr std::uint64_t next_u64() noexcept {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state_;
    z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z               = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  // A uniform integer in [0, bound), advancing the generator by one step
  // regardless of `bound` (even bound == 0 -- see below -- still consumes
  // exactly one SplitMix64 step, so the stream position after the call is
  // identical either way). See this class's overview above for the exact
  // (modulo, not rejection-sampled) reduction and why. bound == 0 has no
  // meaningful uniform range to draw from; rather than divide by zero, this
  // returns 0 in that case -- a documented, harmless result a caller with a
  // genuinely empty range can simply ignore.
  [[nodiscard]] constexpr std::uint64_t next_below(
      std::uint64_t bound) noexcept {
    const std::uint64_t raw = next_u64();
    return bound == 0 ? 0 : raw % bound;
  }

  [[nodiscard]] constexpr bool operator==(const DeterministicPrng&) const =
      default;

 private:
  std::uint64_t state_;
};

}  // namespace graphscore
