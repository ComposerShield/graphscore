// SPDX-License-Identifier: Apache-2.0
//
// Standalone SDL event processor testable from self-tests.
// Accepts real SDL_Event values and deterministic window dimensions.
// Production interactive/input-log path delegates to the same function.

#pragma once

#include "types.hpp"
#include "input_logger.hpp"

#include <cstdint>

namespace spike {

/// Callback invoked when the viewport transform is mutated.
/// The production implementation calls SyncGeometry on the bridge.
using ApplyTransformCallback = void (*)(void* userData,
                                         const Transform& newXf);

struct FingerState {
    uint64_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
};

struct ViewportEventState {
    Transform& viewTransform;
    InputLogger* inputLogger = nullptr;
    bool captureInput = false;
    Vec2& lastMousePos;
    bool& mouseDown;
    FingerState& finger1;
    FingerState& finger2;
    int winW = 800;
    int winH = 600;
    ApplyTransformCallback onApplyTransform = nullptr;
    void* onApplyTransformUserData = nullptr;
    /// Explicit user-declared input device source (--input-device flag).
    /// When set, wheel events recorded during capture use this source
    /// instead of Source::Unknown.  Finger and pinch events use their
    /// own source types regardless of this override.
    InputEvent::Source declaredSource = InputEvent::Source::Unknown;
};

/// Typed per-event mutation result used by App input-log accumulation.
/// Never make final offset/scale comparisons — accumulate these fields.
struct ViewportEventResult {
    bool handled = false;         ///< Event type was matched by the handler
    bool viewportChanged = false; ///< Transform actually mutated
    bool didPan = false;          ///< Offset changed from wheel or finger centriod
    bool didPinch = false;        ///< Scale changed from native SDL pinch event
    bool quit = false;            ///< Quit event received
};

/// Shared accumulator — App owns one instance, tests use the same type.
/// No duplicate accumulator logic; all fidelity pathways feed into this.
struct ViewportOutcomeAccumulator {
    bool hadPan   = false;
    bool hadPinch = false;

    /// Accumulate outcomes from a typed mutation result.
    void Accumulate(const ViewportEventResult& r) {
        if (r.didPan)   hadPan   = true;
        if (r.didPinch) hadPinch = true;
    }

    bool HasPan()   const { return hadPan; }
    bool HasPinch() const { return hadPinch; }

    void Reset() { hadPan = false; hadPinch = false; }
};

/// Process a single SDL_Event through the same code path used by
/// interactive mode and input-log mode.  Returns typed per-event
/// mutation result.  App accumulates didPan / didPinch from this struct,
/// never by comparing final offset/scale.
ViewportEventResult ProcessViewportSDLEvent(const void* sdlEvent,
                                             ViewportEventState& state);

} // namespace spike
