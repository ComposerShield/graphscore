// SPDX-License-Identifier: Apache-2.0

#include "input_logger.hpp"

namespace spike {

InputLogger::InputLogger()
    : m_clock(MonotonicNowUs) {}

InputLogger::InputLogger(ClockFn clock)
    : m_clock(std::move(clock)) {}

void InputLogger::RecordPointerMove(Vec2 pos, Vec2 delta) {
    m_events.push_back({InputEvent::PointerMove, pos, delta, 0, m_clock(),
                         InputEvent::Source::Unknown});
}

void InputLogger::RecordButtonDown(int button, Vec2 pos) {
    m_events.push_back({InputEvent::ButtonDown, pos, {0, 0}, button, m_clock(),
                         InputEvent::Source::Unknown});
}

void InputLogger::RecordButtonUp(int button, Vec2 pos) {
    m_events.push_back({InputEvent::ButtonUp, pos, {0, 0}, button, m_clock(),
                         InputEvent::Source::Unknown});
}

void InputLogger::RecordWheel(float x, float y, Vec2 pos,
                               InputEvent::Source src) {
    m_events.push_back({InputEvent::Wheel, pos, {x, y}, 0, m_clock(), src});
}

void InputLogger::RecordTrackpadScroll(float dx, float dy,
                                         InputEvent::Source src) {
    m_events.push_back({InputEvent::TrackpadScroll, {0, 0}, {dx, dy}, 0, m_clock(), src});
}

void InputLogger::RecordFingerTelemetry(InputEvent::Type type, Vec2 pos, Vec2 delta) {
    m_events.push_back({type, pos, delta, 0, m_clock(), InputEvent::Source::Finger});
}

uint64_t InputLogger::MonotonicNowUs() {
    static uint64_t s_lastUs = 0;
    auto now = std::chrono::steady_clock::now();
    auto us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());
    // Guarantee strictly increasing: if clock hasn't advanced at least 1 µs,
    // bump by 1.
    if (us <= s_lastUs) {
        us = s_lastUs + 1;
    }
    s_lastUs = us;
    return us;
}

bool InputLogger::SaveLog(const char* path) const {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;

    std::fprintf(f, "type,button,x,y,dx,dy,timestamp_us,source\n");
    for (const auto& ev : m_events) {
        const char* typeStr = "?";
        switch (ev.type) {
        case InputEvent::PointerMove:   typeStr = "move";   break;
        case InputEvent::ButtonDown:    typeStr = "down";   break;
        case InputEvent::ButtonUp:      typeStr = "up";     break;
        case InputEvent::Wheel:         typeStr = "wheel";  break;
        case InputEvent::TrackpadScroll:typeStr = "scroll"; break;
        case InputEvent::FingerDown:    typeStr = "finger-down"; break;
        case InputEvent::FingerMotion:  typeStr = "finger-motion"; break;
        case InputEvent::FingerUp:      typeStr = "finger-up"; break;
        case InputEvent::FingerCanceled:typeStr = "finger-canceled"; break;
        }
        const char* srcStr = "unknown";
        switch (ev.source) {
        case InputEvent::Source::Unknown:        srcStr = "unknown"; break;
        case InputEvent::Source::PhysicalWheel:  srcStr = "wheel"; break;
        case InputEvent::Source::Trackpad:       srcStr = "trackpad"; break;
        case InputEvent::Source::Finger:         srcStr = "finger"; break;
        case InputEvent::Source::PinchGesture:   srcStr = "pinch"; break;
        }
        std::fprintf(f, "%s,%d,%.1f,%.1f,%.3f,%.3f,%llu,%s\n",
                     typeStr, ev.button,
                     static_cast<double>(ev.position.x),
                     static_cast<double>(ev.position.y),
                     static_cast<double>(ev.delta.x),
                     static_cast<double>(ev.delta.y),
                     static_cast<unsigned long long>(ev.timestampUs),
                     srcStr);
    }
    std::fclose(f);
    return true;
}

InputLogger::AggregateMetrics InputLogger::ComputeMetrics() const {
    AggregateMetrics m;
    if (m_events.empty()) return m;

    uint64_t firstUs = m_events.front().timestampUs;
    uint64_t lastUs = m_events.back().timestampUs;
    m.totalTimeS = static_cast<double>(lastUs - firstUs) / 1'000'000.0;

    double sumDx = 0, sumDy = 0;
    int moveCount = 0, scrollCount = 0;

    for (const auto& ev : m_events) {
        if (ev.type == InputEvent::PointerMove) {
            ++moveCount;
            sumDx += std::abs(static_cast<double>(ev.delta.x));
            sumDy += std::abs(static_cast<double>(ev.delta.y));
        }
        if (ev.type == InputEvent::Wheel || ev.type == InputEvent::TrackpadScroll) {
            ++scrollCount;
            sumDx += std::abs(static_cast<double>(ev.delta.x));
            sumDy += std::abs(static_cast<double>(ev.delta.y));
        }
    }

    m.moveEventCount = moveCount;
    m.scrollEventCount = scrollCount;
    if (m.totalTimeS > 0.0) {
        m.moveFrequencyHz = static_cast<double>(moveCount) / m.totalTimeS;
    }
    int totalEvents = moveCount + scrollCount;
    if (totalEvents > 0) {
        m.avgDeltaX = sumDx / static_cast<double>(totalEvents);
        m.avgDeltaY = sumDy / static_cast<double>(totalEvents);
    }

    return m;
}

InputLogger::FidelitySummary InputLogger::ComputeFidelitySummary(
        bool hasNonzeroViewportResponse,
        bool hasAppliedPanOutcome,
        bool hasAppliedPinchOutcome) const {
    FidelitySummary fs;
    fs.totalEvents = static_cast<int>(m_events.size());
    fs.hasNonzeroViewportResponse = hasNonzeroViewportResponse;
    fs.hasAppliedPanOutcome = hasAppliedPanOutcome;
    fs.hasAppliedPinchOutcome = hasAppliedPinchOutcome;

    if (m_events.empty()) {
        fs.verdict = "TEMPLATE — no events captured (pending physical session)";
        return fs;
    }

    // Check monotonic timestamps and timestamp presence
    uint64_t prevUs = 0;
    int dropCount = 0;
    for (const auto& ev : m_events) {
        if (ev.timestampUs == 0) {
            fs.allEventsHaveTimestamps = false;
            dropCount++;
        }
        if (prevUs > 0 && ev.timestampUs < prevUs) fs.monotonicTimestamps = false;
        if (prevUs > 0 && ev.timestampUs == prevUs) dropCount++; // duplicate timestamp
        prevUs = ev.timestampUs;

        if (ev.type == InputEvent::Wheel || ev.type == InputEvent::TrackpadScroll) {
            fs.scrollEventCount++;
        }
        if (ev.type == InputEvent::FingerDown ||
            ev.type == InputEvent::FingerMotion ||
            ev.type == InputEvent::FingerUp ||
            ev.type == InputEvent::FingerCanceled) {
            fs.fingerEventCount++;
        }
        if (ev.source == InputEvent::Source::PinchGesture ||
            ev.source == InputEvent::Source::Finger) {
            fs.hasGestureEvent = true;
        }
    }
    fs.droppedOrInvalid = dropCount;

    uint64_t firstUs = m_events.front().timestampUs;
    uint64_t lastUs = m_events.back().timestampUs;
    fs.captureDurationS = static_cast<double>(lastUs - firstUs) / 1'000'000.0;

    // Peak event rate (in a 100ms sliding window)
    {
        constexpr double windowS = 0.1;
        uint64_t windowUs = static_cast<uint64_t>(windowS * 1'000'000.0);
        for (size_t i = 0; i < m_events.size(); ++i) {
            uint64_t t0 = m_events[i].timestampUs;
            int count = 0;
            for (size_t j = i; j < m_events.size(); ++j) {
                if (m_events[j].timestampUs - t0 <= windowUs) count++;
                else break;
            }
            double rate = static_cast<double>(count) / windowS;
            if (rate > fs.peakEventRateHz) fs.peakEventRateHz = rate;
        }
    }

    // Subpixel delta analysis
    for (const auto& ev : m_events) {
        if (ev.type == InputEvent::Wheel || ev.type == InputEvent::TrackpadScroll) {
            double adx = std::abs(static_cast<double>(ev.delta.x));
            double ady = std::abs(static_cast<double>(ev.delta.y));
            if (adx > 0.0 && adx < 1.0) fs.hasSubpixelDeltas = true;
            if (ady > 0.0 && ady < 1.0) fs.hasSubpixelDeltas = true;
            if (fs.totalEvents <= 1 || adx < fs.minDeltaX) fs.minDeltaX = adx;
            if (adx > fs.maxDeltaX) fs.maxDeltaX = adx;
            if (fs.totalEvents <= 1 || ady < fs.minDeltaY) fs.minDeltaY = ady;
            if (ady > fs.maxDeltaY) fs.maxDeltaY = ady;
        }
    }

    // Every metric is derived from the records emitted by the same SDL event
    // handler used during physical capture. Finger telemetry records each
    // supported SDL_EVENT_FINGER_* variant separately; neither it nor native
    // pinch records inflate scrollEventCount. SDL/macOS can deliver correct
    // two-finger pan/pinch through scroll and pinch streams with no low-level
    // finger records, so finger telemetry is not a fidelity pass threshold.
    std::string issues;
    if (fs.totalEvents < FidelitySummary::kMinTotalEventCount) {
        issues += "total events below minimum (" + std::to_string(fs.totalEvents) +
                  " < " + std::to_string(FidelitySummary::kMinTotalEventCount) + "); ";
    }
    if (fs.scrollEventCount < FidelitySummary::kMinScrollEventCount) {
        issues += "scroll events below minimum (" + std::to_string(fs.scrollEventCount) +
                  " < " + std::to_string(FidelitySummary::kMinScrollEventCount) + "); ";
    }
    if (fs.captureDurationS < FidelitySummary::kMinDurationS)
        issues += "capture too short; ";
    if (fs.peakEventRateHz < FidelitySummary::kMinEventRateHz && fs.captureDurationS >= 0.5)
        issues += "event rate too low; ";
    if (!fs.monotonicTimestamps) issues += "non-monotonic timestamps; ";
    if (!fs.allEventsHaveTimestamps) issues += "missing timestamps; ";
    if (fs.droppedOrInvalid > 0) issues += "dropped/invalid events; ";
    if (!fs.hasSubpixelDeltas)
        issues += "no subpixel deltas detected; ";
    if (!fs.hasGestureEvent)
        issues += "no Finger or PinchGesture events; ";
    if (!fs.hasNonzeroViewportResponse)
        issues += "no viewport response; ";
    if (!fs.hasAppliedPanOutcome)
        issues += "no pan outcome applied; ";
    if (!fs.hasAppliedPinchOutcome)
        issues += "no pinch outcome applied; ";

    if (!issues.empty()) {
        fs.verdict = "FAIL — " + issues;
    } else {
        fs.verdict = "PASS — all fidelity thresholds met";
    }

    return fs;
}

} // namespace spike
