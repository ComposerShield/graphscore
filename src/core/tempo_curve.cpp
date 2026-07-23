// SPDX-License-Identifier: Apache-2.0

#include <graphscore/core/tempo_curve.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace graphscore {

namespace {

// See tempo_curve.hpp's overview, "Normalizing a beat-unit-relative BPM to
// a common rate": the same power-of-two convention Duration::base_length()
// (core/duration.hpp) uses for note-value fractions.
[[nodiscard]] Rational note_value_fraction(NoteValue value) noexcept {
  const std::int64_t denominator = std::int64_t{1}
                                   << static_cast<std::uint8_t>(value);
  return Rational(1) / Rational(denominator);
}

[[nodiscard]] double normalized_rate_whole_notes_per_second(
    const Tempo& tempo) noexcept {
  const Rational per_minute =
      tempo.bpm() * note_value_fraction(tempo.beat_unit());
  return (per_minute / Rational(60)).to_double();
}

// Mirrors TempoLane::segment_index_at()'s own linear scan (tempo_lane.cpp)
// rather than depending on it: graphscore_core cannot depend on
// graphscore_domain (ADR 0003). Unlike TempoLane's version, this has no
// upper bound to enforce -- see tempo_curve.hpp's "The lane's final point".
[[nodiscard]] std::optional<std::size_t> locate_segment(
    std::span<const TempoPoint> points, Rational position) {
  if (points.empty() || position < points.front().position)
    return std::nullopt;

  std::optional<std::size_t> governing;
  for (std::size_t index = 0; index < points.size(); ++index) {
    if (points[index].position > position)
      break;
    governing = index;
  }
  return governing;
}

// Cubic Hermite basis functions for the cardinal-spline construction (see
// tempo_curve.hpp's "kSmooth" overview).
[[nodiscard]] double hermite_h00(double t) noexcept {
  return (2.0 * t - 3.0) * t * t + 1.0;
}

[[nodiscard]] double hermite_h10(double t) noexcept {
  return ((t - 2.0) * t + 1.0) * t;
}

[[nodiscard]] double hermite_h01(double t) noexcept {
  return (-2.0 * t + 3.0) * t * t;
}

[[nodiscard]] double hermite_h11(double t) noexcept {
  return (t - 1.0) * t * t;
}

// The kSmooth rate at fraction `t` in [0, 1] through the segment
// points[index] -> points[index + 1]. Precondition: index + 1 <
// points.size() (the caller never invokes this for a trailing,
// next-point-less segment). See tempo_curve.hpp's "kSmooth" and
// "One-sided (lane-boundary) kSmooth segments" overview for the tangent
// construction and the reflected-endpoint convention.
[[nodiscard]] double smooth_rate(std::span<const TempoPoint> points,
                                 std::size_t index, double t) {
  const double r_i =
      normalized_rate_whole_notes_per_second(points[index].tempo);
  const double r_i1 =
      normalized_rate_whole_notes_per_second(points[index + 1].tempo);
  const double r_prev =
      index == 0
          ? (2.0 * r_i - r_i1)
          : normalized_rate_whole_notes_per_second(points[index - 1].tempo);
  const double r_next =
      index + 2 < points.size()
          ? normalized_rate_whole_notes_per_second(points[index + 2].tempo)
          : (2.0 * r_i1 - r_i);

  const double m_i  = kTempoCurveTension * (r_i1 - r_prev);
  const double m_i1 = kTempoCurveTension * (r_next - r_i);

  return hermite_h00(t) * r_i + hermite_h10(t) * m_i + hermite_h01(t) * r_i1 +
         hermite_h11(t) * m_i1;
}

// The rate at fraction `t` in [0, 1) through the segment starting at
// points[index], honoring that point's TempoSegmentKind. Falls back to a
// constant rate when points[index] is the lane's final point (see
// tempo_curve.hpp's "The lane's final point").
[[nodiscard]] double rate_in_segment(std::span<const TempoPoint> points,
                                     std::size_t index, double t) {
  if (index + 1 >= points.size())
    return normalized_rate_whole_notes_per_second(points[index].tempo);

  switch (points[index].segment_kind) {
    case TempoSegmentKind::kStep:
      return normalized_rate_whole_notes_per_second(points[index].tempo);
    case TempoSegmentKind::kLinear: {
      const double r0 =
          normalized_rate_whole_notes_per_second(points[index].tempo);
      const double r1 =
          normalized_rate_whole_notes_per_second(points[index + 1].tempo);
      return r0 + (r1 - r0) * t;
    }
    case TempoSegmentKind::kSmooth:
      return smooth_rate(points, index, t);
  }
  return normalized_rate_whole_notes_per_second(points[index].tempo);
}

// The elapsed-seconds antiderivative F(t) = integral from tau == 0 (the
// segment's own start) to tau == t of
// `Dd / max(rate(tau), kMinNormalizedRate)`, via composite Simpson's rule
// over a fixed kSmoothQuadratureSteps subintervals of [0, t] -- see
// tempo_curve.hpp's "Why kSmooth integrates numerically" overview.
// Anchoring every quadrature at the segment's own start (rather than at
// whatever sub-range a particular call happens to request) is what makes
// integrate_smooth_segment() below exactly additive across an interior
// split: integrate(a, c) == F(c) - F(a) is algebraically identical to
// integrate(a, b) + integrate(b, c) == (F(b) - F(a)) + (F(c) - F(b)), for
// any split point b, up to floating-point non-associativity (at most a few
// ULP), regardless of how narrow or wide [a, b] and [b, c] are individually.
[[nodiscard]] double smooth_antiderivative_at(
    std::span<const TempoPoint> points, std::size_t index, double t,
    double segment_duration_whole_notes) {
  if (t <= 0.0)
    return 0.0;

  const auto reciprocal_rate = [&](double tau) {
    const double rate =
        std::max(smooth_rate(points, index, tau), kMinNormalizedRate);
    return segment_duration_whole_notes / rate;
  };

  const double step = t / static_cast<double>(kSmoothQuadratureSteps);
  double       sum  = reciprocal_rate(0.0) + reciprocal_rate(t);
  for (int k = 1; k < kSmoothQuadratureSteps; ++k) {
    const double tau    = step * static_cast<double>(k);
    const double weight = (k % 2 == 0) ? 2.0 : 4.0;
    sum += weight * reciprocal_rate(tau);
  }
  return sum * step / 3.0;
}

// Elapsed seconds from `ta` to `tb` within one kSmooth segment, as the
// difference of the anchored antiderivative F() above -- see
// smooth_antiderivative_at()'s own comment for why this makes integration
// additive across an interior split of the segment.
[[nodiscard]] double integrate_smooth_segment(
    std::span<const TempoPoint> points, std::size_t index, double ta, double tb,
    double segment_duration_whole_notes) {
  return smooth_antiderivative_at(points, index, tb,
                                  segment_duration_whole_notes) -
         smooth_antiderivative_at(points, index, ta,
                                  segment_duration_whole_notes);
}

// Elapsed seconds between `local_from` and `local_to`, both within the
// segment governed by points[index] (or, if points[index] is the lane's
// final point, both anywhere at or past it -- see tempo_curve.hpp's "The
// lane's final point"). See tempo_curve.hpp's "Integration" overview for
// the closed-form kStep/kLinear derivations.
[[nodiscard]] double integrate_segment(std::span<const TempoPoint> points,
                                       std::size_t index, Rational local_from,
                                       Rational local_to) {
  const double delta_position = (local_to - local_from).to_double();

  if (index + 1 >= points.size()) {
    const double rate =
        normalized_rate_whole_notes_per_second(points[index].tempo);
    return delta_position / rate;
  }

  switch (points[index].segment_kind) {
    case TempoSegmentKind::kStep: {
      const double rate =
          normalized_rate_whole_notes_per_second(points[index].tempo);
      return delta_position / rate;
    }
    case TempoSegmentKind::kLinear: {
      const Rational duration =
          points[index + 1].position - points[index].position;
      const double r0 =
          normalized_rate_whole_notes_per_second(points[index].tempo);
      const double r1 =
          normalized_rate_whole_notes_per_second(points[index + 1].tempo);
      if (r0 == r1)
        return delta_position / r0;

      const double duration_d = duration.to_double();
      const double ta =
          ((local_from - points[index].position) / duration).to_double();
      const double tb =
          ((local_to - points[index].position) / duration).to_double();
      const double ra                   = r0 + (r1 - r0) * ta;
      const double rb                   = r0 + (r1 - r0) * tb;
      const double slope_per_whole_note = (r1 - r0) / duration_d;
      return std::log(rb / ra) / slope_per_whole_note;
    }
    case TempoSegmentKind::kSmooth: {
      const Rational duration =
          points[index + 1].position - points[index].position;
      const double duration_d = duration.to_double();
      const double ta =
          ((local_from - points[index].position) / duration).to_double();
      const double tb =
          ((local_to - points[index].position) / duration).to_double();
      return integrate_smooth_segment(points, index, ta, tb, duration_d);
    }
  }
  return delta_position /
         normalized_rate_whole_notes_per_second(points[index].tempo);
}

// Builds a Rational at a fixed kInversionQuantizationDenominator-wide grid
// from a double position -- see tempo_curve.hpp's "Why quantize rather
// than bisect in Rational space".
[[nodiscard]] Rational quantize_position(double value) {
  const auto numerator = static_cast<std::int64_t>(std::llround(
      value * static_cast<double>(kInversionQuantizationDenominator)));
  // kInversionQuantizationDenominator is a fixed non-zero constant, and
  // Rational::create fails only on a zero denominator, so this always engages.
  const std::optional<Rational> quantized =
      Rational::create(numerator, kInversionQuantizationDenominator);
  assert(quantized.has_value());
  return *quantized;
}

[[nodiscard]] Rational clamp_position(Rational value, Rational lo,
                                      Rational hi) {
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return value;
}

}  // namespace

std::optional<double> tempo_rate_at(std::span<const TempoPoint> points,
                                    Rational                    position) {
  const std::optional<std::size_t> index = locate_segment(points, position);
  if (!index.has_value())
    return std::nullopt;

  if (*index + 1 >= points.size())
    return normalized_rate_whole_notes_per_second(points[*index].tempo);

  const Rational duration =
      points[*index + 1].position - points[*index].position;
  const Rational local = position - points[*index].position;
  const double   t     = (local / duration).to_double();
  return rate_in_segment(points, *index, t);
}

std::optional<double> integrate_elapsed_seconds(
    std::span<const TempoPoint> points, Rational from, Rational to) {
  const std::optional<std::size_t> starting_index =
      locate_segment(points, from);
  if (!starting_index.has_value() || from > to)
    return std::nullopt;

  double   total  = 0.0;
  Rational cursor = from;
  while (cursor < to) {
    // cursor never falls below from (it only advances toward to), and
    // starting_index proved from is within the curve, so locate_segment
    // always engages here.
    const std::optional<std::size_t> segment = locate_segment(points, cursor);
    assert(segment.has_value());
    const std::size_t index = *segment;
    const Rational    next_boundary =
        index + 1 < points.size() ? points[index + 1].position : to;
    const Rational segment_end = next_boundary < to ? next_boundary : to;

    total += integrate_segment(points, index, cursor, segment_end);
    cursor = segment_end;
  }
  return total;
}

std::optional<Rational> invert_elapsed_seconds(
    std::span<const TempoPoint> points, Rational from, Rational curve_end,
    double elapsed_seconds) {
  if (points.empty() || from < points.front().position || curve_end <= from)
    return std::nullopt;
  if (elapsed_seconds <= 0.0)
    return from;

  const std::optional<double> total_span_seconds =
      integrate_elapsed_seconds(points, from, curve_end);
  if (!total_span_seconds.has_value())
    return std::nullopt;
  if (elapsed_seconds >= *total_span_seconds)
    return curve_end;

  double   lo  = from.to_double();
  double   hi  = curve_end.to_double();
  Rational mid = from;
  for (int iteration = 0; iteration < kTempoInversionIterations; ++iteration) {
    const double mid_d = lo + (hi - lo) * 0.5;
    mid = clamp_position(quantize_position(mid_d), from, curve_end);

    // mid is clamped to [from, curve_end]; the total-span integration above
    // already proved this range integrates, so this always engages.
    const std::optional<double> elapsed =
        integrate_elapsed_seconds(points, from, mid);
    assert(elapsed.has_value());
    const double elapsed_to_mid = *elapsed;
    if (elapsed_to_mid < elapsed_seconds) {
      lo = mid_d;
    } else {
      hi = mid_d;
    }
  }
  return mid;
}

std::int64_t round_to_sample_count(double       elapsed_seconds,
                                   std::int64_t sample_rate_hz) {
  if (sample_rate_hz <= 0)
    return 0;
  if (!std::isfinite(elapsed_seconds))
    return 0;

  const double scaled = elapsed_seconds * static_cast<double>(sample_rate_hz);

  constexpr double kInt64MaxAsDouble =
      static_cast<double>(std::numeric_limits<std::int64_t>::max());
  constexpr double kInt64MinAsDouble =
      static_cast<double>(std::numeric_limits<std::int64_t>::min());
  if (scaled >= kInt64MaxAsDouble)
    return std::numeric_limits<std::int64_t>::max();
  if (scaled <= kInt64MinAsDouble)
    return std::numeric_limits<std::int64_t>::min();

  const double floor_value = std::floor(scaled);
  const double fractional  = scaled - floor_value;
  const auto   floor_i     = static_cast<std::int64_t>(floor_value);

  if (fractional < 0.5)
    return floor_i;
  if (fractional > 0.5)
    return floor_i + 1;
  return (floor_i % 2 == 0) ? floor_i : floor_i + 1;
}

}  // namespace graphscore
