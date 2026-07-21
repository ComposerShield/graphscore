// SPDX-License-Identifier: Apache-2.0

#include "viewport_event_handler.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>

namespace spike {

namespace {

void DeactivateFinger(FingerState& first, FingerState& second, uint64_t id) {
    if (first.active && first.id == id) {
        first.active = false;
    } else if (second.active && second.id == id) {
        second.active = false;
    }
}

} // namespace

ViewportEventResult ProcessViewportSDLEvent(const void* sdlEvent,
                                               ViewportEventState& s) {
    const auto& event = *static_cast<const SDL_Event*>(sdlEvent);
    ViewportEventResult result{};
    result.handled = true;

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        s.lastMousePos = {event.motion.x, event.motion.y};
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordPointerMove(
                s.lastMousePos,
                {event.motion.xrel, event.motion.yrel});
        }
        return result;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        s.mouseDown = true;
        s.lastMousePos = {event.button.x, event.button.y};
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordButtonDown(
                static_cast<int>(event.button.button),
                s.lastMousePos);
        }
        return result;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        s.mouseDown = false;
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordButtonUp(
                static_cast<int>(event.button.button),
                s.lastMousePos);
        }
        return result;

    case SDL_EVENT_MOUSE_WHEEL: {
        // Default wheel / two-finger scroll pans the canvas.
        // Pinch events (SDL_EVENT_PINCH_*) exclusively control zoom.
        // Use declaredSource from --input-device when set; otherwise Unknown.
        float dx = static_cast<float>(event.wheel.x);
        float dy = static_cast<float>(event.wheel.y);
        constexpr float kPanSpeed = 20.0f;
        bool hasDelta = (dx != 0.0f || dy != 0.0f);
        if (hasDelta) {
            Transform newXf = s.viewTransform;
            newXf.offset.x += dx * kPanSpeed;
            newXf.offset.y += dy * kPanSpeed;
            s.viewTransform = newXf;
            if (s.onApplyTransform) {
                s.onApplyTransform(s.onApplyTransformUserData, newXf);
            }
            result.viewportChanged = true;
            result.didPan = true;
        }

        if (s.captureInput && s.inputLogger) {
            InputEvent::Source src = s.declaredSource;
            s.inputLogger->RecordWheel(dx, dy,
                {static_cast<float>(event.wheel.mouse_x),
                 static_cast<float>(event.wheel.mouse_y)},
                src);
        }
        return result;
    }

    case SDL_EVENT_FINGER_DOWN: {
        const auto& f = event.tfinger;
        if (!s.finger1.active) {
            s.finger1 = {f.fingerID, f.x, f.y, true};
        } else if (!s.finger2.active) {
            s.finger2 = {f.fingerID, f.x, f.y, true};
        }
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordFingerTelemetry(InputEvent::FingerDown,
                                                 {f.x, f.y}, {f.dx, f.dy});
        }
        return result;
    }

    case SDL_EVENT_FINGER_UP: {
        const auto& f = event.tfinger;
        DeactivateFinger(s.finger1, s.finger2, f.fingerID);
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordFingerTelemetry(InputEvent::FingerUp,
                                                 {f.x, f.y}, {f.dx, f.dy});
        }
        return result;
    }

    case SDL_EVENT_FINGER_MOTION: {
        const auto& f = event.tfinger;

        float oldCx = 0.0f, oldCy = 0.0f;
        if (s.finger1.active && s.finger2.active) {
            oldCx = (s.finger1.x + s.finger2.x) * 0.5f;
            oldCy = (s.finger1.y + s.finger2.y) * 0.5f;
        }

        if (s.finger1.active && s.finger1.id == f.fingerID) {
            s.finger1.x = f.x;
            s.finger1.y = f.y;
        } else if (s.finger2.active && s.finger2.id == f.fingerID) {
            s.finger2.x = f.x;
            s.finger2.y = f.y;
        }

        if (s.finger1.active && s.finger2.active) {
            float curCx = (s.finger1.x + s.finger2.x) * 0.5f;
            float curCy = (s.finger1.y + s.finger2.y) * 0.5f;

            // Centroid pan only: two-finger translation.
            // Zoom is exclusively handled by SDL_EVENT_PINCH_UPDATE.
            float pDeltaX = (curCx - oldCx) * static_cast<float>(s.winW);
            float pDeltaY = (curCy - oldCy) * static_cast<float>(s.winH);
            if (pDeltaX != 0.0f || pDeltaY != 0.0f) {
                Transform newXf = s.viewTransform;
                newXf.offset.x += pDeltaX;
                newXf.offset.y += pDeltaY;
                s.viewTransform = newXf;
                if (s.onApplyTransform) {
                    s.onApplyTransform(s.onApplyTransformUserData, newXf);
                }
                result.viewportChanged = true;
                result.didPan = true;
            }
        }
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordFingerTelemetry(InputEvent::FingerMotion,
                                                 {f.x, f.y}, {f.dx, f.dy});
        }
        return result;
    }

    case SDL_EVENT_FINGER_CANCELED: {
        const auto& f = event.tfinger;
        // SDL cancellation identifies one finger, just like SDL_EVENT_FINGER_UP.
        // It ends that tracked finger without synthesizing viewport motion.
        DeactivateFinger(s.finger1, s.finger2, f.fingerID);
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordFingerTelemetry(InputEvent::FingerCanceled,
                                                 {f.x, f.y}, {f.dx, f.dy});
        }
        return result;
    }

    case SDL_EVENT_PINCH_BEGIN:
        return result;

    case SDL_EVENT_PINCH_UPDATE: {
        float rawScale = event.pinch.scale;

        // Pinch validity: reject non-finite, zero, or negative scale.
        if (!std::isfinite(rawScale) || rawScale <= 0.0f) {
            return result;  // no mutation, not a valid pinch
        }

        // Resolve the pinch focal point (screen coordinates):
        // 1. Event focus_x/focus_y (≥0 on mobile; -1 otherwise)
        // 2. Active two-finger centroid (if fingers are tracked)
        // 3. Window center (explicit fallback)
        float cx, cy;
        if (event.pinch.focus_x >= 0.0f && event.pinch.focus_y >= 0.0f) {
            cx = event.pinch.focus_x;
            cy = event.pinch.focus_y;
        } else if (s.finger1.active && s.finger2.active) {
            cx = (s.finger1.x + s.finger2.x) * 0.5f * static_cast<float>(s.winW);
            cy = (s.finger1.y + s.finger2.y) * 0.5f * static_cast<float>(s.winH);
        } else {
            cx = static_cast<float>(s.winW) * 0.5f;
            cy = static_cast<float>(s.winH) * 0.5f;
        }

        Vec2 worldAtC = s.viewTransform.ScreenToWorld({cx, cy});
        Transform newXf = s.viewTransform;
        newXf.scale *= rawScale;
        float clamped = std::max(0.1f, std::min(20.0f, newXf.scale));
        newXf.scale = clamped;

        // Only record mutation if scale actually changed (tight epsilon).
        constexpr float kScaleEpsilon = 1e-6f;
        bool scaleChanged = std::abs(clamped - s.viewTransform.scale) > kScaleEpsilon;

        if (!scaleChanged) {
            return result;  // scale unchanged (already at clamp boundary, or factor=1)
        }

        Vec2 newScreen = newXf.WorldToScreen(worldAtC);
        newXf.offset.x += cx - newScreen.x;
        newXf.offset.y += cy - newScreen.y;
        s.viewTransform = newXf;
        if (s.onApplyTransform) {
            s.onApplyTransform(s.onApplyTransformUserData, newXf);
        }
        if (s.captureInput && s.inputLogger) {
            s.inputLogger->RecordTrackpadScroll(0.0f, rawScale,
                InputEvent::Source::PinchGesture);
        }
        result.viewportChanged = true;
        result.didPinch = true;
        return result;
    }

    case SDL_EVENT_PINCH_END:
        return result;

    case SDL_EVENT_KEY_DOWN: {
        Transform xf = s.viewTransform;
        if (event.key.key == SDLK_LEFT)  { xf.offset.x -= 20; result.viewportChanged = true; }
        if (event.key.key == SDLK_RIGHT) { xf.offset.x += 20; result.viewportChanged = true; }
        if (event.key.key == SDLK_UP)    { xf.offset.y -= 20; result.viewportChanged = true; }
        if (event.key.key == SDLK_DOWN)  { xf.offset.y += 20; result.viewportChanged = true; }
        if (event.key.key == SDLK_0)     { xf = {1.0f, {0, 0}}; result.viewportChanged = true; }
        if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS) {
            xf.scale *= 1.2f; result.viewportChanged = true;
        }
        if (event.key.key == SDLK_MINUS) {
            xf.scale *= 0.8f;
            xf.scale = std::max(0.1f, xf.scale);
            result.viewportChanged = true;
        }
        if (result.viewportChanged) {
            s.viewTransform = xf;
            if (s.onApplyTransform) {
                s.onApplyTransform(s.onApplyTransformUserData, xf);
            }
        }
        // Keyboard never sets didPan/didPinch — those are physical-trackpad only.
        return result;
    }

    case SDL_EVENT_QUIT:
        result.quit = true;
        return result;

    default:
        result.handled = false;
        break;
    }
    return result;
}

} // namespace spike
