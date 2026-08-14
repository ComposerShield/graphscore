// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/notation/notation_types.hpp>

#include "notation_geometry.hpp"
#include "notation_ids.hpp"

namespace graphscore {

class NodeTimeline;

struct LayoutBuilder {
  const NodeTimeline&          timeline;
  const GlyphMetrics&          metrics;
  const NotationLayoutOptions& options;
  NotationLayout               output;
  NotationLayoutError          error = NotationLayoutError::kNone;

  [[nodiscard]] std::optional<double> add_glyph(
      const NotationId& id, char32_t code_point, NotationPoint origin,
      std::optional<NotationId> semantic_id = std::nullopt,
      double                    scale       = 1.0) {
    const double            glyph_space = options.staff_space * scale;
    const GlyphMetricsValue glyph =
        metrics.glyph_metrics(code_point, glyph_space);
    if (!valid_metric(glyph)) {
      error = NotationLayoutError::kInvalidMetrics;
      return std::nullopt;
    }
    const NotationRect hit_bounds = translated(glyph.bounds, origin);
    if (!finite_point(origin) || !finite_rect(hit_bounds)) {
      error = NotationLayoutError::kInvalidGeometry;
      return std::nullopt;
    }
    output.commands.emplace_back(
        GlyphCommand{id, code_point, origin, glyph_space});
    if (semantic_id.has_value()) {
      output.hit_regions.push_back(
          HitRegion{make_id(id.value, "hit"), *semantic_id, HitRole::kEvent,
                    hit_bounds, kHitPriorityGlyph, std::nullopt, std::nullopt});
    }
    return glyph.advance;
  }

  void add_line(NotationId id, NotationPoint from, NotationPoint to,
                double width) {
    output.commands.emplace_back(LineCommand{std::move(id), from, to, width});
  }

  void add_path(NotationId id, std::vector<PathElement> elements, double width,
                bool filled = false) {
    output.commands.emplace_back(
        PathCommand{std::move(id), std::move(elements), width, filled});
  }

  void add_hit(const NotationId& id, const NotationId& semantic_id,
               HitRole role, NotationRect bounds,
               int priority = kHitPriorityGlyph) {
    output.hit_regions.push_back(HitRegion{make_id(id.value, "hit"),
                                           semantic_id, role, bounds, priority,
                                           std::nullopt, std::nullopt});
  }
};

}  // namespace graphscore
