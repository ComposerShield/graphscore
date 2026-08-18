// SPDX-License-Identifier: Apache-2.0

#include "writer_shell_internal.hpp"

#include <cstdint>
#include <string>
#include <utility>

#ifdef GRAPHSCORE_HAVE_SDL3
#include <SDL3/SDL.h>  // NOLINT(build/include_order)
#endif

namespace graphscore {

void WriterShell::dispatch_test_pointer_event(std::uint8_t kind,
                                              PointerEvent event) {
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  const double scale =
      impl_->test_dpi_scale != 0.0 ? impl_->test_dpi_scale : 1.0;
  if (scale > 0.0) {
    event.x /= scale;
    event.y /= scale;
  }
  switch (kind) {
    case 0:
      handler->on_pointer_press(event);
      break;
    case 1:
      handler->on_pointer_move(event);
      break;
    case 2:
      handler->on_pointer_release(event);
      break;
    case 3:
      handler->on_cancel();
      break;
    default:
      break;
  }
}

void WriterShell::dispatch_test_key_event(KeyEvent event) {
  if (impl_->input_handler != nullptr) {
    impl_->input_handler->on_key_press(event);
  }
}

void WriterShell::dispatch_test_text_input(TextInputEvent event) {
  if (impl_->input_handler != nullptr) {
    impl_->input_handler->on_text_input(std::move(event));
  }
}

void WriterShell::dispatch_test_viewport_resize(double width, double height) {
  if (impl_->input_handler != nullptr) {
    impl_->input_handler->on_viewport_size_changed(width, height);
  }
}

#ifdef GRAPHSCORE_HAVE_SDL3

namespace {

[[nodiscard]] PointerButton sdl_button_to_pointer_button(
    std::uint8_t button) noexcept {
  switch (button) {
    case SDL_BUTTON_LEFT:
      return PointerButton::kPrimary;
    case SDL_BUTTON_RIGHT:
      return PointerButton::kSecondary;
    case SDL_BUTTON_MIDDLE:
      return PointerButton::kMiddle;
    default:
      return PointerButton::kUnknown;
  }
}

// Physical scancodes keep navigation, symbol, digit, and numpad bindings
// layout-independent. Everything outside the action table maps to unknown.
[[nodiscard]] KeyCode sdl_scancode_to_key_code(SDL_Scancode scancode) noexcept {
  switch (scancode) {
    case SDL_SCANCODE_LEFT:
      return KeyCode::kLeft;
    case SDL_SCANCODE_RIGHT:
      return KeyCode::kRight;
    case SDL_SCANCODE_UP:
      return KeyCode::kUp;
    case SDL_SCANCODE_DOWN:
      return KeyCode::kDown;
    case SDL_SCANCODE_HOME:
      return KeyCode::kHome;
    case SDL_SCANCODE_END:
      return KeyCode::kEnd;
    case SDL_SCANCODE_MINUS:
      return KeyCode::kMinus;
    case SDL_SCANCODE_EQUALS:
      return KeyCode::kEquals;
    case SDL_SCANCODE_BACKSPACE:
      return KeyCode::kBackspace;
    case SDL_SCANCODE_DELETE:
      return KeyCode::kDelete;
    case SDL_SCANCODE_R:
      return KeyCode::kR;
    case SDL_SCANCODE_1:
      return KeyCode::kDigit1;
    case SDL_SCANCODE_2:
      return KeyCode::kDigit2;
    case SDL_SCANCODE_3:
      return KeyCode::kDigit3;
    case SDL_SCANCODE_4:
      return KeyCode::kDigit4;
    case SDL_SCANCODE_5:
      return KeyCode::kDigit5;
    case SDL_SCANCODE_6:
      return KeyCode::kDigit6;
    case SDL_SCANCODE_7:
      return KeyCode::kDigit7;
    case SDL_SCANCODE_8:
      return KeyCode::kDigit8;
    case SDL_SCANCODE_9:
      return KeyCode::kDigit9;
    case SDL_SCANCODE_0:
      return KeyCode::kDigit0;
    case SDL_SCANCODE_RETURN:
      return KeyCode::kReturn;
    case SDL_SCANCODE_ESCAPE:
      return KeyCode::kEscape;
    case SDL_SCANCODE_TAB:
      return KeyCode::kTab;
    case SDL_SCANCODE_SPACE:
      return KeyCode::kSpace;
    case SDL_SCANCODE_KP_1:
      return KeyCode::kNumPad1;
    case SDL_SCANCODE_KP_2:
      return KeyCode::kNumPad2;
    case SDL_SCANCODE_KP_3:
      return KeyCode::kNumPad3;
    case SDL_SCANCODE_KP_4:
      return KeyCode::kNumPad4;
    case SDL_SCANCODE_KP_5:
      return KeyCode::kNumPad5;
    case SDL_SCANCODE_KP_6:
      return KeyCode::kNumPad6;
    case SDL_SCANCODE_KP_7:
      return KeyCode::kNumPad7;
    case SDL_SCANCODE_KP_0:
      return KeyCode::kNumPad0;
    case SDL_SCANCODE_KP_PERIOD:
      return KeyCode::kNumPadDecimal;
    default:
      return KeyCode::kUnknown;
  }
}

// Letter mnemonics use SDL's layout-mapped logical keycode, not the physical
// position. Only letters bound by the action table are represented.
[[nodiscard]] LogicalKey sdl_keycode_to_logical_key(
    SDL_Keycode keycode) noexcept {
  switch (keycode) {
    case SDLK_A:
      return LogicalKey::kA;
    case SDLK_B:
      return LogicalKey::kB;
    case SDLK_C:
      return LogicalKey::kC;
    case SDLK_D:
      return LogicalKey::kD;
    case SDLK_E:
      return LogicalKey::kE;
    case SDLK_F:
      return LogicalKey::kF;
    case SDLK_G:
      return LogicalKey::kG;
    case SDLK_N:
      return LogicalKey::kN;
    case SDLK_R:
      return LogicalKey::kR;
    case SDLK_X:
      return LogicalKey::kX;
    case SDLK_V:
      return LogicalKey::kV;
    case SDLK_Z:
      return LogicalKey::kZ;
    case SDLK_K:
      return LogicalKey::kK;
    default:
      return LogicalKey::kUnknown;
  }
}

[[nodiscard]] KeyModifiers sdl_keymod_to_key_modifiers(
    SDL_Keymod modifiers) noexcept {
  KeyModifiers result;
  result.shift   = (modifiers & SDL_KMOD_SHIFT) != 0;
  result.control = (modifiers & SDL_KMOD_CTRL) != 0;
  result.alt     = (modifiers & SDL_KMOD_ALT) != 0;
  result.meta    = (modifiers & SDL_KMOD_GUI) != 0;
  return result;
}

void dispatch_sdl_event(InputHandler* handler, SDL_Renderer* renderer,
                        KeyModifiers* ordered_modifiers, SDL_Event event,
                        bool force_pinch_conversion_failure) {
  if (ordered_modifiers != nullptr) {
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
      *ordered_modifiers = sdl_keymod_to_key_modifiers(event.key.mod);
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
               event.type == SDL_EVENT_QUIT ||
               event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
      *ordered_modifiers = {};
    }
  }
  if (handler == nullptr) {
    return;
  }

  // Convert pixel/window event coordinates to the renderer's logical space.
  // A conversion failure transactionally drops the event.
  if (renderer != nullptr &&
      !SDL_ConvertEventToRenderCoordinates(renderer, &event)) {
    return;
  }

  switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      PointerEvent pointer;
      pointer.x      = static_cast<double>(event.button.x);
      pointer.y      = static_cast<double>(event.button.y);
      pointer.button = sdl_button_to_pointer_button(event.button.button);
      pointer.measure_selection =
          ordered_modifiers != nullptr && ordered_modifiers->shift;
      handler->on_pointer_press(pointer);
      break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
      PointerEvent pointer;
      pointer.x = static_cast<double>(event.motion.x);
      pointer.y = static_cast<double>(event.motion.y);
      pointer.measure_selection =
          ordered_modifiers != nullptr && ordered_modifiers->shift;
      handler->on_pointer_move(pointer);
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      PointerEvent pointer;
      pointer.x      = static_cast<double>(event.button.x);
      pointer.y      = static_cast<double>(event.button.y);
      pointer.button = sdl_button_to_pointer_button(event.button.button);
      pointer.measure_selection =
          ordered_modifiers != nullptr && ordered_modifiers->shift;
      handler->on_pointer_release(pointer);
      break;
    }
    case SDL_EVENT_KEY_DOWN: {
      KeyEvent key;
      key.code      = sdl_scancode_to_key_code(event.key.scancode);
      key.modifiers = sdl_keymod_to_key_modifiers(event.key.mod);
      key.repeat    = event.key.repeat;
      key.logical   = sdl_keycode_to_logical_key(event.key.key);
      handler->on_key_press(key);
      break;
    }
    case SDL_EVENT_TEXT_INPUT: {
      TextInputEvent text;
      text.text = event.text.text != nullptr ? event.text.text : "";
      handler->on_text_input(text);
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
      WheelEvent wheel;
      wheel.delta.x = static_cast<double>(event.wheel.x);
      wheel.delta.y = static_cast<double>(event.wheel.y);
      if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        wheel.delta.x = -wheel.delta.x;
        wheel.delta.y = -wheel.delta.y;
      }
      wheel.pointer = {static_cast<double>(event.wheel.mouse_x),
                       static_cast<double>(event.wheel.mouse_y)};
      if (ordered_modifiers != nullptr) {
        wheel.modifiers = *ordered_modifiers;
      }
      handler->on_wheel(wheel);
      break;
    }
    case SDL_EVENT_PINCH_UPDATE: {
      PinchUpdate update;
      update.scale = static_cast<double>(event.pinch.scale);
      if (event.pinch.focus_x >= 0.0F && event.pinch.focus_y >= 0.0F) {
        float focus_x = event.pinch.focus_x;
        float focus_y = event.pinch.focus_y;
        if (force_pinch_conversion_failure) {
          return;
        }
        if (renderer != nullptr &&
            !SDL_RenderCoordinatesFromWindow(renderer, focus_x, focus_y,
                                             &focus_x, &focus_y)) {
          return;
        }
        update.focal_point = ViewportPosition{static_cast<double>(focus_x),
                                              static_cast<double>(focus_y)};
      }
      handler->on_pinch(update);
      break;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION: {
      FingerContact finger;
      finger.finger_id = static_cast<std::uint64_t>(event.tfinger.fingerID);
      finger.position  = ViewportPosition{static_cast<double>(event.tfinger.x),
                                         static_cast<double>(event.tfinger.y)};
      if (event.type == SDL_EVENT_FINGER_DOWN) {
        handler->on_finger_down(finger);
      } else {
        handler->on_finger_move(finger);
      }
      break;
    }
    case SDL_EVENT_FINGER_UP:
      handler->on_finger_up(static_cast<std::uint64_t>(event.tfinger.fingerID));
      break;
    case SDL_EVENT_FINGER_CANCELED:
      handler->on_finger_canceled(
          static_cast<std::uint64_t>(event.tfinger.fingerID));
      break;
    default:
      break;
  }
}

}  // namespace

void dispatch_platform_event(InputHandler* handler, void* renderer,
                             KeyModifiers* ordered_modifiers, const void* event,
                             bool force_pinch_conversion_failure) {
  dispatch_sdl_event(handler, static_cast<SDL_Renderer*>(renderer),
                     ordered_modifiers, *static_cast<const SDL_Event*>(event),
                     force_pinch_conversion_failure);
}

void initialise_platform_modifiers(KeyModifiers* ordered_modifiers) {
  if (ordered_modifiers != nullptr) {
    *ordered_modifiers = sdl_keymod_to_key_modifiers(SDL_GetModState());
  }
}

void deliver_viewport_size(void* window, InputHandler* handler) {
  if (window == nullptr || handler == nullptr) {
    return;
  }
  int width  = 0;
  int height = 0;
  if (!SDL_GetWindowSize(static_cast<SDL_Window*>(window), &width, &height)) {
    return;
  }
  handler->on_viewport_size_changed(static_cast<double>(width),
                                    static_cast<double>(height));
}

#endif

void WriterShell::dispatch_sdl_test_pointer_event(std::uint8_t kind,
                                                  PointerEvent event) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  const SDL_WindowID window_id =
      SDL_GetWindowID(static_cast<SDL_Window*>(impl_->window));
  SDL_Event sdl_event{};
  switch (kind) {
    case 0:
      sdl_event.type            = SDL_EVENT_MOUSE_BUTTON_DOWN;
      sdl_event.button.windowID = window_id;
      sdl_event.button.x        = static_cast<float>(event.x);
      sdl_event.button.y        = static_cast<float>(event.y);
      sdl_event.button.button   = SDL_BUTTON_LEFT;
      sdl_event.button.down     = true;
      break;
    case 1:
      sdl_event.type            = SDL_EVENT_MOUSE_MOTION;
      sdl_event.motion.windowID = window_id;
      sdl_event.motion.x        = static_cast<float>(event.x);
      sdl_event.motion.y        = static_cast<float>(event.y);
      break;
    case 2:
      sdl_event.type            = SDL_EVENT_MOUSE_BUTTON_UP;
      sdl_event.button.windowID = window_id;
      sdl_event.button.x        = static_cast<float>(event.x);
      sdl_event.button.y        = static_cast<float>(event.y);
      sdl_event.button.button   = SDL_BUTTON_LEFT;
      sdl_event.button.down     = false;
      break;
    case 3:
      handler->on_cancel();
      return;
    default:
      return;
  }
  dispatch_sdl_event(handler, static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, sdl_event,
                     /*force_pinch_conversion_failure=*/false);
#else
  (void)kind;
  (void)event;
#endif
}

std::optional<ViewportPosition> WriterShell::test_window_to_render_point(
    double x, double y) const {
#ifdef GRAPHSCORE_HAVE_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(impl_->renderer);
  if (renderer == nullptr) {
    return std::nullopt;
  }
  float render_x = 0.0F;
  float render_y = 0.0F;
  if (!SDL_RenderCoordinatesFromWindow(renderer, static_cast<float>(x),
                                       static_cast<float>(y), &render_x,
                                       &render_y)) {
    return std::nullopt;
  }
  return ViewportPosition{static_cast<double>(render_x),
                          static_cast<double>(render_y)};
#else
  (void)x;
  (void)y;
  return std::nullopt;
#endif
}

std::optional<ViewportPosition> WriterShell::test_render_to_window_point(
    double x, double y) const {
#ifdef GRAPHSCORE_HAVE_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(impl_->renderer);
  if (renderer == nullptr) {
    return std::nullopt;
  }
  float window_x = 0.0F;
  float window_y = 0.0F;
  if (!SDL_RenderCoordinatesToWindow(renderer, static_cast<float>(x),
                                     static_cast<float>(y), &window_x,
                                     &window_y)) {
    return std::nullopt;
  }
  return ViewportPosition{static_cast<double>(window_x),
                          static_cast<double>(window_y)};
#else
  (void)x;
  (void)y;
  return std::nullopt;
#endif
}

void WriterShell::dispatch_sdl_test_key_event(std::uint32_t scancode,
                                              std::uint16_t modifiers,
                                              std::uint32_t keycode) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  SDL_Event event{};
  event.type         = SDL_EVENT_KEY_DOWN;
  event.key.scancode = static_cast<SDL_Scancode>(scancode);
  event.key.mod      = static_cast<SDL_Keymod>(modifiers);
  event.key.key      = static_cast<SDL_Keycode>(keycode);
  event.key.down     = true;
  dispatch_sdl_event(handler, static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, event,
                     /*force_pinch_conversion_failure=*/false);
#else
  (void)scancode;
  (void)modifiers;
  (void)keycode;
#endif
}

void WriterShell::dispatch_sdl_test_modifier_transition(KeyModifiers modifiers,
                                                        bool         key_down) {
#ifdef GRAPHSCORE_HAVE_SDL3
  SDL_Keymod mask = SDL_KMOD_NONE;
  if (modifiers.shift) {
    mask |= SDL_KMOD_LSHIFT;
  }
  if (modifiers.control) {
    mask |= SDL_KMOD_LCTRL;
  }
  if (modifiers.alt) {
    mask |= SDL_KMOD_LALT;
  }
  if (modifiers.meta) {
    mask |= SDL_KMOD_LGUI;
  }
  SDL_Event event{};
  event.type     = key_down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.mod  = mask;
  event.key.down = key_down;
  dispatch_sdl_event(impl_->input_handler,
                     static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, event,
                     /*force_pinch_conversion_failure=*/false);
#else
  (void)modifiers;
  (void)key_down;
#endif
}

void WriterShell::dispatch_sdl_test_focus_loss() {
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->input_handler != nullptr) {
    impl_->input_handler->on_cancel();
  }
  SDL_Event event{};
  event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  dispatch_sdl_event(impl_->input_handler,
                     static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, event,
                     /*force_pinch_conversion_failure=*/false);
#endif
}

void WriterShell::dispatch_sdl_test_text_input(std::string_view text) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  std::string owned(text);
  SDL_Event   event{};
  event.type      = SDL_EVENT_TEXT_INPUT;
  event.text.text = owned.c_str();
  dispatch_sdl_event(handler, static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, event,
                     /*force_pinch_conversion_failure=*/false);
#else
  (void)text;
#endif
}

void WriterShell::dispatch_sdl_test_scroll_event(double x, double y,
                                                 std::uint32_t direction,
                                                 double        pointer_x,
                                                 double        pointer_y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  SDL_Event event{};
  event.type            = SDL_EVENT_MOUSE_WHEEL;
  event.wheel.x         = static_cast<float>(x);
  event.wheel.y         = static_cast<float>(y);
  event.wheel.direction = static_cast<SDL_MouseWheelDirection>(direction);
  event.wheel.mouse_x   = static_cast<float>(pointer_x);
  event.wheel.mouse_y   = static_cast<float>(pointer_y);
  const SDL_Keymod prior_modifiers = SDL_GetModState();
  SDL_SetModState(SDL_KMOD_NONE);
  dispatch_sdl_event(handler, static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, event,
                     /*force_pinch_conversion_failure=*/false);
  SDL_SetModState(prior_modifiers);
#else
  (void)x;
  (void)y;
  (void)direction;
  (void)pointer_x;
  (void)pointer_y;
#endif
}

void WriterShell::dispatch_sdl_test_pinch_event(double scale, double focus_x,
                                                double focus_y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  SDL_Event event{};
  event.type          = SDL_EVENT_PINCH_UPDATE;
  event.pinch.scale   = static_cast<float>(scale);
  event.pinch.focus_x = static_cast<float>(focus_x);
  event.pinch.focus_y = static_cast<float>(focus_y);
  dispatch_sdl_event(handler, static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, event,
                     impl_->test_force_pinch_conversion_failure);
#else
  (void)scale;
  (void)focus_x;
  (void)focus_y;
#endif
}

void WriterShell::dispatch_sdl_test_finger_event(std::uint32_t kind,
                                                 std::uint64_t finger_id,
                                                 double x, double y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  SDL_Event event{};
  switch (kind) {
    case 0:
      event.type = SDL_EVENT_FINGER_DOWN;
      break;
    case 1:
      event.type = SDL_EVENT_FINGER_MOTION;
      break;
    case 2:
      event.type = SDL_EVENT_FINGER_UP;
      break;
    case 3:
      event.type = SDL_EVENT_FINGER_CANCELED;
      break;
    default:
      return;
  }
  event.tfinger.fingerID = static_cast<SDL_FingerID>(finger_id);
  event.tfinger.x        = static_cast<float>(x);
  event.tfinger.y        = static_cast<float>(y);
  dispatch_sdl_event(handler, static_cast<SDL_Renderer*>(impl_->renderer),
                     &impl_->ordered_modifiers, event,
                     /*force_pinch_conversion_failure=*/false);
#else
  (void)kind;
  (void)finger_id;
  (void)x;
  (void)y;
#endif
}

void WriterShell::set_test_force_pinch_conversion_failure(bool force) {
#ifdef GRAPHSCORE_HAVE_SDL3
  impl_->test_force_pinch_conversion_failure = force;
#else
  (void)force;
#endif
}

}  // namespace graphscore
