// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

#include <cmath>
#include <optional>

namespace graphscore {
namespace {
constexpr int kCanvasVersion = 1;

[[nodiscard]] bool is_finite(GraphPosition position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y);
}

[[nodiscard]] bool is_finite(ViewportPosition position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y);
}

[[nodiscard]] std::optional<double> map_forward_raw(
    double value, double input_anchor, double scale,
    double output_anchor) noexcept {
  const double delta = value - input_anchor;
  if (!std::isfinite(delta)) {
    return std::nullopt;
  }
  const double mapped = std::fma(delta, scale, output_anchor);
  if (!std::isfinite(mapped)) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] std::optional<double> map_inverse_raw(
    double value, double input_anchor, double scale,
    double output_anchor) noexcept {
  const double delta = value - input_anchor;
  if (!std::isfinite(delta)) {
    return std::nullopt;
  }
  const double scaled = delta / scale;
  if (!std::isfinite(scaled)) {
    return std::nullopt;
  }
  const double mapped = scaled + output_anchor;
  if (!std::isfinite(mapped)) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] std::optional<double> map_forward(double value,
                                                double input_anchor,
                                                double scale,
                                                double output_anchor) noexcept {
  const auto mapped =
      map_forward_raw(value, input_anchor, scale, output_anchor);
  if (!mapped || (value != input_anchor && *mapped == output_anchor)) {
    return std::nullopt;
  }
  const auto round_trip =
      map_inverse_raw(*mapped, output_anchor, scale, input_anchor);
  if (!round_trip || *round_trip != value) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] std::optional<double> map_inverse(double value,
                                                double input_anchor,
                                                double scale,
                                                double output_anchor) noexcept {
  const auto mapped =
      map_inverse_raw(value, input_anchor, scale, output_anchor);
  if (!mapped || (value != input_anchor && *mapped == output_anchor)) {
    return std::nullopt;
  }
  const auto round_trip =
      map_forward_raw(*mapped, output_anchor, scale, input_anchor);
  if (!round_trip || *round_trip != value) {
    return std::nullopt;
  }
  return mapped;
}
}  // namespace

std::optional<ViewportPosition> ViewportTransform::to_viewport(
    GraphPosition world_position) const noexcept {
  if (!is_finite(world_position)) {
    return std::nullopt;
  }
  const auto x =
      map_forward(world_position.x, world_anchor_.x, zoom_, viewport_anchor_.x);
  const auto y =
      map_forward(world_position.y, world_anchor_.y, zoom_, viewport_anchor_.y);
  if (!x || !y) {
    return std::nullopt;
  }
  return ViewportPosition{*x, *y};
}

std::optional<GraphPosition> ViewportTransform::to_world(
    ViewportPosition viewport_position) const noexcept {
  if (!is_finite(viewport_position)) {
    return std::nullopt;
  }
  const auto x = map_inverse(viewport_position.x, viewport_anchor_.x, zoom_,
                             world_anchor_.x);
  const auto y = map_inverse(viewport_position.y, viewport_anchor_.y, zoom_,
                             world_anchor_.y);
  if (!x || !y) {
    return std::nullopt;
  }
  return GraphPosition{*x, *y};
}

bool ViewportTransform::set_anchor(GraphPosition    world_anchor,
                                   ViewportPosition viewport_anchor) noexcept {
  if (!is_finite(world_anchor) || !is_finite(viewport_anchor)) {
    return false;
  }
  world_anchor_    = world_anchor;
  viewport_anchor_ = viewport_anchor;
  return true;
}

bool ViewportTransform::pan_by(ViewportPosition viewport_delta) noexcept {
  if (!is_finite(viewport_delta)) {
    return false;
  }
  const ViewportPosition translated{viewport_anchor_.x + viewport_delta.x,
                                    viewport_anchor_.y + viewport_delta.y};
  if (!is_finite(translated) ||
      (viewport_delta.x != 0.0 && translated.x == viewport_anchor_.x) ||
      (viewport_delta.y != 0.0 && translated.y == viewport_anchor_.y)) {
    return false;
  }
  viewport_anchor_ = translated;
  return true;
}

bool ViewportTransform::zoom_to(double           zoom,
                                ViewportPosition focal_point) noexcept {
  if (!std::isfinite(zoom) || zoom <= 0.0 || !is_finite(focal_point)) {
    return false;
  }
  if (zoom == zoom_) {
    return true;
  }
  const auto focal_world = to_world(focal_point);
  if (!focal_world) {
    return false;
  }
  world_anchor_    = *focal_world;
  viewport_anchor_ = focal_point;
  zoom_            = zoom;
  return true;
}

bool ViewportTransform::zoom_by(double           factor,
                                ViewportPosition focal_point) noexcept {
  if (!std::isfinite(factor) || factor <= 0.0) {
    return false;
  }
  const double new_zoom = zoom_ * factor;
  if (!std::isfinite(new_zoom) || new_zoom <= 0.0) {
    return false;
  }
  if (factor != 1.0 && new_zoom == zoom_) {
    return false;
  }
  return zoom_to(new_zoom, focal_point);
}

int canvas_version() noexcept {
  return kCanvasVersion;
}
}  // namespace graphscore
