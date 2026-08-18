// SPDX-License-Identifier: Apache-2.0

#include "canvas_gesture_handler.hpp"

#include <cmath>
#include <type_traits>
#include <utility>

namespace graphscore::writer_app {

static_assert(!std::is_copy_constructible_v<CanvasGestureHandler>);
static_assert(!std::is_move_constructible_v<CanvasGestureHandler>);
static_assert(!std::is_copy_assignable_v<CanvasGestureHandler>);
static_assert(!std::is_move_assignable_v<CanvasGestureHandler>);

CanvasGestureHandler::CanvasGestureHandler(PrimaryModifier primary)
    : controller_(transform_), navigation_(transform_), primary_(primary) {}

void CanvasGestureHandler::on_wheel(graphscore::WheelEvent event) {
  if (is_primary_chord(event.modifiers, primary_)) {
    (void)navigation_.wheel_zoom(event.delta.y, event.pointer);
    return;
  }
  if (classify_chord(event.modifiers, primary_) == ChordClass::kUnmodified) {
    (void)navigation_.wheel_pan(event.delta);
    return;
  }
  if (delegate_ != nullptr) {
    delegate_->on_wheel(event);
  }
}

void CanvasGestureHandler::on_pinch(graphscore::PinchUpdate update) {
  (void)controller_.pinch(update);
}

void CanvasGestureHandler::on_finger_down(graphscore::FingerContact finger) {
  (void)controller_.finger_down(finger);
}

void CanvasGestureHandler::on_finger_move(graphscore::FingerContact finger) {
  (void)controller_.finger_move(finger);
}

void CanvasGestureHandler::on_finger_up(std::uint64_t finger_id) {
  controller_.finger_up(finger_id);
}

void CanvasGestureHandler::on_finger_canceled(std::uint64_t finger_id) {
  controller_.finger_up(finger_id);
}

void CanvasGestureHandler::on_pointer_press(graphscore::PointerEvent event) {
  if (event.button == graphscore::PointerButton::kMiddle) {
    if (std::isfinite(event.x) && std::isfinite(event.y)) {
      middle_drag_position_ = graphscore::ViewportPosition{event.x, event.y};
    }
    return;
  }
  if (delegate_ == nullptr) {
    return;
  }
  if (const auto mapped = to_world_pointer(event); mapped.has_value()) {
    delegate_->on_pointer_press(*mapped);
  }
}

void CanvasGestureHandler::on_pointer_move(graphscore::PointerEvent event) {
  if (middle_drag_position_.has_value()) {
    const graphscore::ViewportPosition current{event.x, event.y};
    if (std::isfinite(current.x) && std::isfinite(current.y)) {
      const graphscore::ViewportPosition delta{
          current.x - middle_drag_position_->x,
          current.y - middle_drag_position_->y};
      if (navigation_.pan(delta)) {
        middle_drag_position_ = current;
      }
    }
    return;
  }
  if (delegate_ == nullptr) {
    return;
  }
  if (const auto mapped = to_world_pointer(event); mapped.has_value()) {
    delegate_->on_pointer_move(*mapped);
  }
}

void CanvasGestureHandler::on_pointer_release(graphscore::PointerEvent event) {
  if (event.button == graphscore::PointerButton::kMiddle) {
    middle_drag_position_.reset();
    return;
  }
  if (delegate_ == nullptr) {
    return;
  }
  if (const auto mapped = to_world_pointer(event); mapped.has_value()) {
    delegate_->on_pointer_release(*mapped);
  }
}

void CanvasGestureHandler::on_cancel() {
  // End finger tracking first, then let the delegate cancel its own
  // transient pointer state.
  controller_.cancel_tracking();
  middle_drag_position_.reset();
  if (delegate_ != nullptr) {
    delegate_->on_cancel();
  }
}

void CanvasGestureHandler::on_key_press(graphscore::KeyEvent event) {
  if (keyboard_focus_ != CanvasKeyboardFocus::kCanvas) {
    if (delegate_ != nullptr) {
      delegate_->on_key_press(event);
    }
    return;
  }

  const ChordClass chord = classify_chord(event.modifiers, primary_);
  if (chord == ChordClass::kShift &&
      event.code == graphscore::KeyCode::kEquals) {
    (void)navigation_.zoom_in(keyboard_zoom_focal());
    return;
  }
  if (chord == ChordClass::kShiftPrimary &&
      event.logical == graphscore::LogicalKey::kR) {
    if (!event.repeat && reset_selected_connector_handler_) {
      reset_selected_connector_handler_();
    }
    return;
  }
  if (chord == ChordClass::kUnmodified) {
    bool consumed = true;
    switch (event.code) {
      case graphscore::KeyCode::kReturn:
      case graphscore::KeyCode::kSpace:
        if (focused_control_.has_value()) {
          const auto request = graphscore::canvas_node_playback_action_request(
              *focused_control_);
          if (request.has_value()) {
            if (!event.repeat && node_play_handler_) {
              node_play_handler_(*request);
            }
            break;
          }
        }
        consumed = false;
        break;
      case graphscore::KeyCode::kDelete:
        if (!event.repeat && delete_selected_connector_handler_) {
          delete_selected_connector_handler_();
        }
        break;
      case graphscore::KeyCode::kLeft:
        (void)navigation_.pan({graphscore::kKeyboardPanStep, 0.0});
        break;
      case graphscore::KeyCode::kRight:
        (void)navigation_.pan({-graphscore::kKeyboardPanStep, 0.0});
        break;
      case graphscore::KeyCode::kUp:
        (void)navigation_.pan({0.0, graphscore::kKeyboardPanStep});
        break;
      case graphscore::KeyCode::kDown:
        (void)navigation_.pan({0.0, -graphscore::kKeyboardPanStep});
        break;
      case graphscore::KeyCode::kEquals:
        (void)navigation_.zoom_in(keyboard_zoom_focal());
        break;
      case graphscore::KeyCode::kMinus:
        (void)navigation_.zoom_out(keyboard_zoom_focal());
        break;
      default:
        consumed = false;
        break;
    }
    if (consumed) {
      return;
    }
  }
  if (delegate_ != nullptr) {
    delegate_->on_key_press(event);
  }
}

void CanvasGestureHandler::on_text_input(graphscore::TextInputEvent event) {
  if (delegate_ != nullptr) {
    delegate_->on_text_input(std::move(event));
  }
}

void CanvasGestureHandler::on_viewport_size_changed(double width,
                                                    double height) {
  controller_.set_window_center({width / 2.0, height / 2.0});
}

// Inverse-maps a pointer event's logical viewport coordinates to world
// (notation) coordinates before forwarding, so the delegate always interprets
// pointer positions in the notation space it lays out — regardless of pan or
// zoom. An unmappable coordinate (non-finite or a degenerate transform) is
// dropped rather than forwarded in the wrong coordinate space.
std::optional<graphscore::PointerEvent> CanvasGestureHandler::to_world_pointer(
    graphscore::PointerEvent event) const {
  const auto world = transform_.to_world({event.x, event.y});
  if (!world.has_value()) {
    return std::nullopt;
  }
  event.x = world->x;
  event.y = world->y;
  return event;
}

void CanvasGestureHandler::set_delegate(
    graphscore::InputHandler* delegate) noexcept {
  delegate_ = delegate;
}

void CanvasGestureHandler::set_focus_point_provider(
    const graphscore::FocusPointProvider* provider) noexcept {
  focus_provider_ = provider;
}

void CanvasGestureHandler::set_keyboard_focus(
    CanvasKeyboardFocus focus) noexcept {
  keyboard_focus_ = focus;
}

void CanvasGestureHandler::set_reset_selected_connector_handler(
    ResetSelectedConnectorHandler handler) {
  reset_selected_connector_handler_ = std::move(handler);
}

void CanvasGestureHandler::set_delete_selected_connector_handler(
    DeleteSelectedConnectorHandler handler) {
  delete_selected_connector_handler_ = std::move(handler);
}

void CanvasGestureHandler::set_focused_control(
    std::optional<graphscore::CanvasControlSelection> control) noexcept {
  focused_control_ = control;
}

void CanvasGestureHandler::set_node_play_handler(NodePlayHandler handler) {
  node_play_handler_ = std::move(handler);
}

void CanvasGestureHandler::set_window_center(
    graphscore::ViewportPosition center) noexcept {
  controller_.set_window_center(center);
}

const graphscore::ViewportTransform& CanvasGestureHandler::transform()
    const noexcept {
  return transform_;
}

const graphscore::TrackpadGestureController& CanvasGestureHandler::controller()
    const noexcept {
  return controller_;
}

graphscore::ViewportPosition CanvasGestureHandler::keyboard_zoom_focal() const {
  if (focus_provider_ != nullptr) {
    const auto bounds = focus_provider_->focused_world_bounds();
    if (bounds.has_value() && std::isfinite(bounds->origin.x) &&
        std::isfinite(bounds->origin.y) && std::isfinite(bounds->width) &&
        std::isfinite(bounds->height) && bounds->width >= 0.0 &&
        bounds->height >= 0.0) {
      const graphscore::GraphPosition center{
          bounds->origin.x + bounds->width / 2.0,
          bounds->origin.y + bounds->height / 2.0};
      if (std::isfinite(center.x) && std::isfinite(center.y)) {
        if (const auto viewport = transform_.to_viewport(center);
            viewport.has_value()) {
          return *viewport;
        }
      }
    }
  }
  return controller_.window_center();
}

}  // namespace graphscore::writer_app
