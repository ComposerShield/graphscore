// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/domain/graph_position.hpp>
#include <graphscore/notation/notation_layout.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace graphscore {

struct ViewportPosition {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool operator==(const ViewportPosition&) const = default;
};

// A toolkit-neutral scroll delta, the two-finger trackpad pan stream
// (translation only, never zoom). Produced by graphscore_writer_shell's
// translation of SDL_EVENT_MOUSE_WHEEL and consumed by a
// TrackpadGestureController. Values are subpixel doubles in logical viewport
// coordinates; positive y is downward content motion.
struct ScrollDelta {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool operator==(const ScrollDelta&) const = default;
};

struct WorldBounds {
  GraphPosition origin;
  double        width  = 0.0;
  double        height = 0.0;

  [[nodiscard]] bool operator==(const WorldBounds&) const = default;
};

enum class CanvasItemKind : std::uint8_t {
  kNode,
  kLabel,
  kControl,
  kConnectorSegment,
  kHitRegion,
};

struct CanvasItemId {
  CanvasItemKind kind  = CanvasItemKind::kNode;
  std::uint64_t  value = 0;

  [[nodiscard]] bool operator==(const CanvasItemId&) const  = default;
  [[nodiscard]] auto operator<=>(const CanvasItemId&) const = default;
};

struct CanvasSceneItem {
  CanvasItemId id;
  WorldBounds  bounds;

  [[nodiscard]] bool operator==(const CanvasSceneItem&) const = default;
};

struct SpatialQueryStatistics {
  // Every BVH node whose bounds are tested, including branch and leaf nodes.
  std::size_t nodes_visited = 0;
  // Every leaf item whose bounds are tested for exact inclusive intersection.
  std::size_t candidates_tested = 0;
};

struct SpatialQueryResult {
  std::vector<CanvasSceneItem> items;
  SpatialQueryStatistics       statistics;
};

// A finite endpoint representation used for dirty output. Unlike an
// origin-plus-width rectangle, this can bound disjoint valid geometry near
// opposite finite coordinate extremes without overflowing its representation.
struct WorldRect {
  double left   = 0.0;
  double top    = 0.0;
  double right  = 0.0;
  double bottom = 0.0;

  [[nodiscard]] bool operator==(const WorldRect&) const = default;
};

[[nodiscard]] bool is_valid_world_bounds(const WorldBounds& bounds) noexcept;

class ViewportTransform;

// Converts finite logical viewport dimensions to inclusive world bounds.
// Items touching or crossing any edge are visible. `expansion` is logical
// viewport-space interaction slop and does not alter retained scene content.
[[nodiscard]] std::optional<WorldBounds> viewport_world_bounds(
    const ViewportTransform& transform, double viewport_width,
    double viewport_height, double expansion = 0.0) noexcept;

class SparseSpatialIndex {
 public:
  SparseSpatialIndex();
  ~SparseSpatialIndex();
  SparseSpatialIndex(const SparseSpatialIndex&)            = delete;
  SparseSpatialIndex& operator=(const SparseSpatialIndex&) = delete;
  SparseSpatialIndex(SparseSpatialIndex&&) noexcept;
  SparseSpatialIndex& operator=(SparseSpatialIndex&&) noexcept;

  // Identity is the (kind, value) pair. Duplicate insertion is rejected.
  // Invalid geometry and missing updates/removals leave the index unchanged.
  [[nodiscard]] bool insert(CanvasSceneItem item);
  [[nodiscard]] bool update(CanvasItemId id, WorldBounds bounds);
  [[nodiscard]] bool remove(CanvasItemId id);
  [[nodiscard]] bool contains(CanvasItemId id) const;
  [[nodiscard]] std::optional<CanvasSceneItem> find(CanvasItemId id) const;
  [[nodiscard]] std::size_t                    size() const noexcept;

  // Results are sorted by stable identity, independent of tree traversal order.
  // Intersection is inclusive at every edge.
  [[nodiscard]] std::optional<SpatialQueryResult> query(
      WorldBounds bounds) const;
  [[nodiscard]] std::optional<SpatialQueryResult> query_viewport(
      const ViewportTransform& transform, double viewport_width,
      double viewport_height, double expansion = 0.0) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Accumulates exact add/remove and old/new update coverage. Regions coalesce
// when they touch. If their count would exceed `region_cap`, all regions are
// collapsed to one finite endpoint rectangle; coverage is never discarded.
class BoundedInvalidation {
 public:
  explicit BoundedInvalidation(std::size_t region_cap = 32);

  [[nodiscard]] bool invalidate_add(const CanvasSceneItem& item);
  [[nodiscard]] bool invalidate_remove(const CanvasSceneItem& item);
  [[nodiscard]] bool invalidate_update(const CanvasSceneItem& old_item,
                                       const CanvasSceneItem& new_item);
  void               clear() noexcept;

  [[nodiscard]] const std::vector<WorldRect>& regions() const noexcept;
  [[nodiscard]] std::size_t                   region_cap() const noexcept;

 private:
  [[nodiscard]] bool add_bounds(const WorldBounds& bounds);

  std::size_t            region_cap_ = 0;
  std::vector<WorldRect> regions_;
};

// Scene ownership keeps culling separate from semantic retention: queries
// never remove items. Every successful mutation records bounded redraw damage.
class CanvasScene {
 public:
  explicit CanvasScene(std::size_t dirty_region_cap = 32);

  [[nodiscard]] bool insert(CanvasSceneItem item);
  [[nodiscard]] bool update(CanvasItemId id, WorldBounds bounds);
  [[nodiscard]] bool remove(CanvasItemId id);

  [[nodiscard]] const SparseSpatialIndex&  index() const noexcept;
  [[nodiscard]] const BoundedInvalidation& invalidation() const noexcept;
  void                                     clear_invalidation() noexcept;

 private:
  SparseSpatialIndex  index_;
  BoundedInvalidation invalidation_;
};

// App-owned accessibility focus implementations provide only the focused
// semantic element's GraphScore world geometry through this seam. The broader
// semantic tree remains owned by later accessibility phases.
class FocusPointProvider {
 public:
  virtual ~FocusPointProvider() = default;
  [[nodiscard]] virtual std::optional<WorldBounds> focused_world_bounds()
      const = 0;
};

// Deterministic viewport-space navigation increments. Wheel deltas retain
// their high-resolution magnitude; keyboard input applies one increment per
// delivered press (including OS repeat presses).
constexpr double kKeyboardPanStep      = 64.0;
constexpr double kKeyboardZoomStep     = 1.2;
constexpr double kWheelZoomStepPerUnit = 1.1;

// A toolkit-neutral pinch update, the exclusive pinch-zoom stream. `scale`
// is the multiplicative zoom change since the previous update (scale > 1
// zooms in). `focal_point` is the gesture's focal point in logical viewport
// coordinates, or nullopt when the platform reports none (SDL focus_x/focus_y
// == -1 on desktop).
struct PinchUpdate {
  double                          scale = 1.0;
  std::optional<ViewportPosition> focal_point;

  [[nodiscard]] bool operator==(const PinchUpdate&) const = default;
};

// A tracked touch contact for the diagnostic two-finger centroid fallback.
// The position is in logical viewport coordinates (already converted by the
// shell). Finger contacts are optional: macOS commonly delivers none, and
// their absence is not a defect.
struct FingerContact {
  std::uint64_t    finger_id = 0;
  ViewportPosition position;

  [[nodiscard]] bool operator==(const FingerContact&) const = default;
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

// Applies the ADR 0004 §8 trackpad gesture contract to a ViewportTransform.
//
//   - Two-finger pan (ScrollDelta) changes translation only, never zoom.
//   - Pinch zoom is applied exactly once per PinchUpdate and is never derived
//     from per-finger distance (no double-zoom).
//   - Pinch focal priority: the event's focal_point, then the active
//     two-finger centroid, then the window-center fallback.
//   - Finger contacts are diagnostic/optional: they only maintain the
//     centroid fallback and never synthesize viewport motion. Finger
//     up/canceled ends tracking for the identified finger.
//   - Three-finger behavior is out of scope (a third finger_down is ignored).
//   - Non-finite input is rejected without mutating the transform.
//
// The transform is non-owning and must outlive the controller.
class TrackpadGestureController {
 public:
  explicit TrackpadGestureController(ViewportTransform& transform) noexcept;

  // The controller holds a non-owning reference to its transform. A generated
  // copy or move would rebind that reference to the source's transform rather
  // than the copy's own, so the operations are deleted.
  TrackpadGestureController(const TrackpadGestureController&) = delete;
  TrackpadGestureController& operator=(const TrackpadGestureController&) =
      delete;
  TrackpadGestureController(TrackpadGestureController&&)            = delete;
  TrackpadGestureController& operator=(TrackpadGestureController&&) = delete;

  // Two-finger pan: translation only. Returns false (without mutating the
  // transform) when the delta is non-finite or cannot be applied.
  [[nodiscard]] bool pan(ScrollDelta delta) noexcept;

  // Pinch: zoom exactly once, with the focal point resolved per the priority
  // cascade above. Returns false (without mutating) on a non-positive or
  // non-finite scale, a non-finite focal point, or an inapplicable zoom.
  [[nodiscard]] bool pinch(PinchUpdate update) noexcept;

  // Diagnostic finger tracking. finger_down records a new contact (or
  // refreshes an already-tracked one) and finger_move updates its position;
  // both return false (without tracking/updating) on a non-finite position.
  // finger_up ends tracking for the identified finger; never motion.
  [[nodiscard]] bool finger_down(FingerContact finger) noexcept;
  [[nodiscard]] bool finger_move(FingerContact finger) noexcept;
  void               finger_up(std::uint64_t finger_id) noexcept;

  // Ends tracking for every finger (window focus loss, gesture cancellation).
  // Never synthesizes viewport motion.
  void cancel_tracking() noexcept;

  // The fallback focal point used when neither the pinch event nor the
  // active centroid provides one. Defaults to the viewport origin.
  void set_window_center(ViewportPosition center) noexcept;
  [[nodiscard]] ViewportPosition window_center() const noexcept;

  // The active two-finger centroid, or nullopt when fewer than two fingers
  // are tracked.
  [[nodiscard]] std::optional<ViewportPosition> active_centroid()
      const noexcept;

  [[nodiscard]] std::size_t tracked_finger_count() const noexcept;

 private:
  // The focal-point priority cascade: event focus, then centroid, then
  // window center.
  [[nodiscard]] std::optional<ViewportPosition> resolve_focal(
      const PinchUpdate& update) const noexcept;

  ViewportTransform&                          transform_;
  ViewportPosition                            window_center_{0.0, 0.0};
  std::array<std::optional<FingerContact>, 2> fingers_;
};

class CanvasNavigationController {
 public:
  explicit CanvasNavigationController(ViewportTransform& transform) noexcept;

  CanvasNavigationController(const CanvasNavigationController&) = delete;
  CanvasNavigationController& operator=(const CanvasNavigationController&) =
      delete;
  CanvasNavigationController(CanvasNavigationController&&)            = delete;
  CanvasNavigationController& operator=(CanvasNavigationController&&) = delete;

  [[nodiscard]] bool wheel_pan(ScrollDelta delta) noexcept;
  [[nodiscard]] bool wheel_zoom(double           delta_y,
                                ViewportPosition focal_point) noexcept;
  [[nodiscard]] bool pan(ViewportPosition delta) noexcept;
  [[nodiscard]] bool zoom_in(ViewportPosition focal_point) noexcept;
  [[nodiscard]] bool zoom_out(ViewportPosition focal_point) noexcept;

 private:
  ViewportTransform& transform_;
};

// One retained, node-local notation layout at its graph-canvas position. Every
// project node has one record, including nodes that cannot yet be laid out; the
// error then explains why `layout` is empty without hiding unaffected nodes.
struct CanvasNodeNotation {
  NodeId                        node_id;
  GraphPosition                 position;
  NotationLayoutError           error = NotationLayoutError::kNone;
  std::optional<NotationLayout> layout;

  [[nodiscard]] explicit operator bool() const noexcept {
    return layout.has_value();
  }
};

struct CanvasNotationScene {
  // Project order is retained so paint and hit-test ordering never depend on
  // UUIDs or associative-container traversal.
  std::vector<CanvasNodeNotation> nodes;

  [[nodiscard]] bool complete() const noexcept;
};

class Canvas {
 public:
  Canvas() = default;

  // Produces complete notation for every graph node. layout_notation owns the
  // active-track/staff traversal and common measure geometry; canvas retains
  // each result at the node's world position rather than introducing a second
  // engraving or timeline-alignment algorithm.
  [[nodiscard]] CanvasNotationScene layout_nodes(
      const Project& project, const GlyphMetrics& metrics,
      const NotationLayoutOptions& options = {}) const;
};

[[nodiscard]] int canvas_version() noexcept;

}  // namespace graphscore
