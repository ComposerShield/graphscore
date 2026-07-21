// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include "render_list.hpp"
#include "rasterizer.hpp"
#include "font_manager.hpp"
#include "notation_renderer.hpp"
#include "graph_renderer.hpp"
#include "hit_test.hpp"
#include "input_logger.hpp"
#include "self_test.hpp"
#include "semantic_tree.hpp"
#include "accessibility_bridge.hpp"
#include "viewport_event_handler.hpp"

#include <cstdint>

struct SDL_Window;

namespace spike {

enum class RunMode {
    Interactive,
    SelfTest,
    Smoke,
    OffscreenRender,
    InputLog,
    NativeHandleReport,
    AccessibilityReport,
};

struct AppConfig {
    RunMode mode = RunMode::Interactive;
    int smokeQuitMs = 1000;
    const char* offscreenOutput = nullptr;
    const char* inputLogPath = nullptr;
    const char* nativeHandleReportPath = nullptr;
    const char* accessibilityReportPath = nullptr;
    const char* bravuraFontPath = nullptr;
    const char* textFontPath = nullptr;
    const char* inputDevice = nullptr; // --input-device: trackpad, mouse, unknown
    bool captureInputSession = false;
};

class App {
public:
    App(const AppConfig& config);
    ~App();

    // Run the application; returns 0 on success
    int Run();

    // Non-interactive modes
    int RunSelfTest();
    int RunSmoke();
    int RunOffscreenRender();
    int RunInputLog();
    int RunNativeHandleReport();
    int RunAccessibilityReport();

    // Interactive mode (requires window)
    int RunInteractive();

    // Centralized viewport mutation — updates render state and immediately
    // calls bridge SyncGeometry.  Public so the event-handler trampoline
    // and SelfTest can invoke it.
    void ApplyViewportTransform(const Transform& newXf);

private:
    AppConfig m_config;

    // Ownership
    FontManager m_fontManager;
    NotationRenderer m_notationRenderer;
    GraphRenderer m_graphRenderer;
    HitTester m_hitTester;
    InputLogger m_inputLogger;

    // Accessibility (interactive mode only)
    SemanticTree m_semanticTree;
    std::unique_ptr<IAccessibilityBridge> m_accessibilityBridge;

    // SDL3 window (only for Smoke and Interactive modes)
    SDL_Window* m_window = nullptr;

    // Viewport state
    Transform m_viewTransform;
    Vec2 m_lastMousePos;
    bool m_mouseDown = false;

    // Two-finger touch/trackpad state
    FingerState m_finger1;
    FingerState m_finger2;

    // Accumulated outcome tracking (pan/pinch applied during the session)
    ViewportOutcomeAccumulator m_outcomes;

    // Render list and target
    RenderList m_renderList;
    Rasterizer m_rasterizer;

    // Build the complete scene into the render list
    void BuildScene(int width, int height);

    // Handle SDL events (SDL_Event pointer for non-inclusion in header)
    void ProcessSDLEvent(const void* sdlEvent);

    // Blit rasterizer buffer to SDL window surface.
    // Returns false on any SDL surface/blit/update failure.
    bool PresentToWindow();
};

} // namespace spike
