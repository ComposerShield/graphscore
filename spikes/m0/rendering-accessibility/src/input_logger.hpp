// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include <vector>
#include <string>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <functional>

namespace spike {

struct InputEvent {
    enum Type {
        PointerMove,
        ButtonDown,
        ButtonUp,
        Wheel,
        TrackpadScroll,
        FingerDown,
        FingerMotion,
        FingerUp,
        FingerCanceled
    };
    enum class Source { Unknown, PhysicalWheel, Trackpad, Finger, PinchGesture };
    Type type;
    Vec2 position;     // screen coords
    Vec2 delta;        // for scroll/wheel: delta; for move: delta since last
    int button = 0;    // mouse button number
    uint64_t timestampUs; // microsecond timestamp
    Source source = Source::Unknown;
};

class InputLogger {
public:
    // Injectable monotonic clock: returns microsecond timestamps.
    // Production default guarantees strictly increasing values.
    using ClockFn = std::function<uint64_t()>;

    InputLogger();
    explicit InputLogger(ClockFn clock);

    void RecordPointerMove(Vec2 pos, Vec2 delta);
    void RecordButtonDown(int button, Vec2 pos);
    void RecordButtonUp(int button, Vec2 pos);
    void RecordWheel(float x, float y, Vec2 pos,
                     InputEvent::Source src = InputEvent::Source::PhysicalWheel);
    void RecordTrackpadScroll(float dx, float dy,
                               InputEvent::Source src = InputEvent::Source::Finger);
    // Diagnostic SDL_EVENT_FINGER_* telemetry. It is distinct from scroll
    // records and never contributes to scrollEventCount.
    void RecordFingerTelemetry(InputEvent::Type type, Vec2 pos, Vec2 delta);

    const std::vector<InputEvent>& Events() const { return m_events; }

    // Save log to file (CSV-like)
    bool SaveLog(const char* path) const;

    // Compute aggregate metrics
    struct AggregateMetrics {
        int moveEventCount = 0;
        int scrollEventCount = 0;
        double totalTimeS = 0.0;
        double moveFrequencyHz = 0.0;
        double avgDeltaX = 0.0;
        double avgDeltaY = 0.0;
    };
    AggregateMetrics ComputeMetrics() const;

    // Compute input-fidelity summary against explicit criteria.
    // This is the structured pass/fail report for manual trackpad capture.
    struct FidelitySummary {
        // Enforceable physical-capture thresholds.
        static constexpr int kMinTotalEventCount = 50;
        static constexpr int kMinScrollEventCount = 30;
        static constexpr double kMinDurationS = 0.5;
        static constexpr double kMinEventRateHz = 0.5;

        bool monotonicTimestamps = true;
        bool allEventsHaveTimestamps = true;
        int totalEvents = 0;
        int scrollEventCount = 0;
        // Diagnostic count of SDL_EVENT_FINGER_DOWN, _MOTION, _UP, and
        // _CANCELED records.
        // It is deliberately not a pass threshold: on macOS, SDL may deliver
        // working two-finger trackpad pan/pinch through scroll and pinch
        // streams without low-level finger records.
        int fingerEventCount = 0;
        double captureDurationS = 0.0;
        double peakEventRateHz = 0.0;
        bool hasSubpixelDeltas = false;
        double minDeltaX = 0.0, maxDeltaX = 0.0;
        double minDeltaY = 0.0, maxDeltaY = 0.0;
        int droppedOrInvalid = 0;
        bool hasGestureEvent = false;
        bool hasNonzeroViewportResponse = false;
        bool hasAppliedPanOutcome = false;
        bool hasAppliedPinchOutcome = false;
        std::string verdict;  // PASS / FAIL / INCONCLUSIVE / TEMPLATE
    };
    FidelitySummary ComputeFidelitySummary(bool hasNonzeroViewportResponse,
                                           bool hasAppliedPanOutcome,
                                           bool hasAppliedPinchOutcome) const;

    // Production monotonic clock — guarantees strictly increasing µs timestamps.
    static uint64_t MonotonicNowUs();

private:
    std::vector<InputEvent> m_events;
    ClockFn m_clock;
};

} // namespace spike
