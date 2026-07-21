// SPDX-License-Identifier: Apache-2.0

#include "app.hpp"
#include <cstdio>
#include <cstring>
#include <string>

static void PrintUsage(const char* prog) {
    std::fprintf(stdout,
        "GraphScore M0 Phase C — Rendering & Accessibility Spike\n"
        "\n"
        "Usage: %s [mode] [options]\n"
        "\n"
        "Modes:\n"
        "  (no mode)        Interactive window with graph nodes, staves, hit testing\n"
        "  --self-test      Run deterministic self-tests (no window needed)\n"
        "  --smoke          Create a real window and exit after 2 seconds\n"
        "  --offscreen      Render offscreen and output PPM\n"
        "  --input-log      Capture mouse/keyboard/trackpad events to CSV log\n"
        "  --native-handle  Probe and report native window handle\n"
        "  --accessibility-report  Generate accessibility tree + bridge report\n"
        "\n"
        "Options:\n"
        "  --quit-after-ms N    Milliseconds before smoke exit (default: 2000)\n"
        "  --offscreen-output P  PPM output path (default: /tmp/graphscore_spike_offscreen.ppm)\n"
        "  --input-log-path P   CSV log output path\n"
        "  --input-device D     Declared input device for capture metadata:\n"
        "                       trackpad | mouse | unknown (default: unknown)\n"
        "  --native-handle-report P  Report output path\n"
        "  --accessibility-report P  Accessibility report output path\n"
        "  --bravura-font P     Path to Bravura.otf (default: bravura_font/Bravura.otf)\n"
        "  --text-font P        Path to a TrueType text font\n"
        "  --help               Show this help\n"
        "\n",
        prog);
}

int main(int argc, char** argv) {
    spike::AppConfig config;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--self-test") == 0) {
            config.mode = spike::RunMode::SelfTest;
        } else if (std::strcmp(argv[i], "--smoke") == 0) {
            config.mode = spike::RunMode::Smoke;
        } else if (std::strcmp(argv[i], "--offscreen") == 0) {
            config.mode = spike::RunMode::OffscreenRender;
        } else if (std::strcmp(argv[i], "--input-log") == 0) {
            config.mode = spike::RunMode::InputLog;
        } else if (std::strcmp(argv[i], "--native-handle") == 0) {
            config.mode = spike::RunMode::NativeHandleReport;
        } else if (std::strcmp(argv[i], "--accessibility-report") == 0 && i + 1 < argc) {
            config.accessibilityReportPath = argv[++i];
            config.mode = spike::RunMode::AccessibilityReport;
        } else if (std::strcmp(argv[i], "--accessibility-report") == 0) {
            config.mode = spike::RunMode::AccessibilityReport;
        } else if (std::strcmp(argv[i], "--quit-after-ms") == 0 && i + 1 < argc) {
            config.smokeQuitMs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--offscreen-output") == 0 && i + 1 < argc) {
            config.offscreenOutput = argv[++i];
        } else if (std::strcmp(argv[i], "--input-log-path") == 0 && i + 1 < argc) {
            config.inputLogPath = argv[++i];
        } else if (std::strcmp(argv[i], "--input-device") == 0 && i + 1 < argc) {
            config.inputDevice = argv[++i];
        } else if (std::strcmp(argv[i], "--native-handle-report") == 0 && i + 1 < argc) {
            config.nativeHandleReportPath = argv[++i];
        } else if (std::strcmp(argv[i], "--bravura-font") == 0 && i + 1 < argc) {
            config.bravuraFontPath = argv[++i];
        } else if (std::strcmp(argv[i], "--text-font") == 0 && i + 1 < argc) {
            config.textFontPath = argv[++i];
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // Default font paths
    if (!config.bravuraFontPath) {
        config.bravuraFontPath = "bravura_font/Bravura.otf";
    }
    if (!config.textFontPath) {
        config.textFontPath = "noto_font/NotoSans-Regular.ttf";
    }

    spike::App app(config);
    return app.Run();
}
