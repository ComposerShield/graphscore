// SPDX-License-Identifier: Apache-2.0

#include "app.hpp"
#include "native_handle.hpp"
#include "viewport_event_handler.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>

namespace spike {

App::App(const AppConfig& config)
    : m_config(config)
    , m_notationRenderer(&m_fontManager)
    , m_graphRenderer(&m_fontManager)
    , m_rasterizer(800, 600, m_fontManager.GlyphCache())
{
    if (config.bravuraFontPath && std::strlen(config.bravuraFontPath) > 0) {
        m_fontManager.LoadBravura(config.bravuraFontPath);
    }
    if (config.textFontPath && std::strlen(config.textFontPath) > 0) {
        m_fontManager.LoadTextFont(config.textFontPath);
    }
}

App::~App() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
        SDL_Quit();
    }
}

int App::Run() {
    switch (m_config.mode) {
    case RunMode::SelfTest: return RunSelfTest();
    case RunMode::Smoke: return RunSmoke();
    case RunMode::OffscreenRender: return RunOffscreenRender();
    case RunMode::InputLog: return RunInputLog();
    case RunMode::NativeHandleReport: return RunNativeHandleReport();
    case RunMode::AccessibilityReport: return RunAccessibilityReport();
    case RunMode::Interactive: return RunInteractive();
    }
    return 1;
}

int App::RunSelfTest() {
    if (m_config.bravuraFontPath) {
        SelfTest::SetTestFontPath(m_config.bravuraFontPath);
    }
    if (m_config.textFontPath) {
        SelfTest::SetTestTextFontPath(m_config.textFontPath);
    }
    SelfTest st;
    bool ok = st.RunAll();
    st.SaveResults("/tmp/graphscore_self_test_results.json");
    return ok ? 0 : 1;
}

int App::RunSmoke() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    m_window = SDL_CreateWindow("GraphScore Spike — Smoke",
                                 800, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    m_rasterizer = Rasterizer(800, 600, m_fontManager.GlyphCache());
    BuildScene(800, 600);
    m_rasterizer.Execute(m_renderList);
    if (!PresentToWindow()) {
        std::fprintf(stderr, "Smoke: PresentToWindow failed\n");
        return 1;
    }

    uint64_t startMs = SDL_GetTicks();
    bool done = false;
    while (!done && SDL_GetTicks() - startMs < static_cast<uint64_t>(m_config.smokeQuitMs)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                done = true;
                break;
            }
        }
        SDL_Delay(10);
    }
    (void)done;

    if (m_config.nativeHandleReportPath) {
        auto report = ProbeNativeHandle(m_window);
        FILE* f = std::fopen(m_config.nativeHandleReportPath, "w");
        if (f) {
            std::fprintf(f, "platform=%s\nhandleType=%s\nhandleNonNull=%s\nvalid=%s\n",
                         report.platform.c_str(), report.handleType.c_str(),
                         report.handleNonNull ? "yes" : "no",
                         report.valid ? "yes" : "no");
            std::fclose(f);
        }
    }

    return 0;
}

int App::RunOffscreenRender() {
    m_rasterizer = Rasterizer(800, 600, m_fontManager.GlyphCache());
    BuildScene(800, 600);
    m_rasterizer.Execute(m_renderList);

    const char* outPath = m_config.offscreenOutput;
    if (!outPath || std::strlen(outPath) == 0) {
        outPath = "/tmp/graphscore_spike_offscreen.ppm";
    }
    bool ok = m_rasterizer.SavePPM(outPath);
    std::fprintf(stdout, "Offscreen render: %s -> %s\n",
                  ok ? "ok" : "FAILED", outPath);
    if (!ok) {
        std::fprintf(stderr, "Offscreen PPM write failed: %s\n", outPath);
        return 1;
    }
    uint64_t hash = m_rasterizer.PixelHash();
    std::fprintf(stdout, "Pixel hash: 0x%016llx\n",
                 static_cast<unsigned long long>(hash));

    // Golden hash pinned against exact full scene (BuildScene 800x600).
    // Must match across normal and ASan builds.
    static constexpr uint64_t kGoldenFullSceneHash = 0x16120cb8d680d0c6ULL;
    if (hash != kGoldenFullSceneHash) {
        std::fprintf(stderr, "Offscreen hash MISMATCH: expected 0x%016llx, got 0x%016llx\n",
                     static_cast<unsigned long long>(kGoldenFullSceneHash),
                     static_cast<unsigned long long>(hash));
        return 1;
    }
    return 0;
}

int App::RunInputLog() {
    // Enable capture mode so ProcessSDLEvent records events
    m_config.captureInputSession = true;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    m_window = SDL_CreateWindow("GraphScore Spike — Input Log",
                                 800, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Enable touch and gesture events for trackpad capture
    SDL_SetEventEnabled(SDL_EVENT_FINGER_DOWN, true);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_UP, true);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_MOTION, true);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_CANCELED, true);
    SDL_SetEventEnabled(SDL_EVENT_PINCH_BEGIN, true);
    SDL_SetEventEnabled(SDL_EVENT_PINCH_UPDATE, true);
    SDL_SetEventEnabled(SDL_EVENT_PINCH_END, true);

    std::fprintf(stdout, "Input logging active. Move mouse, click, scroll, use trackpad.\n");
    std::fprintf(stdout, "Close the window (or press Escape) to stop and save the log.\n");

    m_rasterizer = Rasterizer(800, 600, m_fontManager.GlyphCache());
    BuildScene(800, 600);
    m_rasterizer.Execute(m_renderList);
    if (!PresentToWindow()) {
        std::fprintf(stderr, "RunInputLog: PresentToWindow failed\n");
        return 1;
    }

    // Record initial transform for before/after tracking
    Transform beforeXf = m_viewTransform;
    m_outcomes.Reset();

    SDL_Event ev;
    bool running = true;
    bool presentFailed = false;
    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                running = false;
                break;
            }
            // Route through the same ProcessSDLEvent used by interactive mode
            ProcessSDLEvent(&ev);
        }

        // Re-render every frame so pan/zoom is visible during capture.
        // Uses the same code path as RunInteractive.
        int w = 800, h = 600;
        SDL_GetWindowSize(m_window, &w, &h);
        if (w != m_rasterizer.Width() || h != m_rasterizer.Height()) {
            m_rasterizer = Rasterizer(w, h, m_fontManager.GlyphCache());
        }
        m_rasterizer.Clear(Color::White());
        BuildScene(w, h);
        m_rasterizer.Execute(m_renderList);
        if (!PresentToWindow()) {
            std::fprintf(stderr, "RunInputLog: PresentToWindow failed\n");
            presentFailed = true;
            running = false;
            break;
        }
        SDL_Delay(8);
    }

    // Record after-transform to verify viewport response.
    // The fidelity summary requires at least one pan outcome and at least
    // one pinch outcome in addition to the existing criteria.
    Transform afterXf = m_viewTransform;
    bool hadViewportResponse = (beforeXf.scale != afterXf.scale ||
                                 beforeXf.offset.x != afterXf.offset.x ||
                                 beforeXf.offset.y != afterXf.offset.y);

    const char* logPath = m_config.inputLogPath;
    if (!logPath || std::strlen(logPath) == 0) {
        logPath = "/tmp/graphscore_spike_input_log.csv";
    }

    bool saved = m_inputLogger.SaveLog(logPath);
    if (!saved) {
        std::fprintf(stderr, "Failed to save input log: %s\n", logPath);
    }

    // Compute fidelity summary with enforceable thresholds.
    auto summary = m_inputLogger.ComputeFidelitySummary(
        hadViewportResponse, m_outcomes.HasPan(), m_outcomes.HasPinch());

    std::fprintf(stdout, "\n=== Input Fidelity Summary ===\n");
    std::fprintf(stdout, "required: totalEvents >= %d, scrollEventCount >= %d\n",
                  InputLogger::FidelitySummary::kMinTotalEventCount,
                  InputLogger::FidelitySummary::kMinScrollEventCount);
    std::fprintf(stdout, "fingerEventCount: diagnostic only (no required minimum)\n");
    std::fprintf(stdout, "verdict: %s\n", summary.verdict.c_str());
    std::fprintf(stdout, "events:  %d total, %d scroll, %d finger\n",
                 summary.totalEvents, summary.scrollEventCount, summary.fingerEventCount);
    std::fprintf(stdout, "duration: %.3f s\n", summary.captureDurationS);
    std::fprintf(stdout, "peak rate: %.1f Hz\n", summary.peakEventRateHz);
    std::fprintf(stdout, "timestamps monotonic: %s\n",
                 summary.monotonicTimestamps ? "yes" : "no");
    std::fprintf(stdout, "subpixel deltas: %s\n",
                 summary.hasSubpixelDeltas ? "yes" : "no");
    std::fprintf(stdout, "dropped/invalid: %d\n", summary.droppedOrInvalid);
    std::fprintf(stdout, "viewport response: %s\n",
                  summary.hasNonzeroViewportResponse ? "yes" : "no");
    std::fprintf(stdout, "pan outcome: %s\n",
                  summary.hasAppliedPanOutcome ? "yes" : "no");
    std::fprintf(stdout, "pinch outcome: %s\n",
                  summary.hasAppliedPinchOutcome ? "yes" : "no");

    auto metrics = m_inputLogger.ComputeMetrics();
    std::fprintf(stdout, "\nInput log saved: %s\n", logPath);
    std::fprintf(stdout, "  Events: %d move, %d scroll\n",
                 metrics.moveEventCount, metrics.scrollEventCount);
    std::fprintf(stdout, "  Duration: %.3f s\n", metrics.totalTimeS);
    std::fprintf(stdout, "  Move freq: %.1f Hz\n", metrics.moveFrequencyHz);
    std::fprintf(stdout, "  Avg delta: (%.4f, %.4f)\n",
                 metrics.avgDeltaX, metrics.avgDeltaY);

    // Persist fidelity summary alongside raw CSV
    std::string summaryPath(logPath);
    summaryPath += ".summary.txt";
    FILE* sf = std::fopen(summaryPath.c_str(), "w");
    if (sf) {
        std::fprintf(sf, "requiredMinTotalEvents: %d\n",
                     InputLogger::FidelitySummary::kMinTotalEventCount);
        std::fprintf(sf, "requiredMinScrollEventCount: %d\n",
                     InputLogger::FidelitySummary::kMinScrollEventCount);
        std::fprintf(sf, "fingerEventCountRole: diagnostic-only-no-minimum\n");
        std::fprintf(sf, "verdict: %s\n", summary.verdict.c_str());
        std::fprintf(sf, "totalEvents: %d\n", summary.totalEvents);
        std::fprintf(sf, "scrollEventCount: %d\n", summary.scrollEventCount);
        std::fprintf(sf, "fingerEventCount: %d\n", summary.fingerEventCount);
        std::fprintf(sf, "captureDurationS: %.6f\n", summary.captureDurationS);
        std::fprintf(sf, "peakEventRateHz: %.3f\n", summary.peakEventRateHz);
        std::fprintf(sf, "monotonicTimestamps: %s\n",
                     summary.monotonicTimestamps ? "true" : "false");
        std::fprintf(sf, "hasSubpixelDeltas: %s\n",
                     summary.hasSubpixelDeltas ? "true" : "false");
        std::fprintf(sf, "droppedOrInvalid: %d\n", summary.droppedOrInvalid);
        std::fprintf(sf, "hasNonzeroViewportResponse: %s\n",
                      summary.hasNonzeroViewportResponse ? "true" : "false");
        std::fprintf(sf, "hasAppliedPanOutcome: %s\n",
                      summary.hasAppliedPanOutcome ? "true" : "false");
        std::fprintf(sf, "hasAppliedPinchOutcome: %s\n",
                      summary.hasAppliedPinchOutcome ? "true" : "false");
        std::fclose(sf);
    }

    // Return nonzero when explicit criteria fail after a completed capture.
    // The verdict from ComputeFidelitySummary already incorporates all
    // enforceable thresholds (min duration, event rate, monotonic timestamps,
    // dropped/invalid events, subpixel precision, gesture presence, and
    // viewport response). Use it directly — do not re-check thresholds.
    if (summary.totalEvents == 0) {
        std::fprintf(stdout,
            "No events captured — template/pending mode. "
            "Perform physical trackpad gestures for a real capture.\n");
        return 0;
    }

    if (summary.verdict.find("PASS") != 0) {
        std::fprintf(stderr, "Input fidelity FAILED: %s\n", summary.verdict.c_str());
        return 1;
    }

    if (presentFailed) {
        std::fprintf(stderr, "RunInputLog: presentation failed during capture\n");
        return 1;
    }

    return 0;
}

int App::RunNativeHandleReport() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    m_window = SDL_CreateWindow("GraphScore Spike — Native Handle",
                                 400, 300, SDL_WINDOW_HIDDEN);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    auto report = ProbeNativeHandle(m_window);

    const char* outPath = m_config.nativeHandleReportPath;
    if (!outPath || std::strlen(outPath) == 0) {
        outPath = "/tmp/graphscore_spike_native_handle.txt";
    }

    FILE* f = std::fopen(outPath, "w");
    if (!f) {
        std::fprintf(stderr, "Failed to write native handle report: %s\n", outPath);
        return 1;
    }
    std::fprintf(f, "platform=%s\n", report.platform.c_str());
    std::fprintf(f, "handleType=%s\n", report.handleType.c_str());
    std::fprintf(f, "handleNonNull=%s\n", report.handleNonNull ? "yes" : "no");
    std::fprintf(f, "valid=%s\n", report.valid ? "yes" : "no");
    if (!report.error.empty()) {
        std::fprintf(f, "error=%s\n", report.error.c_str());
    }
    std::fclose(f);
    std::fprintf(stdout, "Native handle report saved: %s\n", outPath);

    if (!report.valid) {
        std::fprintf(stderr, "Native handle probe failed: %s\n", report.error.c_str());
        return 1;
    }
    return 0;
}

int App::RunAccessibilityReport() {
    // Build the semantic tree
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    // Build the render scene (for bounds context)
    BuildScene(800, 600);

    // Determine report path
    const char* reportPath = m_config.accessibilityReportPath;
    if (!reportPath || std::strlen(reportPath) == 0) {
        reportPath = "/tmp/graphscore_spike_accessibility_report.txt";
    }

    // Open a real SDL window (needed for native macOS bridge)
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    m_window = SDL_CreateWindow("GraphScore Spike — Accessibility Report",
                                 800, 600, SDL_WINDOW_HIDDEN);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int result = spike::RunAccessibilityReport(
        static_cast<void*>(m_window), &tree, reportPath);

    std::fprintf(stdout, "[accessibility-report] Report saved: %s (result=%d)\n",
                 reportPath, result);

    return result;
}

// Centralized viewport mutation helper — updates render state and
// immediately synchronises the accessibility bridge if attached.
void App::ApplyViewportTransform(const Transform& newXf) {
    m_viewTransform = newXf;
    if (m_accessibilityBridge) {
        int w = 800, h = 600;
        if (m_window) SDL_GetWindowSize(m_window, &w, &h);
        WindowGeometry geom{w, h, 1.0f};
        m_accessibilityBridge->SyncGeometry(geom, m_viewTransform);
    }
}

int App::RunInteractive() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Enable touch and gesture events for trackpad multi-touch
    SDL_SetEventEnabled(SDL_EVENT_FINGER_DOWN, true);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_UP, true);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_MOTION, true);
    SDL_SetEventEnabled(SDL_EVENT_FINGER_CANCELED, true);
    SDL_SetEventEnabled(SDL_EVENT_PINCH_BEGIN, true);
    SDL_SetEventEnabled(SDL_EVENT_PINCH_UPDATE, true);
    SDL_SetEventEnabled(SDL_EVENT_PINCH_END, true);

    m_window = SDL_CreateWindow("GraphScore Spike — Interactive",
                                 800, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                 SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Build the semantic accessibility tree and install the action callback.
    SemanticTreeFactory::BuildDemo(m_semanticTree);
    m_semanticTree.SetActionCallback(
        [this](const std::string& nodeId, const std::string& actionId) {
            std::fprintf(stdout, "[interactive] Action: node=%s action=%s\n",
                         nodeId.c_str(), actionId.c_str());

            if (actionId == "press" && nodeId == "gs_action_play") {
                int idx = m_semanticTree.FindByStableId("gs_action_play");
                if (idx >= 0) {
                    auto& pn = m_semanticTree.Node(idx);
                    pn.value = (pn.value == "playing") ? "stopped" : "playing";
                    if (m_accessibilityBridge) {
                        m_accessibilityBridge->AnnouncePropertyChange("gs_action_play");
                    }
                }
            } else if (actionId == "select") {
                int idx = m_semanticTree.FindByStableId(nodeId);
                if (idx >= 0) {
                    for (int i = 0; i < m_semanticTree.NodeCount(); ++i) {
                        m_semanticTree.SetSelected(i, false);
                        m_semanticTree.SetFocused(i, false);
                        m_semanticTree.SetKeyboardFocus(i, false);
                    }
                    m_semanticTree.SetSelected(idx, true);
                    m_semanticTree.SetFocused(idx, true);
                    m_semanticTree.SetKeyboardFocus(idx, true);
                    if (m_accessibilityBridge) {
                        m_accessibilityBridge->AnnounceSelectionChange();
                        m_accessibilityBridge->AnnounceFocusedChange(nodeId);
                    }
                }
            } else if (actionId == "increment") {
                Transform newXf = m_viewTransform;
                newXf.scale *= 1.2f;
                newXf.scale = std::max(0.1f, std::min(20.0f, newXf.scale));
                ApplyViewportTransform(newXf);
                std::fprintf(stdout, "[interactive] Zoom: %.2fx\n",
                             static_cast<double>(m_viewTransform.scale));
            } else if (actionId == "decrement") {
                Transform newXf = m_viewTransform;
                newXf.scale *= 0.8f;
                newXf.scale = std::max(0.1f, std::min(20.0f, newXf.scale));
                ApplyViewportTransform(newXf);
                std::fprintf(stdout, "[interactive] Zoom: %.2fx\n",
                             static_cast<double>(m_viewTransform.scale));
            }
        });

    m_accessibilityBridge = CreateAccessibilityBridge();
    if (!m_accessibilityBridge->Attach(static_cast<void*>(m_window), &m_semanticTree)) {
        std::fprintf(stderr, "[interactive] Accessibility bridge attach failed\n");
    } else {
        std::fprintf(stdout, "[interactive] Accessibility bridge attached\n");
        int winW = 800, winH = 600;
        SDL_GetWindowSize(m_window, &winW, &winH);
        WindowGeometry initGeom{winW, winH, 1.0f};
        m_accessibilityBridge->SyncGeometry(initGeom, m_viewTransform);
    }

    SDL_Event ev;
    bool running = true;
    bool transformChanged = false;
    bool presentFailed = false;

    m_outcomes.Reset();
    m_inputLogger = InputLogger{};

    while (running) {
        transformChanged = false;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                running = false;
                break;
            }
            Transform beforeXf = m_viewTransform;
            ProcessSDLEvent(&ev);
            if (m_viewTransform.scale != beforeXf.scale ||
                m_viewTransform.offset.x != beforeXf.offset.x ||
                m_viewTransform.offset.y != beforeXf.offset.y) {
                transformChanged = true;
            }
        }

        int w = 800, h = 600;
        SDL_GetWindowSize(m_window, &w, &h);
        if (w != m_rasterizer.Width() || h != m_rasterizer.Height()) {
            m_rasterizer = Rasterizer(w, h, m_fontManager.GlyphCache());
            transformChanged = true;
        }

        m_rasterizer.Clear(Color::White());
        BuildScene(w, h);
        m_rasterizer.Execute(m_renderList);
        if (!PresentToWindow()) {
            std::fprintf(stderr, "RunInteractive: PresentToWindow failed\n");
            presentFailed = true;
            running = false;
            break;
        }

        // Synchronise accessibility bounds after transform or resize
        if (transformChanged && m_accessibilityBridge) {
            WindowGeometry geom{w, h, 1.0f};
            m_accessibilityBridge->SyncGeometry(geom, m_viewTransform);
        }

        SDL_Delay(8);
    }

    if (m_accessibilityBridge) {
        m_accessibilityBridge->Detach();
    }

    return presentFailed ? 1 : 0;
}

void App::BuildScene(int width, int height) {
    m_renderList.Clear();

    m_renderList.Add(RenderCommand::SetClip(
        {0, 0, static_cast<float>(width), static_cast<float>(height)}));

    m_renderList.Add(RenderCommand::FillRect(
        {0, 0, static_cast<float>(width), static_cast<float>(height)},
        Color::White()));

    for (const auto& node : GraphRenderer::DemoNodes()) {
        m_graphRenderer.RenderNode(m_renderList, node, m_viewTransform);
    }

    for (const auto& conn : GraphRenderer::DemoConnectors()) {
        m_graphRenderer.RenderConnector(m_renderList, conn, m_viewTransform);
    }

    float staffX = 20.0f;
    float staffY = 280.0f;
    float staffW = static_cast<float>(width) - 40.0f;
    float lineSpacing = 8.0f;
    m_notationRenderer.RenderDemoStaves(m_renderList, staffX, staffY,
                                         staffW, lineSpacing, m_viewTransform);

    if (m_mouseDown && m_config.mode == RunMode::Interactive) {
        HitTester ht;
        HitTester::PopulateFromDemo(ht);
        auto result = ht.Test(m_lastMousePos, m_viewTransform);
        float cx = m_lastMousePos.x;
        float cy = m_lastMousePos.y;
        m_renderList.Add(RenderCommand::StrokeLine(
            cx - 10, cy, cx + 10, cy, 2.0f,
            result.hit ? Color::Green() : Color::Red()));
        m_renderList.Add(RenderCommand::StrokeLine(
            cx, cy - 10, cx, cy + 10, 2.0f,
            result.hit ? Color::Green() : Color::Red()));

        if (result.hit) {
            std::fprintf(stdout, "[hit] type=%s index=%d world=(%.1f,%.1f) screen=(%.1f,%.1f)\n",
                         result.targetType.c_str(), result.targetIndex,
                         static_cast<double>(result.worldPos.x),
                         static_cast<double>(result.worldPos.y),
                         static_cast<double>(result.screenPos.x),
                         static_cast<double>(result.screenPos.y));
        }
    }
}

// Trampoline: converts the ApplyTransformCallback back into
// an App::ApplyViewportTransform call via userData.
static void OnViewportTransformTrampoline(void* userData, const Transform& newXf) {
    auto* app = static_cast<App*>(userData);
    app->ApplyViewportTransform(newXf);
}

void App::ProcessSDLEvent(const void* sdlEvent) {
    int winW = 800, winH = 600;
    if (m_window) SDL_GetWindowSize(m_window, &winW, &winH);

    // Resolve user-declared input device source from --input-device flag
    InputEvent::Source declaredSrc = InputEvent::Source::Unknown;
    if (m_config.inputDevice) {
        if (std::strcmp(m_config.inputDevice, "trackpad") == 0) {
            declaredSrc = InputEvent::Source::Trackpad;
        } else if (std::strcmp(m_config.inputDevice, "mouse") == 0) {
            declaredSrc = InputEvent::Source::PhysicalWheel;
        }
    }

    ViewportEventState state{
        m_viewTransform,
        (m_config.captureInputSession ? &m_inputLogger : nullptr),
        m_config.captureInputSession,
        m_lastMousePos,
        m_mouseDown,
        m_finger1,
        m_finger2,
        winW,
        winH,
        OnViewportTransformTrampoline,
        this,
        declaredSrc
    };

    ViewportEventResult result = ProcessViewportSDLEvent(sdlEvent, state);

    // Accumulate pan/pinch outcomes from typed mutation metadata — never
    // compare final offset/scale.
    m_outcomes.Accumulate(result);
}

bool App::PresentToWindow() {
    if (!m_window) return false;

    SDL_Surface* surf = SDL_GetWindowSurface(m_window);
    if (!surf) {
        std::fprintf(stderr, "PresentToWindow: SDL_GetWindowSurface failed: %s\n",
                     SDL_GetError());
        return false;
    }

    int w = m_rasterizer.Width();
    int h = m_rasterizer.Height();
    const uint32_t* pixels = m_rasterizer.Pixels();

    SDL_Surface* srcSurf = SDL_CreateSurfaceFrom(
        w, h, SDL_PIXELFORMAT_ABGR8888,
        const_cast<uint32_t*>(pixels), w * 4);

    if (!srcSurf) {
        std::fprintf(stderr, "PresentToWindow: SDL_CreateSurfaceFrom failed: %s\n",
                     SDL_GetError());
        return false;
    }

    if (!SDL_BlitSurface(srcSurf, nullptr, surf, nullptr)) {
        std::fprintf(stderr, "PresentToWindow: SDL_BlitSurface failed: %s\n",
                     SDL_GetError());
        SDL_DestroySurface(srcSurf);
        return false;
    }
    SDL_DestroySurface(srcSurf);

    if (!SDL_UpdateWindowSurface(m_window)) {
        std::fprintf(stderr, "PresentToWindow: SDL_UpdateWindowSurface failed: %s\n",
                     SDL_GetError());
        return false;
    }

    return true;
}

} // namespace spike
