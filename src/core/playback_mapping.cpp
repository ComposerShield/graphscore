// SPDX-License-Identifier: Apache-2.0

#include <graphscore/core/playback_mapping.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace graphscore {

namespace {

// Rounds a non-negative exact Rational to the nearest std::uint8_t with
// round-half-to-even tie-breaking, entirely in integer arithmetic (see
// playback_mapping.hpp's overview, "Hairpin interpolation and its rounding
// rule"). Precondition: value's numerator/denominator are both
// non-negative (guaranteed here because MidiVelocity is never negative and
// interpolate_hairpin_velocity() clamps `position` to [0, 1] before this
// is called, so the interpolated value always lies within
// [min(from, to), max(from, to)] subset of [0, 127]).
[[nodiscard]] std::uint8_t round_velocity(Rational value) noexcept {
  const std::int64_t numerator       = value.numerator();
  const std::int64_t denominator     = value.denominator();
  const std::int64_t quotient        = numerator / denominator;
  const std::int64_t remainder       = numerator % denominator;
  const std::int64_t twice_remainder = 2 * remainder;

  std::int64_t rounded = quotient;
  if (twice_remainder > denominator) {
    rounded = quotient + 1;
  } else if (twice_remainder == denominator) {
    rounded = (quotient % 2 == 0) ? quotient : quotient + 1;
  }

  const std::int64_t clamped =
      std::clamp<std::int64_t>(rounded, MidiVelocity::kMin, MidiVelocity::kMax);
  return static_cast<std::uint8_t>(clamped);
}

// Weights for grace_steal_durations()'s multi-note division, summing to
// exactly Rational(1) for any note_count >= 1. See playback_mapping.hpp's
// overview, "Multi-note division" and "Grace-group size bound": for
// acciaccatura, the geometric halving depth is capped at
// kMaxGraceNotesPerGroup so the shift exponent constructed below never
// approaches 64 regardless of note_count, with every note beyond that
// depth sharing the smallest slot equally rather than continuing to
// halve.
[[nodiscard]] std::vector<Rational> grace_note_weights(GraceNoteType type,
                                                       std::size_t note_count) {
  std::vector<Rational> weights;
  weights.reserve(note_count);

  if (note_count == 0)
    return weights;

  if (type == GraceNoteType::kAppoggiatura) {
    const Rational even_share =
        Rational(1) / Rational(static_cast<std::int64_t>(note_count));
    for (std::size_t index = 0; index < note_count; ++index)
      weights.push_back(even_share);
    return weights;
  }

  const std::size_t depth = std::min(note_count, kMaxGraceNotesPerGroup);
  for (std::size_t index = 0; index + 1 < depth; ++index) {
    const std::int64_t power = static_cast<std::int64_t>(index) + 1;
    weights.push_back(Rational(1) / Rational(std::int64_t{1} << power));
  }
  const std::int64_t last_power = static_cast<std::int64_t>(depth) - 1;
  const Rational     last_slot_weight =
      Rational(1) / Rational(std::int64_t{1} << last_power);
  const std::size_t notes_sharing_last_slot = note_count - (depth - 1);
  const Rational    each_shared_weight =
      last_slot_weight /
      Rational(static_cast<std::int64_t>(notes_sharing_last_slot));
  for (std::size_t index = 0; index < notes_sharing_last_slot; ++index)
    weights.push_back(each_shared_weight);
  return weights;
}

// The capped total fraction of `available_duration` that
// grace_steal_durations() distributes across `note_count` grace notes of
// kind `type`. Shared by grace_steal_durations() and
// grace_steal_remaining_duration() so the two cannot drift apart -- see
// playback_mapping.hpp's overview, "Composition order with articulation".
[[nodiscard]] Rational capped_total_steal_fraction(
    GraceNoteType type, std::size_t note_count) noexcept {
  const Rational per_note_fraction = (type == GraceNoteType::kAcciaccatura)
                                         ? kAcciaccaturaPerNoteStealFraction
                                         : kAppoggiaturaPerNoteStealFraction;
  const Rational total_fraction_uncapped =
      per_note_fraction * Rational(static_cast<std::int64_t>(note_count));
  return std::min(total_fraction_uncapped, kMaxGraceStealFraction);
}

}  // namespace

MidiVelocity velocity_for_dynamic(Dynamic dynamic) noexcept {
  const std::size_t index = static_cast<std::size_t>(dynamic);
  return *MidiVelocity::create(kDynamicVelocityTable[index]);
}

MidiVelocity interpolate_hairpin_velocity(MidiVelocity from, MidiVelocity to,
                                          Rational position) noexcept {
  const Rational clamped_position =
      std::clamp(position, Rational(0), Rational(1));
  const Rational from_value(from.value());
  const Rational to_value(to.value());
  const Rational interpolated =
      from_value + (to_value - from_value) * clamped_position;
  return *MidiVelocity::create(round_velocity(interpolated));
}

MidiVelocity apply_emphasis(MidiVelocity base, bool accent,
                            bool marcato) noexcept {
  std::int32_t boosted = base.value();
  if (marcato) {
    boosted += kMarcatoVelocityBoost;
  } else if (accent) {
    boosted += kAccentVelocityBoost;
  }
  const std::int32_t saturated =
      std::clamp<std::int32_t>(boosted, MidiVelocity::kMin, MidiVelocity::kMax);
  return *MidiVelocity::create(static_cast<std::uint8_t>(saturated));
}

Rational sounded_duration_for_articulation(
    Rational                    notated_duration,
    std::optional<Articulation> duration_articulation, bool is_tied) noexcept {
  if (is_tied)
    return notated_duration;

  if (!duration_articulation.has_value())
    return notated_duration * kDefaultSoundedDurationRatio;

  switch (*duration_articulation) {
    case Articulation::kStaccato:
      return notated_duration * kStaccatoSoundedDurationRatio;
    case Articulation::kStaccatissimo:
      return notated_duration * kStaccatissimoSoundedDurationRatio;
    case Articulation::kTenuto:
      return notated_duration * kTenutoSoundedDurationRatio;
    case Articulation::kAccent:
    case Articulation::kMarcato:
      return notated_duration * kDefaultSoundedDurationRatio;
  }
  return notated_duration * kDefaultSoundedDurationRatio;
}

Rational legato_sounded_duration(Rational articulated_duration,
                                 Rational gap_to_next_onset) noexcept {
  return articulated_duration + gap_to_next_onset;
}

std::vector<Rational> grace_steal_durations(GraceNoteType type,
                                            std::size_t   note_count,
                                            Rational      available_duration) {
  std::vector<Rational> durations;
  durations.reserve(note_count);

  if (note_count == 0)
    return durations;

  if (available_duration <= Rational(0)) {
    for (std::size_t index = 0; index < note_count; ++index)
      durations.push_back(kGraceFallbackNoteDuration);
    return durations;
  }

  const Rational total_fraction = capped_total_steal_fraction(type, note_count);
  const Rational total_duration = total_fraction * available_duration;

  const std::vector<Rational> weights = grace_note_weights(type, note_count);
  for (const Rational& weight : weights)
    durations.push_back(weight * total_duration);
  return durations;
}

Rational grace_steal_remaining_duration(GraceNoteType type,
                                        std::size_t   note_count,
                                        Rational      available_duration) {
  if (available_duration <= Rational(0))
    return available_duration;

  const Rational total_fraction = capped_total_steal_fraction(type, note_count);
  return available_duration * (Rational(1) - total_fraction);
}

}  // namespace graphscore
