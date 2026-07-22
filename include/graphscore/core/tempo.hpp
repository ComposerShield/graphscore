// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

#include <graphscore/core/rational.hpp>

namespace graphscore {

// Undotted base note value used as a tempo point's explicit beat unit.
// Dotted durations and tuplets belong to the notation model, not this
// value-type layer.
enum class NoteValue : std::uint8_t {
  kWhole = 0,
  kHalf,
  kQuarter,
  kEighth,
  kSixteenth,
  kThirtySecond,
  kSixtyFourth,
};

// A tempo point: an exact BPM value in [10, 400] paired with the beat unit
// it is measured against. BPM is stored as a Rational rather than a
// floating-point value so tempo comparisons remain exact.
class Tempo {
 public:
  static constexpr std::int64_t kMinBpm = 10;
  static constexpr std::int64_t kMaxBpm = 400;

  constexpr Tempo() = default;

  [[nodiscard]] static constexpr std::optional<Tempo> create(
      Rational bpm, NoteValue beat_unit) noexcept {
    if (bpm < Rational(kMinBpm) || bpm > Rational(kMaxBpm))
      return std::nullopt;
    return Tempo(bpm, beat_unit);
  }

  [[nodiscard]] constexpr const Rational& bpm() const noexcept { return bpm_; }

  [[nodiscard]] constexpr NoteValue beat_unit() const noexcept {
    return beat_unit_;
  }

  [[nodiscard]] bool operator==(const Tempo&) const = default;

 private:
  constexpr Tempo(Rational bpm, NoteValue beat_unit) noexcept
      : bpm_(bpm), beat_unit_(beat_unit) {}

  Rational  bpm_       = Rational(120);
  NoteValue beat_unit_ = NoteValue::kQuarter;
};

}  // namespace graphscore
