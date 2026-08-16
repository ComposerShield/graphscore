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
class NodeTimeline;
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
// The deterministic horizontal width of a node's pickdown region, derived
// from its duration relative to the final main measure's own length under
// that measure's meter (the pickdown inherits the final main measure's meter;
// docs/plan/README.md), but never narrower than pickdown_minimum_width() so a
// valid tiny duration still fits the double transition boundary and at least
// one notehead column. Returns 0.0 when the timeline has no pickdown or the
// supplied final measure width is not strictly positive.
[[nodiscard]] double pickdown_region_width(const NodeTimeline&,
                                           double final_measure_width,
                                           double staff_space);
// The deterministic visual inset reserved at each end of the pickdown region's
// content span, so boundary-onset glyph/hit geometry never overlaps the double
// transition boundary and node-end content stays inside the represented tail.
[[nodiscard]] double pickdown_content_inset(double staff_space) noexcept;
// The deterministic minimum visual width of a pickdown region: strictly wider
// than the double transition boundary (second stroke at 0.35 staff-spaces) and
// wide enough for a notehead column between the two content insets.
[[nodiscard]] double pickdown_minimum_width(double staff_space) noexcept;
// Maps a node-local pickdown position (in [boundary, node_end)) linearly onto
// the content span [boundary_x + inset, boundary_x + pickdown_width - inset],
// the same node-local coordinate system the main region uses. `inset` is the
// content-aware inset already baked into `pickdown_width` by the caller (see
// layout_internal), so a boundary-onset event's complete geometry stays inside
// the transition-to-node-end area. Returns boundary_x when there is no
// pickdown or pickdown_width is not positive.
[[nodiscard]] double pickdown_position_x(const NodeTimeline&, Rational, double,
                                         double, double inset) noexcept;
}  // namespace graphscore
