// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

#include <optional>

namespace graphscore::writer_app {

struct DefaultProject {
  graphscore::Project        project;
  graphscore::NodeId         node_id;
  graphscore::TrackId        track_id;
  graphscore::StaveId        stave_id;
  graphscore::NotationLayout layout;
};

[[nodiscard]] std::optional<DefaultProject> build_default_project(
    const graphscore::GlyphMetrics& metrics);

// Stub glyph metrics producing fixed bounds — used only for headless tests
// that must not load a real font.
class SelfTestMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return graphscore::GlyphMetricsValue{
        graphscore::NotationRect{-staff_space * 0.25, -staff_space * 0.5,
                                 staff_space * 1.5, staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

}  // namespace graphscore::writer_app
