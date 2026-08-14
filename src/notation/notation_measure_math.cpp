// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/measure_map.hpp>
#include <graphscore/notation/notation_layout.hpp>

#include "layout_index.hpp"
#include "measure_math.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace graphscore {

[[nodiscard]] double glyph_extent(const GlyphMetrics& metrics, char32_t glyph,
                                  double staff_space) {
  const GlyphMetricsValue value = metrics.glyph_metrics(glyph, staff_space);
  return std::max(value.advance, value.bounds.x + value.bounds.width) -
         std::min(0.0, value.bounds.x);
}

void append_fragment(NotationLayout& output, const SystemFragment& fragment) {
  output.systems.push_back(fragment.system);
  output.commands.insert(output.commands.end(), fragment.commands.begin(),
                         fragment.commands.end());
  output.hit_regions.insert(output.hit_regions.end(),
                            fragment.hit_regions.begin(),
                            fragment.hit_regions.end());
  output.diagnostics.insert(output.diagnostics.end(),
                            fragment.diagnostics.begin(),
                            fragment.diagnostics.end());
}

[[nodiscard]] double compute_measure_width(
    std::size_t index, const MeasureMap& measures,
    const LayoutIndex& layout_index, const GlyphMetrics& metrics,
    const NotationLayoutOptions& options) {
  double rhythmic_width = 0.0;
  for (const IndexedStaff& staff : layout_index.staves) {
    for (const IndexedVoice& voice : staff.voices) {
      double voice_width = 0.0;
      for (const IndexedEvent& event : voice.measures[index]) {
        voice_width += event.spacing;
      }
      rhythmic_width = std::max(rhythmic_width, voice_width);
    }
  }
  const Measure& measure = measures.measure(index);
  const double   clef_width =
      std::max({glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kGClef),
                             options.staff_space),
                glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kCClef),
                             options.staff_space),
                glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kFClef),
                             options.staff_space),
                options.staff_space * 3.0});
  const double accidental_width = std::max(
      glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kAccidentalSharp),
                   options.staff_space),
      glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kAccidentalFlat),
                   options.staff_space));
  const auto digits_width = [&](std::uint16_t value) {
    double result = 0.0;
    for (const char digit : std::to_string(value)) {
      result +=
          glyph_extent(metrics, kTimeZero + static_cast<char32_t>(digit - '0'),
                       options.staff_space);
    }
    return result;
  };
  const double signature_width =
      options.staff_space + clef_width +
      accidental_width *
          (std::abs(static_cast<int>(measure.key_signature.fifths())) +
           (index > 0 && measure.key_signature !=
                             measures.measure(index - 1).key_signature
                ? std::abs(static_cast<int>(
                      measures.measure(index - 1).key_signature.fifths()))
                : 0)) +
      std::max(digits_width(measure.time_signature.numerator()),
               digits_width(measure.time_signature.denominator())) +
      options.staff_space;
  return std::max(
      {options.minimum_measure_width,
       options.whole_note_spacing * measures.measure_length(index).to_double(),
       signature_width + rhythmic_width + options.staff_space * 2.0});
}

[[nodiscard]] std::vector<double> measure_widths(
    const MeasureMap& measures, const LayoutIndex& layout_index,
    const GlyphMetrics& metrics, const NotationLayoutOptions& options) {
  std::vector<double> widths;
  widths.reserve(measures.measure_count());
  for (std::size_t index = 0; index < measures.measure_count(); ++index) {
    widths.push_back(
        compute_measure_width(index, measures, layout_index, metrics, options));
  }
  return widths;
}

[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> system_ranges(
    const std::vector<double>& widths, double content_width) {
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  std::size_t                                      first = 0;
  while (first < widths.size()) {
    std::size_t end  = first;
    double      used = 0.0;
    while (end < widths.size() &&
           (end == first || used + widths[end] <= content_width)) {
      used += widths[end];
      ++end;
    }
    ranges.emplace_back(first, end);
    first = end;
  }
  return ranges;
}

[[nodiscard]] double event_y(const Voice& voice, double staff_top,
                             double staff_space) noexcept {
  constexpr double kVoiceOffsets[] = {2.0, 1.5, 2.5, 1.0};
  return staff_top + kVoiceOffsets[voice.index() - Voice::kMin] * staff_space;
}

namespace {

// The horizontal space `position_x`/`time_at_x` reserve at the head of a
// measure for its clef/key/time-signature glyphs, before the rhythmic span
// begins. Shared so the pointer-entry preview's x<->time mapping stays
// exactly reproducible from the layout it reads, never a separate
// approximation of it.
[[nodiscard]] double measure_leading_width(const MeasureMap& measures,
                                           std::size_t       measure_index,
                                           double            measure_width,
                                           double staff_space) noexcept {
  const Measure& measure       = measures.measure(measure_index);
  const double   digit_columns = static_cast<double>(
      std::max(std::to_string(measure.time_signature.numerator()).size(),
                 std::to_string(measure.time_signature.denominator()).size()));
  return std::min(
      measure_width - staff_space * 2.0,
      staff_space *
          (6.5 +
           1.5 * (std::abs(static_cast<int>(measure.key_signature.fifths())) +
                  (measure_index > 0 &&
                           measure.key_signature !=
                               measures.measure(measure_index - 1).key_signature
                       ? std::abs(static_cast<int>(
                             measures.measure(measure_index - 1)
                                 .key_signature.fifths()))
                       : 0)) +
           1.5 * digit_columns));
}

}  // namespace

[[nodiscard]] double position_x(const MeasureMap& measures,
                                std::size_t measure_index, double measure_width,
                                Rational position, double measure_x,
                                double staff_space) noexcept {
  const Rational within = position - measures.measure_start(measure_index);
  const double   fraction =
      within.to_double() / measures.measure_length(measure_index).to_double();
  const double leading = measure_leading_width(measures, measure_index,
                                               measure_width, staff_space);
  const double rhythmic_width =
      std::max(staff_space, measure_width - leading - staff_space);
  return measure_x + leading + rhythmic_width * fraction;
}

[[nodiscard]] double position_x(const MeasureMap&          measures,
                                const std::vector<double>& widths,
                                std::size_t measure_index, Rational position,
                                double measure_x, double staff_space) noexcept {
  return position_x(measures, measure_index, widths[measure_index], position,
                    measure_x, staff_space);
}

// The exact inverse of position_x(): the musical time at horizontal position
// `x` within the measure at `measure_index`, clamped to the measure's own
// rhythmic span (never before/after it, matching position_x's own domain).
[[nodiscard]] double time_at_x(const MeasureMap& measures,
                               std::size_t measure_index, double x,
                               double measure_x, double measure_width,
                               double staff_space) noexcept {
  const double leading = measure_leading_width(measures, measure_index,
                                               measure_width, staff_space);
  const double rhythmic_width =
      std::max(staff_space, measure_width - leading - staff_space);
  const double fraction =
      rhythmic_width > 0.0
          ? std::clamp((x - measure_x - leading) / rhythmic_width, 0.0, 1.0)
          : 0.0;
  return measures.measure_start(measure_index).to_double() +
         fraction * measures.measure_length(measure_index).to_double();
}

}  // namespace graphscore
