// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "key_bindings.hpp"

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <functional>
#include <optional>

namespace graphscore::writer_app {

// Application-owned keyboard focus context. The notation context includes its
// higher-precedence command-palette and text-entry routing; canvas navigation
// is eligible only when the application explicitly transfers focus to it.
enum class CanvasKeyboardFocus : std::uint8_t {
  kNotation,
  kCanvas,
};

// Owns and routes the canvas viewport's trackpad, wheel, middle-drag, and
// keyboard navigation while forwarding every unconsumed event unchanged.
//
// Every non-gesture event (pointer/key/text/cancel) is forwarded to a
// settable delegate so this handler can sit in front of the notation
// SelectionToolHandler without coupling viewport ownership into it — the
// M6-phase-5 isolation requirement. Gesture events are never forwarded.
class CanvasGestureHandler final : public graphscore::InputHandler {
 public:
  explicit CanvasGestureHandler(
      PrimaryModifier primary = kPlatformPrimaryModifier);

  // The controller holds a reference to this handler's transform. A generated
  // copy or move would rebind it to the source transform, so the operations
  // are deleted.
  CanvasGestureHandler(const CanvasGestureHandler&)            = delete;
  CanvasGestureHandler& operator=(const CanvasGestureHandler&) = delete;
  CanvasGestureHandler(CanvasGestureHandler&&)                 = delete;
  CanvasGestureHandler& operator=(CanvasGestureHandler&&)      = delete;

  // ---- InputHandler (gestures) ---------------------------------------------

  void on_wheel(graphscore::WheelEvent event) override;
  void on_pinch(graphscore::PinchUpdate update) override;
  void on_finger_down(graphscore::FingerContact finger) override;
  void on_finger_move(graphscore::FingerContact finger) override;
  void on_finger_up(std::uint64_t finger_id) override;
  void on_finger_canceled(std::uint64_t finger_id) override;

  // ---- InputHandler (forwarded to the delegate) ----------------------------

  void on_pointer_press(graphscore::PointerEvent event) override;
  void on_pointer_move(graphscore::PointerEvent event) override;
  void on_pointer_release(graphscore::PointerEvent event) override;
  void on_cancel() override;
  void on_key_press(graphscore::KeyEvent event) override;
  void on_text_input(graphscore::TextInputEvent event) override;

  // ---- InputHandler (size-derived fallbacks) --------------------------------

  // Derives the pinch focal window-center fallback from the actual logical
  // viewport size the shell reports on window creation and resize.
  void on_viewport_size_changed(double width, double height) override;

  // The delegate that receives non-gesture events. May be null (events are
  // then dropped). Not owned.
  void set_delegate(graphscore::InputHandler* delegate) noexcept;
  void set_focus_point_provider(
      const graphscore::FocusPointProvider* provider) noexcept;
  void set_keyboard_focus(CanvasKeyboardFocus focus) noexcept;

  // Installs the graph-canvas owner for the selected-connector reset action.
  // The callback is consulted only while canvas keyboard focus is active; the
  // notation context therefore keeps ownership of the same chord otherwise.
  using ResetSelectedConnectorHandler = std::function<void()>;
  void set_reset_selected_connector_handler(
      ResetSelectedConnectorHandler handler);

  // The pinch focal fallback (logical viewport coordinates), forwarded to
  // the controller.
  void set_window_center(graphscore::ViewportPosition center) noexcept;

  // ---- test access ---------------------------------------------------------

  [[nodiscard]] const graphscore::ViewportTransform& transform() const noexcept;
  [[nodiscard]] const graphscore::TrackpadGestureController& controller()
      const noexcept;

 private:
  // Inverse-maps a pointer event's logical viewport coordinates to world
  // (notation) coordinates. Returns nullopt when the coordinate cannot be
  // mapped (non-finite or a degenerate transform), in which case the event is
  // dropped rather than dispatched in the wrong coordinate space.
  [[nodiscard]] std::optional<graphscore::PointerEvent> to_world_pointer(
      graphscore::PointerEvent event) const;
  [[nodiscard]] graphscore::ViewportPosition keyboard_zoom_focal() const;

  graphscore::ViewportTransform          transform_;
  graphscore::TrackpadGestureController  controller_;
  graphscore::CanvasNavigationController navigation_;
  PrimaryModifier                        primary_;
  graphscore::InputHandler*              delegate_       = nullptr;
  const graphscore::FocusPointProvider*  focus_provider_ = nullptr;
  CanvasKeyboardFocus keyboard_focus_ = CanvasKeyboardFocus::kNotation;
  ResetSelectedConnectorHandler               reset_selected_connector_handler_;
  std::optional<graphscore::ViewportPosition> middle_drag_position_;
};

}  // namespace graphscore::writer_app
