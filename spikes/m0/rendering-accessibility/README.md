# M0 Phase C — Rendering & Accessibility Spike

## Purpose

Disposable quarantined spike that retires rendering, windowing, input, and
accessibility-platform risks before production architecture depends on them.

This spike does NOT join the production CMake DAG and will be deleted or
replaced after decisions are recorded.

## Dependencies

| Dependency | SHA | License | Status |
|------------|-----|---------|--------|
| SDL3 | `08b9c55393be5cb08fbec12ca431470faba3c8c9` | zlib | PROVISIONAL |
| FreeType | `f01dec5e676847267834b881b25f6e8c79581163` | FTL | POLICY-CLEARED |
| HarfBuzz | `af192b7e0f49a9965220ba3f18473e3f8e28b8b9` | Old MIT | PROVISIONAL |
| Bravura | `02e8ed29a29115df35007d1178cebaeee26c20e1` | OFL 1.1 | Spike-only asset |
| Noto Sans | `ffebf8c1ee449e544955a7e813c54f9b73848eac` | OFL 1.1 | Reproducible text font; SHA-256: d78a4640... |

## Renderer Decision

This spike uses an **owned CPU render-list/raster fallback**, not ThorVG.
Reasons:
- ThorVG requires Meson/Ninja toolchain (not yet pinned in ADR).
- The owned CPU raster/surface approach is appropriate for a disposable spike.
- It validates the render-list abstraction before committing to ThorVG or an
  owned GPU path in production.

The rasterizer is intentionally minimal: filled rects, filled circles, thick
lines (stippled dots), and glyph blitting. This is NOT a production-grade
vector renderer.

## Accessibility

**Platform-native accessibility APIs** are selected. AccessKit is EXCLUDED
(ADR 0002 §2, confirmed in ADR 0004 §2).

| Platform | API | Status |
|----------|-----|--------|
| macOS | NSAccessibility (Cocoa) | Implemented and tested |
| Windows | UI Automation (UIA) | Documented mapping; deferred to physical hardware |
| Linux | AT-SPI2 via D-Bus | Documented mapping; deferred to physical hardware |

The macOS bridge (`accessibility_bridge_mac.mm`) attaches to the SDL3 Cocoa
NSWindow contentView, exposes 23 NSAccessibilityElement objects (graph nodes,
connectors, measures, notes, controls, actions, selection), and supports
VoiceOver navigation. All Cocoa types are confined to the `.mm` file.

## Build

### Prerequisites

- CMake 3.20+
- Apple Clang 17+ (C++23), or Clang 19+
- Git (for FetchContent)
- macOS 14+ (tested on arm64)

### Configure and Build

```sh
cd spikes/m0/rendering-accessibility
cmake -B build -G Ninja
cmake --build build
```

#### Offline / air-gapped builds

When network access is unavailable, pre-populate the `_deps/` source
directories manually or point FetchContent at existing clones.

**Option A — override individual sources via cache variables (recommended):**

```sh
cmake -B build -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE=/path/to/freetype \
  -DFETCHCONTENT_SOURCE_DIR_HARFBUZZ=/path/to/harfbuzz \
  -DFETCHCONTENT_SOURCE_DIR_BRAVURA=/path/to/bravura \
  -DNOTO_SANS_SRC=/path/to/NotoSans-Regular.ttf
```

> **Important:** CMake ≥3.24 requires the cache variable name to match the
> upper-cased dependency name exactly as passed to `FetchContent_Declare`.
> The names are `SDL3`, `freetype`, `harfbuzz`, `bravura`.
> Noto Sans uses `NOTO_SANS_SRC` (a `FILEPATH` cache variable), not FetchContent.

**Option B — fully disconnected mode (no network at all):**

```sh
cmake -B build -G Ninja \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE=/path/to/freetype \
  -DFETCHCONTENT_SOURCE_DIR_HARFBUZZ=/path/to/harfbuzz \
  -DFETCHCONTENT_SOURCE_DIR_BRAVURA=/path/to/bravura \
  -DNOTO_SANS_SRC=/path/to/NotoSans-Regular.ttf
```

When `FETCHCONTENT_FULLY_DISCONNECTED=ON`, CMake skips all network
resolution and requires every `FetchContent_Declare`d dependency to
have a corresponding `FETCHCONTENT_SOURCE_DIR_<NAME>` cache variable.

**Option C — use an existing populated `_deps/` cache:**

If a previous build populated the `_deps/` tree, point extracted source
directories at the existing clones:

```sh
cmake -B build -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_SDL3=$(pwd)/prev-build/_deps/sdl3-src \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE=$(pwd)/prev-build/_deps/freetype-src \
  -DFETCHCONTENT_SOURCE_DIR_HARFBUZZ=$(pwd)/prev-build/_deps/harfbuzz-src \
  -DFETCHCONTENT_SOURCE_DIR_BRAVURA=$(pwd)/prev-build/_deps/bravura-src \
  -DNOTO_SANS_SRC=$(pwd)/prev-build/_downloads/NotoSans-Regular.ttf
```
All three approaches were verified on macOS arm64 with CMake 4.4.0.

### HarfBuzz-FreeType Isolation

HarfBuzz is configured **before** FreeType to prevent the upstream
`if(TARGET freetype)` guard from silently enabling `HB_HAVE_FREETYPE`.
After configuration, `get_target_property` verifies `harfbuzz` has no
`freetype` entry in `LINK_LIBRARIES`.

This proof is checked at CMake configure time — build fails if violated.

### Run Tests

```sh
cd build && ctest --output-on-failure
```

### ASan+UBSan Build

```sh
cmake -B build -G Ninja -DSPIKE_ENABLE_ASAN=ON
cmake --build build
cd build && ASAN_OPTIONS=detect_leaks=0 ctest --output-on-failure
```

### Modes

| Mode | Flag | Description | Window? | CI-Safe? |
|------|------|-------------|---------|----------|
| Interactive | (default) | Real SDL3 window with graph nodes, staves, hit testing | Yes | No |
| Self-test | `--self-test` | Deterministic tests (no window needed) | No | Yes |
| Smoke | `--smoke --quit-after-ms 2000` | Create window, present scene, exit | Yes | Opt-in only |
| Offscreen | `--offscreen` | Render to PPM file, assert golden hash `0x16120cb8d680d0c6` | No | Yes |
| Input log | `--input-log` | Capture mouse/scroll events to CSV | Yes | Manual |
| Native handle | `--native-handle` | Probe Cocoa/Win32/Wayland/X11 handle | Yes (hidden) | Yes |
| Accessibility | `--accessibility-report <path>` | Semantic tree + macOS bridge report | Yes (hidden) | macOS only |

### Display Smoke Test
The smoke test requires a physical display and SDL_RENDER=ON (which auto-enables
SDL_RENDER_METAL on macOS). On macOS Cocoa, SDL_GetWindowSurface uses the Metal texture
framebuffer through the render API — without it, window-surface presentation fails.
```
cmake -B build -G Ninja -DSPIKE_ENABLE_DISPLAY_TESTS=ON
```
When enabled, `spike_smoke` is registered and required to pass. Standard
headless CTest always exits zero without it.

To run smoke directly from a clean build:
```
cmake -B build -G Ninja
cmake --build build
./build/m0_spike --smoke --quit-after-ms 2000 \
    --bravura-font build/bravura_font/Bravura.otf \
    --text-font build/noto_font/NotoSans-Regular.ttf
```

### Font Paths
- `--bravura-font <path>` — Path to Bravura.otf (SMuFL music glyphs, default: `bravura_font/Bravura.otf`)
- `--text-font <path>` — Path to NotoSans-Regular.ttf (Latin text shaping/rasterization, default: `noto_font/NotoSans-Regular.ttf`)

Both fonts are pinned at immutable SHAs. Noto Sans is downloaded via `file(DOWNLOAD)` with
`EXPECTED_HASH` and verified at configure time via SHA-256. NOTO_SANS_SRC can be overridden
via cache variable for offline builds.

## Future Cross-Platform Instructions

### Windows (x86-64, build-only)

```sh
cmake -B build -G "Visual Studio 17 2025" -A x64
cmake --build build --config Release
```

GitHub-hosted Windows runners have no display. Self-test and offscreen
modes should work; smoke mode will fail to create a window.

### Ubuntu 24.04 (x86-64, Wayland/X11)

```sh
sudo apt-get install libxkbcommon-dev libwayland-dev libx11-dev \
  libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev
cmake -B build -G Ninja
cmake --build build
```

GitHub-hosted Linux runners have no display. Self-test/offscreen modes work;
smoke mode may work if a virtual framebuffer (Xvfb) is configured.

### CI Limitations

Standard GitHub-hosted CI runners do NOT have a physical display, so:
- `--self-test` and `--offscreen` are CI-safe.
- `--smoke` requires a display; it will fail on headless CI with a nonzero
  exit code (expected).
- `--input-log` and interactive modes require physical interaction and are
  manual-only.
- `--accessibility-report` requires macOS with AppKit framework.

## VoiceOver Manual Gate

Adam accepted the physical VoiceOver gate with his earlier “Looks good to me.
Proceed” approval. `evidence/14-manual-voiceover.log` records that acceptance
without inventing a navigation or announcement transcript. See ADR 0004 for the
reference checklist and automated bridge coverage.

## Trackpad Input Capture (PASS under revised acceptance)

Adam's first physical attempt FAILED (2026-07-19):
- Two-finger same-direction pan also triggered zoom (the removed distance-based
  pinch heuristic interpreted finger motion as zoom)
- --input-log mode showed no visual feedback during capture

Both issues are fixed (2026-07-20). Wheel events now pan, raw finger motion never zooms
(zoom is pinch-exclusive via native SDL_EVENT_PINCH_UPDATE supplying the scale directly),
and RunInputLog renders every frame. `Test_InputFingerPanOnly` verifies that raw
finger motion remains pan-only.

On 2026-07-21, Adam's second macOS capture reported 512 total events, 488
scroll events, 0 `SDL_EVENT_FINGER_*` records, nonzero viewport response,
applied pan=true, applied pinch=true, and dropped/invalid=0. Adam observed
two-finger horizontal and vertical pan without unwanted zoom plus pinch zoom.
SDL/macOS can provide those working gestures through scroll and pinch streams
without low-level finger events, so `fingerEventCount` is diagnostic only and
the capture is PASS under Adam's explicit waiver. Three-finger behaviour is not
required or claimed.

When SDL does provide low-level touch input, the logger preserves each
`SDL_EVENT_FINGER_DOWN`, `_MOTION`, `_UP`, and `_CANCELED` event as separate
diagnostic telemetry. These records never contribute to `scrollEventCount`.

```sh
cd build
./m0_spike --input-log --input-log-path /tmp/trackpad_capture.csv \
    --input-device trackpad \
    --bravura-font bravura_font/Bravura.otf \
    --text-font noto_font/NotoSans-Regular.ttf
# Perform: two-finger pan (horizontal, vertical), pinch zoom, click on nodes
# Close window; check required metrics in stdout: totalEvents >= 50,
# scrollEventCount >= 30; fingerEventCount is diagnostic only
```

The `--input-device` flag declares the capture hardware for CSV metadata.
Values: `trackpad`, `mouse`, or `unknown` (default). When declared, wheel
events in the CSV use the declared source instead of "unknown". Finger and
pinch events always use their own source types regardless of this flag.

## Ownership

All source files in `src/` are GraphScore-owned and licensed under
Apache-2.0 (SPDX headers present). Third-party libraries are fetched
at build time and are NOT owned by GraphScore.
