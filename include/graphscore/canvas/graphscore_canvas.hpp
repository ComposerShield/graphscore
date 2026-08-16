// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/domain/graph_position.hpp>

#include <optional>

namespace graphscore {

struct ViewportPosition {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool operator==(const ViewportPosition&) const = default;
};

class ViewportTransform {
 public:
  constexpr ViewportTransform() = default;

  [[nodiscard]] constexpr const GraphPosition& world_anchor() const noexcept {
    return world_anchor_;
  }

  [[nodiscard]] constexpr const ViewportPosition& viewport_anchor()
      const noexcept {
    return viewport_anchor_;
  }

  [[nodiscard]] constexpr double zoom() const noexcept { return zoom_; }

  [[nodiscard]] std::optional<ViewportPosition> to_viewport(
      GraphPosition world_position) const noexcept;
  [[nodiscard]] std::optional<GraphPosition> to_world(
      ViewportPosition viewport_position) const noexcept;

  [[nodiscard]] bool set_anchor(GraphPosition    world_anchor,
                                ViewportPosition viewport_anchor) noexcept;
  [[nodiscard]] bool pan_by(ViewportPosition viewport_delta) noexcept;
  [[nodiscard]] bool zoom_to(double           zoom,
                             ViewportPosition focal_point) noexcept;
  [[nodiscard]] bool zoom_by(double           factor,
                             ViewportPosition focal_point) noexcept;

 private:
  GraphPosition    world_anchor_;
  ViewportPosition viewport_anchor_;
  double           zoom_ = 1.0;
};

class Canvas {
 public:
  Canvas() = default;
};

[[nodiscard]] int canvas_version() noexcept;

}  // namespace graphscore
