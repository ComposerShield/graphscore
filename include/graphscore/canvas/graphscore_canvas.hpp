// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/domain/command_history.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/graph_position.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/validation_service.hpp>
#include <graphscore/notation/notation_layout.hpp>
#include <graphscore/notation/notation_palette.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
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

enum class CanvasNodeValidationState : std::uint8_t {
  kValid = 0,
  kWarning,
  kError,
};

enum class CanvasNodeHeaderAction : std::uint8_t {
  kEditFreeformNotes = 0,
  kOpenTempoLane,
  kPlay,
};

// A distinct toolkit-neutral button identity with node-local click geometry.
// Activation routing and accessibility behavior remain layered onto these
// stable actions by their dedicated canvas phases.
struct CanvasNodeHeaderButton {
  static constexpr double kSize = 32.0;

  CanvasNodeHeaderAction action = CanvasNodeHeaderAction::kEditFreeformNotes;
  NotationRect           bounds;

  [[nodiscard]] bool operator==(const CanvasNodeHeaderButton&) const = default;
};

// Header semantics retained independently from notation geometry. The notes
// and tempo booleans describe current content; their buttons remain present so
// an empty value can still be opened and edited.
struct CanvasNodeHeader {
  std::string               name;
  std::uint32_t             color              = 0xFFFFFFFF;
  bool                      has_freeform_notes = false;
  CanvasNodeValidationState validation     = CanvasNodeValidationState::kValid;
  bool                      has_tempo_lane = false;
  CanvasNodeHeaderButton    freeform_notes_button;
  CanvasNodeHeaderButton    tempo_lane_button{
      CanvasNodeHeaderAction::kOpenTempoLane, {}};
  CanvasNodeHeaderButton play_button{CanvasNodeHeaderAction::kPlay, {}};

  [[nodiscard]] bool operator==(const CanvasNodeHeader&) const = default;
};

// Content-driven node geometry in node-local coordinates, plus the complete
// world-space bound used by culling and routing. Nodes auto-fit their notation:
// width is the larger of kMinimumWidth and the notation width, while height is
// the fixed header followed by the complete notation height. There is no
// independent crop size. Changing notation wrapping or musical content requires
// relayout and therefore deterministically resizes the node.
struct CanvasNodeGeometry {
  static constexpr double kHeaderHeight          = 64.0;
  static constexpr double kMinimumWidth          = 320.0;
  static constexpr double kFallbackContentHeight = 160.0;

  WorldBounds  bounds;
  NotationRect header_bounds;
  NotationRect content_bounds;
  NotationRect notation_bounds;

  [[nodiscard]] bool operator==(const CanvasNodeGeometry&) const = default;
};

enum class CanvasPortDirection : std::uint8_t {
  kInput = 0,
  kOutput,
};

// A retained node-local port presentation. ConnectorId is the stable domain
// identity; names and labels may change without changing that identity. Bounds
// straddle the appropriate node edge and preserve domain insertion order.
struct CanvasNodePort {
  static constexpr double kDiameter = 16.0;

  ConnectorId         connector_id;
  CanvasPortDirection direction = CanvasPortDirection::kInput;
  std::string         name;
  std::string         accessibility_id;
  std::string         accessibility_label;
  NotationRect        bounds;

  [[nodiscard]] bool operator==(const CanvasNodePort&) const = default;
};

// One retained, node-local header and notation layout at its graph-canvas
// position. Every project node has one record, including nodes that cannot yet
// be laid out; the error then explains why `layout` is empty and geometry uses
// a deterministic fallback body without hiding unaffected nodes.
struct CanvasNodeNotation {
  NodeId                        node_id;
  GraphPosition                 position;
  CanvasNodeHeader              header;
  CanvasNodeGeometry            geometry;
  std::vector<CanvasNodePort>   ports;
  NotationLayoutError           error = NotationLayoutError::kNone;
  std::optional<NotationLayout> layout;

  [[nodiscard]] explicit operator bool() const noexcept {
    return layout.has_value();
  }
};

// The short orthogonal legs that attach one connected output to its source
// and destination node bounds.
struct CanvasConnectorEndpointLeg {
  GraphPosition attachment;
  GraphPosition outer;

  [[nodiscard]] bool operator==(const CanvasConnectorEndpointLeg&) const =
      default;
};

// A compact destination-end affordance with a larger circular activation area.
// Both diameters are world-space presentation values; the visual circle stays
// the size of a port while the interaction circle meets a 44-unit target.
struct CanvasConnectorActionCircle {
  static constexpr double kDiameter            = 16.0;
  static constexpr double kInteractionDiameter = 44.0;

  GraphPosition center;

  [[nodiscard]] bool operator==(const CanvasConnectorActionCircle&) const =
      default;
};

enum class CanvasConnectorLinePattern : std::uint8_t {
  kSolid = 0,
  kDashed,
};

// Toolkit-neutral route styling. Color uses packed 0xRRGGBBAA so canvas does
// not depend on the rendering backend; line pattern redundantly communicates
// the semantic type without relying on color perception.
struct CanvasConnectorStyle {
  std::uint32_t              color_rgba   = 0;
  CanvasConnectorLinePattern line_pattern = CanvasConnectorLinePattern::kSolid;

  [[nodiscard]] bool operator==(const CanvasConnectorStyle&) const = default;
};

[[nodiscard]] constexpr CanvasConnectorStyle canvas_connector_style(
    ConnectorType type) noexcept {
  switch (type) {
    case ConnectorType::kSequential:
      return {0x2F80EDFFU, CanvasConnectorLinePattern::kSolid};
    case ConnectorType::kVertical:
      return {0xD35400FFU, CanvasConnectorLinePattern::kDashed};
  }
  return {};
}

enum class CanvasConnectorPathVerb : std::uint8_t {
  kMove = 0,
  kLine,
  kQuadratic,
};

// One world-space command in the toolkit-neutral connector render path.
// kQuadratic uses control as its control point; kMove and kLine ignore it.
struct CanvasConnectorPathElement {
  CanvasConnectorPathVerb verb = CanvasConnectorPathVerb::kMove;
  GraphPosition           control;
  GraphPosition           end;

  [[nodiscard]] bool operator==(const CanvasConnectorPathElement&) const =
      default;
};

// Converts an orthogonal route polyline into a render path. Every 90-degree
// turn uses the same radius unless an adjacent segment is too short, in which
// case the radius is clamped symmetrically so neighboring corners never
// overlap.
[[nodiscard]] std::vector<CanvasConnectorPathElement>
canvas_connector_render_path(std::span<const GraphPosition> route_points);

enum class CanvasCursorShape : std::uint8_t {
  kDefault = 0,
  kResizeEastWest,
  kResizeNorthSouth,
};

// A hovered segment in the authoritative route polyline. The cursor describes
// the segment's permitted movement, perpendicular to the segment itself.
struct CanvasConnectorSegmentHover {
  std::size_t       segment_index = 0;
  CanvasCursorShape cursor        = CanvasCursorShape::kDefault;

  [[nodiscard]] bool operator==(const CanvasConnectorSegmentHover&) const =
      default;
};

// Resolves a world-space pointer to the nearest orthogonal route segment
// within the inclusive hit tolerance. Equal-distance hits use route order.
[[nodiscard]] std::optional<CanvasConnectorSegmentHover>
canvas_connector_segment_hover(std::span<const GraphPosition> route_points,
                               GraphPosition                  pointer,
                               double hit_tolerance) noexcept;

// Moves an orthogonal segment to the pointer's perpendicular coordinate. The
// two endpoint clearance legs remain fixed; dragging a segment adjacent to one
// of those legs inserts the bends needed to move the editable part of the
// route. Collinear and zero-length interior bends are removed.
[[nodiscard]] std::optional<std::vector<GraphPosition>>
canvas_connector_drag_segment(std::span<const GraphPosition> route_points,
                              std::size_t                    segment_index,
                              GraphPosition                  pointer) noexcept;

struct CanvasConnectorGeometry {
  static constexpr double kEndpointClearance = 24.0;
  static constexpr double kCornerRadius      = 12.0;

  NodeId                     source_node;
  ConnectorId                source_connector;
  NodeId                     destination_node;
  ConnectorId                destination_connector;
  CanvasConnectorEndpointLeg source_leg;
  CanvasConnectorEndpointLeg destination_leg;
  // Centered on the destination outer point so the affordance remains clear of
  // the input port while following endpoint repair and node movement.
  CanvasConnectorActionCircle action_circle;
  // Complete derived world-space polyline, including both attachment and
  // outer points. Automatic routes avoid node interiors; customized routes
  // retain their interior points while endpoint joins are repaired as needed.
  std::vector<GraphPosition> route_points;
  // Derived from route_points for painting. The orthogonal polyline remains
  // authoritative for routing and hit testing.
  std::vector<CanvasConnectorPathElement> render_path;
  ConnectorType                           type = ConnectorType::kSequential;
  CanvasConnectorStyle                    style =
      canvas_connector_style(ConnectorType::kSequential);

  [[nodiscard]] bool operator==(const CanvasConnectorGeometry&) const = default;
};

// The stable identity of a selected connector. A scene vector index is not
// suitable selection state because project edits can reorder derived geometry.
struct CanvasConnectorSelection {
  NodeId      source_node;
  ConnectorId source_connector;

  [[nodiscard]] bool operator==(const CanvasConnectorSelection&) const =
      default;
};

struct CanvasNotationScene;

struct CanvasPortSelection {
  NodeId              node_id;
  ConnectorId         connector_id;
  CanvasPortDirection direction = CanvasPortDirection::kInput;

  [[nodiscard]] bool operator==(const CanvasPortSelection&) const = default;
};

// A connector path click also names the authoritative orthogonal segment at
// the click. The stable connector identity drives inspector/editing selection;
// an editable interior segment index can start a route-edit gesture.
struct CanvasConnectorPathSelection {
  CanvasConnectorSelection connector;
  std::size_t              segment_index = 0;

  [[nodiscard]] bool operator==(const CanvasConnectorPathSelection&) const =
      default;
};

struct CanvasNodeSelection {
  NodeId node_id;

  [[nodiscard]] bool operator==(const CanvasNodeSelection&) const = default;
};

struct CanvasControlSelection {
  NodeId                 node_id;
  CanvasNodeHeaderAction action = CanvasNodeHeaderAction::kEditFreeformNotes;

  [[nodiscard]] bool operator==(const CanvasControlSelection&) const = default;
};

// A dedicated node-play activation. Pointer selection and keyboard focus both
// resolve through CanvasControlSelection first, so activating this request can
// never be confused with selecting or beginning a drag on the containing node.
struct CanvasNodePlaybackActionRequest {
  NodeId node_id;

  [[nodiscard]] bool operator==(const CanvasNodePlaybackActionRequest&) const =
      default;
};

// Converts only the dedicated play control to a playback request. The other
// header controls and the node background retain their independent editing
// behavior.
[[nodiscard]] std::optional<CanvasNodePlaybackActionRequest>
canvas_node_playback_action_request(
    const CanvasControlSelection& control) noexcept;

struct CanvasNotationSelection {
  NodeId    node_id;
  Selection selection;

  [[nodiscard]] bool operator==(const CanvasNotationSelection&) const = default;
};

using CanvasSingleClickSelection =
    std::variant<CanvasPortSelection, CanvasConnectorPathSelection,
                 CanvasNodeSelection, CanvasControlSelection,
                 CanvasNotationSelection>;

// Resolves one normal world-space click to the topmost editable canvas target.
// Node-local controls, ports, and notation take precedence over their node;
// nodes paint above connector paths, and later scene entries paint above
// earlier entries. Connector tolerance is expressed in world coordinates.
[[nodiscard]] std::optional<CanvasSingleClickSelection>
canvas_single_click_selection(const Project&             project,
                              const CanvasNotationScene& scene,
                              const NotePaletteState&    palette,
                              GraphPosition              pointer,
                              double connector_hit_tolerance);

// A request to invoke a connection's playback action. Canvas gesture
// recognition carries only the stable output identity; availability is
// resolved against the active node before a later playback controller performs
// sequential-queue versus vertical-jump dispatch.
struct CanvasConnectorPlaybackActionRequest {
  CanvasConnectorSelection connector;

  [[nodiscard]] bool operator==(
      const CanvasConnectorPlaybackActionRequest&) const = default;
};

// Toolkit-neutral action availability used by both connector invocation
// gestures. An unavailable action retains a composer-facing reason but no
// request to dispatch, while the connector remains independently editable.
struct CanvasConnectorPlaybackActionResult {
  std::optional<CanvasConnectorPlaybackActionRequest> request;
  std::string                                         unavailable_reason;

  [[nodiscard]] bool available() const noexcept { return request.has_value(); }
};

// App-owned playback implementations expose current transport state and the
// two semantic connector actions through this toolkit-neutral seam. Canvas
// routes requests here but does not render audio; Milestones 08/09 connect an
// implementation to the writer scheduler and plugin chains.
class CanvasConnectorPlaybackController {
 public:
  virtual ~CanvasConnectorPlaybackController() = default;

  [[nodiscard]] virtual std::optional<NodeId> active_node() const noexcept = 0;

  virtual void queue_sequential_connector(
      CanvasConnectorSelection connector) noexcept = 0;
  virtual void take_vertical_connector(
      CanvasConnectorSelection connector) noexcept = 0;
};

// Enables an action only when its output belongs to the active node. No active
// node represents stopped playback and carries a distinct unavailable reason.
[[nodiscard]] CanvasConnectorPlaybackActionResult
canvas_connector_playback_action(
    const CanvasConnectorPlaybackActionRequest& request,
    std::optional<NodeId>                       active_node);

// Resolves availability against controller-owned transport state, looks up the
// output's current semantic type, and routes exactly one action through the
// corresponding controller method. Stale or disconnected identities are
// unavailable and never reach the controller.
[[nodiscard]] CanvasConnectorPlaybackActionResult
canvas_connector_playback_action(
    const Project& project, const CanvasConnectorPlaybackActionRequest& request,
    CanvasConnectorPlaybackController& controller);

// Resolves a normal double-click to a playback-action request only when the
// same topmost target would be selected as a connector path by a single click.
// This leaves canvas_single_click_selection's editing behavior independent and
// unchanged.
[[nodiscard]] std::optional<CanvasConnectorPlaybackActionRequest>
canvas_double_click_playback_action_request(const Project&             project,
                                            const CanvasNotationScene& scene,
                                            const NotePaletteState&    palette,
                                            GraphPosition              pointer,
                                            double connector_hit_tolerance);

// Activates the topmost destination action circle through the same stable
// playback-action request used by connector double-click. The interaction
// circle is intentionally larger than its visual circle.
[[nodiscard]] std::optional<CanvasConnectorPlaybackActionRequest>
canvas_action_circle_playback_action_request(const CanvasNotationScene& scene,
                                             GraphPosition pointer) noexcept;

struct CanvasConnectorDestinationFields {
  NodeId                     node_id;
  std::optional<std::string> node_name;
  ConnectorId                input_id;
  std::optional<std::string> input_name;

  [[nodiscard]] bool operator==(const CanvasConnectorDestinationFields&) const =
      default;
};

struct CanvasConnectorEventFields {
  EventId                    event_id;
  std::optional<std::string> event_name;

  [[nodiscard]] bool operator==(const CanvasConnectorEventFields&) const =
      default;
};

struct CanvasConnectorListenerFields {
  QueuePolicy policy   = QueuePolicy::kLatestValidWins;
  std::size_t capacity = 1;

  [[nodiscard]] bool operator==(const CanvasConnectorListenerFields&) const =
      default;
};

// Toolkit-neutral values for an output connector inspector. Listener values
// are resolved from the source node's shared (node, event) listener rather
// than copied onto the connector. Missing linked entities remain visible by
// stable identity and are explained by the attached validation diagnostics.
struct CanvasConnectorInspector {
  NodeId                                    source_node_id;
  std::string                               source_node_name;
  ConnectorId                               output_id;
  std::string                               name;
  ConnectorType                             type = ConnectorType::kSequential;
  std::optional<CanvasConnectorEventFields> event;
  int                                       priority      = 0;
  Rational                                  random_weight = Rational(1);
  std::optional<CanvasConnectorDestinationFields> destination;
  std::optional<CanvasConnectorListenerFields>    listener;
  std::vector<Diagnostic>                         diagnostics;

  [[nodiscard]] bool operator==(const CanvasConnectorInspector&) const =
      default;
};

struct CanvasNotationScene {
  // Project order is retained so paint and hit-test ordering never depend on
  // UUIDs or associative-container traversal.
  std::vector<CanvasNodeNotation> nodes;
  // Project/output order is retained for the same reason. Only connected
  // outputs have derived endpoint geometry.
  std::vector<CanvasConnectorGeometry> connectors;

  [[nodiscard]] bool complete() const noexcept;
};

// Stages one node drag in retained canvas geometry. Pointer updates move the
// node and every attached endpoint leg immediately, while the Project remains
// unchanged. finish() records one SetNodePositionCommand; cancel() (and the
// destructor safety net) restores the retained scene to its starting state.
class CanvasNodeDragController {
 public:
  CanvasNodeDragController(Project& project, CommandHistory& history,
                           CanvasNotationScene& scene) noexcept;
  ~CanvasNodeDragController();

  CanvasNodeDragController(const CanvasNodeDragController&)            = delete;
  CanvasNodeDragController& operator=(const CanvasNodeDragController&) = delete;
  CanvasNodeDragController(CanvasNodeDragController&&)                 = delete;
  CanvasNodeDragController& operator=(CanvasNodeDragController&&)      = delete;

  [[nodiscard]] bool   begin(NodeId node_id, GraphPosition pointer) noexcept;
  [[nodiscard]] bool   update(GraphPosition pointer) noexcept;
  [[nodiscard]] Result finish() noexcept;
  void                 cancel() noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }

 private:
  [[nodiscard]] CanvasNodeNotation* dragged_node() noexcept;
  [[nodiscard]] bool set_preview_position(GraphPosition position) noexcept;

  Project&                             project_;
  CommandHistory&                      history_;
  CanvasNotationScene&                 scene_;
  NodeId                               node_id_;
  GraphPosition                        pointer_start_;
  GraphPosition                        position_start_;
  std::vector<CanvasConnectorGeometry> connectors_start_;
  bool                                 active_ = false;
};

// Owns homogeneous node selection and the organization operations which act
// on it. Selection order follows project order, so every group command and
// connector remap is deterministic. Structural edits relayout the retained
// scene transactionally; a failed publication rolls the domain command back.
class CanvasNodeOperationsController {
 public:
  CanvasNodeOperationsController(Project& project, CommandHistory& history,
                                 CanvasNotationScene&  scene,
                                 const GlyphMetrics&   metrics,
                                 NotationLayoutOptions options = {}) noexcept;
  ~CanvasNodeOperationsController();

  CanvasNodeOperationsController(const CanvasNodeOperationsController&) =
      delete;
  CanvasNodeOperationsController& operator=(
      const CanvasNodeOperationsController&)                       = delete;
  CanvasNodeOperationsController(CanvasNodeOperationsController&&) = delete;
  CanvasNodeOperationsController& operator=(CanvasNodeOperationsController&&) =
      delete;

  [[nodiscard]] bool select(NodeId node_id, bool additive = false);
  void               clear_selection() noexcept;
  [[nodiscard]] const std::vector<NodeId>& selection() const noexcept;

  [[nodiscard]] bool begin_move(NodeId anchor, GraphPosition pointer) noexcept;
  [[nodiscard]] bool update_move(GraphPosition pointer) noexcept;
  [[nodiscard]] Result finish_move() noexcept;
  void                 cancel_move() noexcept;

  [[nodiscard]] bool move_active() const noexcept { return move_active_; }

  [[nodiscard]] Result duplicate_selected(GraphPosition offset = {
                                              40.0, 40.0}) noexcept;
  [[nodiscard]] Result copy_selected() noexcept;
  [[nodiscard]] Result paste(GraphPosition offset = {40.0, 40.0}) noexcept;
  [[nodiscard]] Result delete_selected() noexcept;

  [[nodiscard]] bool has_clipboard() const noexcept {
    return !clipboard_.empty();
  }

 private:
  [[nodiscard]] bool   selection_is_current() const noexcept;
  [[nodiscard]] Result duplicate_nodes(std::vector<Node> snapshots,
                                       GraphPosition     offset) noexcept;

  Project&                             project_;
  CommandHistory&                      history_;
  CanvasNotationScene&                 scene_;
  const GlyphMetrics&                  metrics_;
  NotationLayoutOptions                options_;
  std::vector<NodeId>                  selection_;
  std::vector<Node>                    clipboard_;
  std::vector<GraphPosition>           move_starts_;
  std::vector<CanvasConnectorGeometry> connectors_start_;
  GraphPosition                        pointer_start_;
  bool                                 move_active_ = false;
};

// Stages one orthogonal connector-segment drag. The retained scene is updated
// during the gesture while the Project remains unchanged; finish() records one
// SetCustomRouteCommand and cancel() restores the exact starting geometry.
class CanvasConnectorSegmentDragController {
 public:
  CanvasConnectorSegmentDragController(Project&             project,
                                       CommandHistory&      history,
                                       CanvasNotationScene& scene) noexcept;
  ~CanvasConnectorSegmentDragController();

  CanvasConnectorSegmentDragController(
      const CanvasConnectorSegmentDragController&) = delete;
  CanvasConnectorSegmentDragController& operator=(
      const CanvasConnectorSegmentDragController&) = delete;
  CanvasConnectorSegmentDragController(CanvasConnectorSegmentDragController&&) =
      delete;
  CanvasConnectorSegmentDragController& operator=(
      CanvasConnectorSegmentDragController&&) = delete;

  [[nodiscard]] bool   begin(NodeId source_node, ConnectorId source_output,
                             std::size_t   segment_index,
                             GraphPosition pointer) noexcept;
  [[nodiscard]] bool   update(GraphPosition pointer) noexcept;
  [[nodiscard]] Result finish() noexcept;
  void                 cancel() noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }

 private:
  [[nodiscard]] bool set_preview_route(
      std::vector<GraphPosition> route_points) noexcept;

  Project&                                project_;
  CommandHistory&                         history_;
  CanvasNotationScene&                    scene_;
  NodeId                                  source_node_;
  ConnectorId                             source_output_;
  GraphPosition                           source_position_start_;
  GraphPosition                           destination_position_start_;
  std::size_t                             connector_index_ = 0;
  std::size_t                             segment_index_   = 0;
  std::vector<GraphPosition>              route_start_;
  std::vector<CanvasConnectorPathElement> render_start_;
  std::vector<RoutePoint>                 domain_waypoints_start_;
  std::vector<CanvasConnectorGeometry>    connectors_start_;
  std::vector<NodeId>                     node_ids_start_;
  std::vector<GraphPosition>              node_positions_start_;
  std::vector<WorldBounds>                node_bounds_start_;
  std::vector<GraphPosition>              preview_route_;
  std::vector<CanvasConnectorPathElement> preview_render_path_;
  std::optional<ConnectorDestination>     destination_start_;
  ConnectorType type_start_          = ConnectorType::kSequential;
  bool          route_was_automatic_ = true;
  bool          preview_changed_     = false;
  bool          active_              = false;
};

// Owns the selected connection identity and its route-reset/delete actions.
// Retained-scene updates are prepared before commands are committed, then
// published with no-throw operations so project and scene cannot disagree.
class CanvasConnectorSelectionController {
 public:
  CanvasConnectorSelectionController(Project& project, CommandHistory& history,
                                     CanvasNotationScene& scene) noexcept;

  CanvasConnectorSelectionController(
      const CanvasConnectorSelectionController&) = delete;
  CanvasConnectorSelectionController& operator=(
      const CanvasConnectorSelectionController&) = delete;
  CanvasConnectorSelectionController(CanvasConnectorSelectionController&&) =
      delete;
  CanvasConnectorSelectionController& operator=(
      CanvasConnectorSelectionController&&) = delete;

  // Selects a connected output represented by the current retained scene.
  // Selection is stable across scene vector reordering because it stores ids.
  [[nodiscard]] bool select(NodeId      source_node,
                            ConnectorId source_connector) noexcept;
  void               clear_selection() noexcept;

  [[nodiscard]] const std::optional<CanvasConnectorSelection>& selection()
      const noexcept {
    return selection_;
  }

  // Primary+Shift+R's canvas action: discard custom waypoints and return the
  // selected connector to deterministic automatic routing. An already
  // automatic route is a successful no-op and does not add history.
  [[nodiscard]] Result reset_selected_route() noexcept;

  // Delete's canvas action: disconnect the selected output through command
  // history while retaining its named output port for later reconnection.
  [[nodiscard]] Result delete_selected_connector() noexcept;

 private:
  Project&                                project_;
  CommandHistory&                         history_;
  CanvasNotationScene&                    scene_;
  std::optional<CanvasConnectorSelection> selection_;
};

// Owns one toolkit-neutral output-to-input attachment gesture. begin() accepts
// only an unconnected output represented in the retained scene. finish()
// commits through ConnectCommand, publishes exactly one derived connector
// geometry, and ends the gesture. An occupied output must be disconnected
// before it can begin another attachment, so retargeting is never implicit.
class CanvasConnectorAttachmentController {
 public:
  CanvasConnectorAttachmentController(Project& project, CommandHistory& history,
                                      CanvasNotationScene& scene) noexcept;

  CanvasConnectorAttachmentController(
      const CanvasConnectorAttachmentController&) = delete;
  CanvasConnectorAttachmentController& operator=(
      const CanvasConnectorAttachmentController&) = delete;
  CanvasConnectorAttachmentController(CanvasConnectorAttachmentController&&) =
      delete;
  CanvasConnectorAttachmentController& operator=(
      CanvasConnectorAttachmentController&&) = delete;

  [[nodiscard]] bool   begin(NodeId      source_node,
                             ConnectorId source_output) noexcept;
  [[nodiscard]] Result finish(NodeId      destination_node,
                              ConnectorId destination_input) noexcept;
  void                 cancel() noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }

 private:
  Project&             project_;
  CommandHistory&      history_;
  CanvasNotationScene& scene_;
  NodeId               source_node_;
  ConnectorId          source_output_;
  bool                 active_ = false;
};

// Authors an output's semantic transition type through the reversible domain
// command path and updates any retained connected route immediately. The
// style is always derived from ConnectorType and is never persisted as a
// second source of truth.
class CanvasConnectorTypeController {
 public:
  CanvasConnectorTypeController(Project& project, CommandHistory& history,
                                CanvasNotationScene& scene) noexcept;

  [[nodiscard]] Result set_type(NodeId node_id, ConnectorId output_id,
                                ConnectorType type) noexcept;

 private:
  Project&             project_;
  CommandHistory&      history_;
  CanvasNotationScene& scene_;
};

// Authors connector-inspector values through reversible domain commands.
// Queue policy and capacity are addressed through an output only to locate its
// bound source-node listener; matching outputs therefore observe one shared
// value. Renaming also refreshes the retained port presentation immediately.
class CanvasConnectorInspectorController {
 public:
  CanvasConnectorInspectorController(Project& project, CommandHistory& history,
                                     CanvasNotationScene& scene) noexcept;

  [[nodiscard]] Result set_event_binding(NodeId node_id, ConnectorId output_id,
                                         std::optional<EventId> event) noexcept;
  [[nodiscard]] Result set_priority(NodeId node_id, ConnectorId output_id,
                                    int priority) noexcept;
  [[nodiscard]] Result set_random_weight(NodeId node_id, ConnectorId output_id,
                                         Rational weight) noexcept;
  [[nodiscard]] Result set_name(NodeId node_id, ConnectorId output_id,
                                std::string name) noexcept;
  [[nodiscard]] Result set_listener(NodeId node_id, ConnectorId output_id,
                                    QueuePolicy policy,
                                    std::size_t capacity) noexcept;

 private:
  [[nodiscard]] OutputConnector* find_output(NodeId      node_id,
                                             ConnectorId output_id) noexcept;

  Project&             project_;
  CommandHistory&      history_;
  CanvasNotationScene& scene_;
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

  [[nodiscard]] std::optional<CanvasConnectorInspector> inspect_connector(
      const Project& project, NodeId source_node, ConnectorId output_id) const;
};

[[nodiscard]] int canvas_version() noexcept;

}  // namespace graphscore
