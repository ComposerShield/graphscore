// SPDX-License-Identifier: Apache-2.0

#include "canvas_gesture_handler.hpp"

#include <type_traits>
#include <utility>

namespace graphscore::writer_app {

static_assert(!std::is_copy_constructible_v<CanvasGestureHandler>);
static_assert(!std::is_move_constructible_v<CanvasGestureHandler>);
static_assert(!std::is_copy_assignable_v<CanvasGestureHandler>);
static_assert(!std::is_move_assignable_v<CanvasGestureHandler>);

CanvasGestureHandler::CanvasGestureHandler() : controller_(transform_) {}

void CanvasGestureHandler::on_scroll(graphscore::ScrollDelta delta) {
  // Gesture application is fire-and-forget: a rejected delta (non-finite,
  // unrepresentable) simply leaves the viewport unchanged.
  (void)controller_.pan(delta);
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
  if (delegate_ == nullptr) {
    return;
  }
  if (const auto mapped = to_world_pointer(event); mapped.has_value()) {
    delegate_->on_pointer_press(*mapped);
  }
}

void CanvasGestureHandler::on_pointer_move(graphscore::PointerEvent event) {
  if (delegate_ == nullptr) {
    return;
  }
  if (const auto mapped = to_world_pointer(event); mapped.has_value()) {
    delegate_->on_pointer_move(*mapped);
  }
}

void CanvasGestureHandler::on_pointer_release(graphscore::PointerEvent event) {
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
  if (delegate_ != nullptr) {
    delegate_->on_cancel();
  }
}

void CanvasGestureHandler::on_key_press(graphscore::KeyEvent event) {
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

}  // namespace graphscore::writer_app
