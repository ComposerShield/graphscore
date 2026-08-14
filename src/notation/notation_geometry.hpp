// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cmath>

#include <graphscore/notation/notation_types.hpp>

namespace graphscore {

[[nodiscard]] inline bool finite_rect(const NotationRect& rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height) &&
         rect.width >= 0.0 && rect.height >= 0.0 &&
         std::isfinite(rect.x + rect.width) &&
         std::isfinite(rect.y + rect.height);
}

[[nodiscard]] inline bool finite_point(NotationPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] inline bool bounded_point(NotationPoint point) noexcept {
  return finite_point(point) &&
         std::abs(point.x) <= NotationLayoutOptions::kMaximumCoordinate &&
         std::abs(point.y) <= NotationLayoutOptions::kMaximumCoordinate;
}

[[nodiscard]] inline bool bounded_rect(const NotationRect& rect) noexcept {
  return finite_rect(rect) && bounded_point({rect.x, rect.y}) &&
         bounded_point({rect.x + rect.width, rect.y + rect.height});
}

[[nodiscard]] inline bool valid_metric(
    const GlyphMetricsValue& metric) noexcept {
  return finite_rect(metric.bounds) && std::isfinite(metric.advance) &&
         metric.advance >= 0.0;
}

[[nodiscard]] inline NotationRect translated(const NotationRect& rect,
                                             NotationPoint origin) noexcept {
  return {origin.x + rect.x, origin.y + rect.y, rect.width, rect.height};
}

[[nodiscard]] bool finite_command(const NotationCommand& command);

}  // namespace graphscore
