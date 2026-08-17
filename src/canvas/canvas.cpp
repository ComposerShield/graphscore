// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/validation_service.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace graphscore {

static_assert(!std::is_copy_constructible_v<TrackpadGestureController>);
static_assert(!std::is_move_constructible_v<TrackpadGestureController>);
static_assert(!std::is_copy_assignable_v<TrackpadGestureController>);
static_assert(!std::is_move_assignable_v<TrackpadGestureController>);
static_assert(!std::is_copy_constructible_v<CanvasNavigationController>);
static_assert(!std::is_move_constructible_v<CanvasNavigationController>);

namespace {
constexpr int kCanvasVersion = 1;

[[nodiscard]] bool is_finite(GraphPosition position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y);
}

[[nodiscard]] bool is_finite(ViewportPosition position) noexcept {
  return std::isfinite(position.x) && std::isfinite(position.y);
}

[[nodiscard]] bool diagnostic_applies_to_node(const Diagnostic& diagnostic,
                                              NodeId node_id) noexcept {
  const NodeId* const subject = std::get_if<NodeId>(&diagnostic.entity);
  return (subject != nullptr && *subject == node_id) ||
         diagnostic.node == node_id;
}

[[nodiscard]] CanvasNodeValidationState validation_state_for_node(
    const ValidationReport& report, NodeId node_id) noexcept {
  CanvasNodeValidationState state = CanvasNodeValidationState::kValid;
  for (const Diagnostic& diagnostic : report.diagnostics) {
    if (!diagnostic_applies_to_node(diagnostic, node_id)) {
      continue;
    }
    if (diagnostic.severity == DiagnosticSeverity::kError) {
      return CanvasNodeValidationState::kError;
    }
    state = CanvasNodeValidationState::kWarning;
  }
  return state;
}

[[nodiscard]] CanvasNodeGeometry node_geometry(
    GraphPosition                        position,
    const std::optional<NotationLayout>& layout) noexcept {
  const double notation_width = layout.has_value() ? layout->bounds.width : 0.0;
  const double content_height =
      layout.has_value() ? layout->bounds.height
                         : CanvasNodeGeometry::kFallbackContentHeight;
  const double width =
      std::max(CanvasNodeGeometry::kMinimumWidth, notation_width);

  return CanvasNodeGeometry{
      {position, width, CanvasNodeGeometry::kHeaderHeight + content_height},
      {0.0, 0.0, width, CanvasNodeGeometry::kHeaderHeight},
      {0.0, CanvasNodeGeometry::kHeaderHeight, width, content_height},
      {0.0, CanvasNodeGeometry::kHeaderHeight, notation_width,
       layout.has_value() ? layout->bounds.height : 0.0}};
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

// A forward→inverse (or inverse→forward) round trip is not bit-exact for
// most zoom scales: `fma(delta, scale, anchor)` forward composed with the
// `(mapped - anchor)/scale + anchor` inverse introduces rounding at the
// magnitude of the operands. Requiring exact equality here therefore rejected
// ordinary pinch-zoom increments — `to_viewport` returned nullopt and the
// writer's render pass skipped the notation surface, which is the observed
// flicker.
//
// The acceptance criterion derives its budget from `original` and `recovered`
// alone — never from the mapping's anchors or scale, and never as a percentage
// of the value. Both of those alternatives admit a genuine many-to-one
// collapse because they grow without bound as the operands grow:
//
//   * A nonzero value must survive within a small ULP budget of its own
//     magnitude. A relative tolerance (one part per billion) admits millions
//     of ULPs: at input_anchor 2^622, scale 1, output_anchor 0 the values
//     2^600 and 2^600 + 2^568 forward-map to the same image, and the second
//     recovers with relative error ~2.33e-10 — comfortably inside 1e-9 — even
//     though it lost 2^20 ULPs. The round-off of an otherwise-invertible
//     mapping is a handful of ULPs (measured at most ~600 across the ordinary
//     pinch-zoom sweep), while a collapse absorbs value bits into a far larger
//     magnitude, so its recovery error is millions to quadrillions of ULPs and
//     fails this budget no matter how large the anchors are.
//
//   * Zero is exactly representable and `0 - input_anchor` is exact, but a
//     *nonzero* value can still be absorbed next to it and forward-map to the
//     same image: at input_anchor 3*2^1000, scale 2^-100, output_anchor 2^954
//     both 0 and -2^1000 map identically and inverse-recover as -2^1000. Zero
//     is therefore admitted only against a small absolute arithmetic-noise
//     floor — enough for the ordinary canvas-origin round-trip, whose operands
//     are window-sized and land within ~1e-12 of zero — never against a floor
//     scaled by the anchors, which that counterexample inflates past DBL_MAX
//     to infinity. The floor is symmetric around zero, so a recovered ±0 is
//     accepted for either sign of zero (`-0.0 == 0.0` is the zero case).
//
// Non-finite operands are rejected before this point (the finite checks in the
// raw helpers and the exact-absorption check in the callers), so `original`
// and `recovered` are always finite. Their difference can still overflow to
// ±inf — e.g. a sign flip at DBL_MAX — but ±inf fails any finite budget, so
// the comparison below is well-defined and correctly rejects it.
[[nodiscard]] bool round_trip_acceptable(double original,
                                         double recovered) noexcept {
  if (original == 0.0) {
    constexpr double kZeroNoiseFloor = 1e-9;
    return std::abs(recovered) <= kZeroNoiseFloor;
  }
  // ULP budget: 2^12 ULPs of the larger of the two magnitudes. This is ~6.7x
  // the measured ordinary-path maximum (~600 ULPs), yet ~2.4 orders of
  // magnitude below the ~1e6-ULP collapse the large-value counterexample
  // produces (and ~12 orders below the value-1-collapses-to-0 case), so it
  // separates faithful round-trips from many-to-one collapse without ever
  // depending on the anchors or growing with the value.
  constexpr double kUlpBudget = 4096.0;
  const double magnitude = std::max(std::abs(original), std::abs(recovered));

  // One finite ULP at `magnitude`. The successor spacing
  // (nextafter(magnitude, +∞) − magnitude) is finite for every magnitude
  // except DBL_MAX, whose successor is infinity; fall back to the
  // predecessor-toward-zero spacing there, which is DBL_MAX's true ULP (2^971).
  // A non-finite ULP here would make the budget infinite and admit any
  // recovery error at ±DBL_MAX.
  const double successor =
      std::nextafter(magnitude, std::numeric_limits<double>::infinity());
  const double ulp = std::isfinite(successor)
                         ? successor - magnitude
                         : magnitude - std::nextafter(magnitude, 0.0);

  // The budget is finite by construction: ulp ≤ 2^971 and kUlpBudget = 2^12,
  // so the product is ≤ 2^983 and can never overflow. Comparing the (possibly
  // ±inf) error against it is therefore always well-defined.
  return std::abs(recovered - original) <= kUlpBudget * ulp;
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
  if (!round_trip) {
    return std::nullopt;
  }
  if (!round_trip_acceptable(value, *round_trip)) {
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
  if (!round_trip) {
    return std::nullopt;
  }
  if (!round_trip_acceptable(value, *round_trip)) {
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

TrackpadGestureController::TrackpadGestureController(
    ViewportTransform& transform) noexcept
    : transform_(transform) {}

bool TrackpadGestureController::pan(ScrollDelta delta) noexcept {
  return transform_.pan_by(ViewportPosition{delta.x, delta.y});
}

bool TrackpadGestureController::pinch(PinchUpdate update) noexcept {
  if (!std::isfinite(update.scale) || update.scale <= 0.0) {
    return false;
  }
  if (update.focal_point.has_value() && !is_finite(*update.focal_point)) {
    return false;
  }
  const auto focal = resolve_focal(update);
  if (!focal.has_value() || !is_finite(*focal)) {
    return false;
  }
  return transform_.zoom_by(update.scale, *focal);
}

bool TrackpadGestureController::finger_down(FingerContact finger) noexcept {
  if (!is_finite(finger.position)) {
    return false;
  }
  for (auto& slot : fingers_) {
    if (slot.has_value() && slot->finger_id == finger.finger_id) {
      slot = finger;
      return true;
    }
  }
  for (auto& slot : fingers_) {
    if (!slot.has_value()) {
      slot = finger;
      return true;
    }
  }
  return false;
}

bool TrackpadGestureController::finger_move(FingerContact finger) noexcept {
  if (!is_finite(finger.position)) {
    return false;
  }
  for (auto& slot : fingers_) {
    if (slot.has_value() && slot->finger_id == finger.finger_id) {
      slot->position = finger.position;
      return true;
    }
  }
  return false;
}

void TrackpadGestureController::finger_up(std::uint64_t finger_id) noexcept {
  for (auto& slot : fingers_) {
    if (slot.has_value() && slot->finger_id == finger_id) {
      slot.reset();
      return;
    }
  }
}

void TrackpadGestureController::cancel_tracking() noexcept {
  fingers_[0].reset();
  fingers_[1].reset();
}

void TrackpadGestureController::set_window_center(
    ViewportPosition center) noexcept {
  window_center_ = center;
}

ViewportPosition TrackpadGestureController::window_center() const noexcept {
  return window_center_;
}

std::optional<ViewportPosition> TrackpadGestureController::active_centroid()
    const noexcept {
  if (!fingers_[0].has_value() || !fingers_[1].has_value()) {
    return std::nullopt;
  }
  return ViewportPosition{
      (fingers_[0]->position.x + fingers_[1]->position.x) / 2.0,
      (fingers_[0]->position.y + fingers_[1]->position.y) / 2.0};
}

std::size_t TrackpadGestureController::tracked_finger_count() const noexcept {
  return (fingers_[0].has_value() ? 1U : 0U) +
         (fingers_[1].has_value() ? 1U : 0U);
}

std::optional<ViewportPosition> TrackpadGestureController::resolve_focal(
    const PinchUpdate& update) const noexcept {
  if (update.focal_point.has_value()) {
    return update.focal_point;
  }
  if (const auto centroid = active_centroid(); centroid.has_value()) {
    return centroid;
  }
  return window_center_;
}

CanvasNavigationController::CanvasNavigationController(
    ViewportTransform& transform) noexcept
    : transform_(transform) {}

bool CanvasNavigationController::wheel_pan(ScrollDelta delta) noexcept {
  return transform_.pan_by({delta.x, delta.y});
}

bool CanvasNavigationController::wheel_zoom(
    double delta_y, ViewportPosition focal_point) noexcept {
  if (!std::isfinite(delta_y)) {
    return false;
  }
  const double factor = std::pow(kWheelZoomStepPerUnit, delta_y);
  return transform_.zoom_by(factor, focal_point);
}

bool CanvasNavigationController::pan(ViewportPosition delta) noexcept {
  return transform_.pan_by(delta);
}

bool CanvasNavigationController::zoom_in(
    ViewportPosition focal_point) noexcept {
  return transform_.zoom_by(kKeyboardZoomStep, focal_point);
}

bool CanvasNavigationController::zoom_out(
    ViewportPosition focal_point) noexcept {
  return transform_.zoom_by(1.0 / kKeyboardZoomStep, focal_point);
}

bool CanvasNotationScene::complete() const noexcept {
  return std::ranges::all_of(nodes, [](const CanvasNodeNotation& node) {
    return static_cast<bool>(node);
  });
}

CanvasNotationScene Canvas::layout_nodes(
    const Project& project, const GlyphMetrics& metrics,
    const NotationLayoutOptions& options) const {
  CanvasNotationScene scene;
  scene.nodes.reserve(project.nodes().size());
  const ValidationReport validation =
      ValidationService{}.validate_complete(project);
  for (const Node& node : project.nodes()) {
    NotationLayoutResult result =
        layout_notation(project, node.id(), metrics, options);
    const NodeTimeline* const timeline = node.timeline();
    const CanvasNodeGeometry  geometry =
        node_geometry(node.position(), result.layout);
    scene.nodes.push_back(CanvasNodeNotation{
        node.id(), node.position(),
        CanvasNodeHeader{node.name(),
                         node.color(),
                         !node.notes().empty(),
                         validation_state_for_node(validation, node.id()),
                         timeline != nullptr && timeline->tempo() != nullptr,
                         {CanvasNodeHeaderAction::kEditFreeformNotes},
                         {CanvasNodeHeaderAction::kOpenTempoLane},
                         {CanvasNodeHeaderAction::kPlay}},
        geometry, result.error, std::move(result.layout)});
  }
  return scene;
}

int canvas_version() noexcept {
  return kCanvasVersion;
}
}  // namespace graphscore
