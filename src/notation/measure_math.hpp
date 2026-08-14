// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <graphscore/core/rational.hpp>
#include <graphscore/domain/notation_event.hpp>

namespace graphscore {
class GlyphMetrics;
class MeasureMap;
class Voice;
struct LayoutIndex;
struct NotationLayoutOptions;
[[nodiscard]] double glyph_extent(const GlyphMetrics&, char32_t, double);
[[nodiscard]] double compute_measure_width(std::size_t, const MeasureMap&,
                                           const LayoutIndex&,
                                           const GlyphMetrics&,
                                           const NotationLayoutOptions&);
[[nodiscard]] std::vector<double> measure_widths(const MeasureMap&,
                                                 const LayoutIndex&,
                                                 const GlyphMetrics&,
                                                 const NotationLayoutOptions&);
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> system_ranges(
    const std::vector<double>&, double);
[[nodiscard]] double event_y(const Voice&, double, double) noexcept;
[[nodiscard]] double position_x(const MeasureMap&, std::size_t, double,
                                Rational, double, double) noexcept;
[[nodiscard]] double position_x(const MeasureMap&, const std::vector<double>&,
                                std::size_t, Rational, double, double) noexcept;
[[nodiscard]] double time_at_x(const MeasureMap&, std::size_t, double, double,
                               double, double) noexcept;
}  // namespace graphscore
