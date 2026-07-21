# Evidence Catalog — M0 Rendering & Accessibility Spike

Generated: 2026-07-21 | AppleClang 17.0.0 | macOS 15.7.7 arm64

## Evidence Files

| File | Description | Status |
|------|-------------|--------|
| `00-tool-versions.txt` | Compiler, CMake, Ninja, Git, OS versions | Generated |
| `00-tsan-n-a.txt` | TSan N/A explanation | Written |
| `00-cross-platform-status.txt` | Windows/Linux/arm64 DEFERRED; Adam accepted VoiceOver and revised-trackpad gates | Updated |
| `00-pin-resolution.txt` | SDL3, FreeType, HarfBuzz, Bravura, Noto Sans (SHA + artifact SHA-256) pin verification | Updated |
| `01-configure-summary.log` | Clean normal configure — HB-FT isolation, all fonts copied, artifact integrity verified | Generated |
| `02-build.log` | Clean normal build — warnings-as-errors target completed successfully | Generated |
| `03-ctest.log` | CTest — 6/6 passed (100%), including expected invalid PPM-output-path failure; headless negative forced deterministic via SDL_VIDEO_DRIVER | Generated |
| `04-self-test.log` | Self-test — 476/476 PASSED (normal build) | Generated |
| `05-offscreen.log` | Offscreen PPM — hash `0x16120cb8d680d0c6` asserted as golden | Generated |
| `06-native-handle.log` | Cocoa NSWindow* handle — non-null, valid | Generated |
| `07-smoke.log` | Display-attached smoke — clean source build, exit 0, Metal-accelerated | PASS (clean source) |
| `08-asan-configure.log` | Clean ASan+UBSan configure | Generated |
| `08-asan-build.log` | Clean ASan+UBSan build — warnings-as-errors target completed successfully | Generated |
| `09-asan-self-test.log` | ASan+UBSan self-test — 476/476 PASSED, zero findings | Generated |
| `09-asan-offscreen.log` | ASan+UBSan offscreen — hash `0x16120cb8d680d0c6` (deterministic) | Generated |
| `10-clean-build-provenance.log` | Command-attributed clean-build transcript — source manifest digest, configure/build/test/smoke/ASan provenance | Generated |
| `11-a11y-bridge-report.log` | macOS bridge stdout — full NSAccessibility traversal/action/frame/mutation/detach | Generated |
| `11-a11y-report-file.txt` | macOS bridge report file — semantic tree dump + summary | Generated |
| `12-asan-ctest-a11y.log` | ASan+UBSan CTest — 6/6 passed, zero sanitizer findings | Generated |
| `13-manual-display-smoke.log` | Manual display smoke — clean source, exit 0, genuine Cocoa presentation | PASS (clean source) |
| `14-manual-voiceover.log` | Adam's explicit physical-gate approval; no fabricated announcement transcript | ACCEPTED |
| `15-manual-trackpad.log` | Adam's second capture: 512 total, 488 scroll, 0 finger; PASS under revised criteria | PASS |

## Generated Assets (in build/)

| Asset | Description |
|-------|-------------|
| `build/spike_output.ppm` | Deterministic offscreen PPM (800x600, P6 binary) |
| `build-asan/spike_output.ppm` | Offscreen PPM under ASan (same hash) |
| `build/spike_native_handle.txt` | Native handle probe result |
| `build/spike_accessibility_report.txt` | Comprehensive a11y report (semantic tree + macOS bridge) |

## Deterministic Hash

The full-scene offscreen pixel hash `0x16120cb8d680d0c6` is asserted at runtime
in `RunOffscreenRender`.  It is produced identically with and without ASan+UBSan,
proving deterministic execution of the owned CPU rasterizer.

Text-render golden pixel hash: `0xca4a2991491e720f` ("Hello" at 32px, Noto Sans,
400x80 raster).

## Current State

- **Test count**: 476/476 self-tests (normal), 476/476 (ASan+UBSan), zero sanitizer findings.
- **CTest**: 6/6 passed (normal), 6/6 passed (ASan), including the expected invalid PPM-output-path test.
- **Headless negative**: Forced deterministic via `SDL_VIDEO_DRIVER=__headless_negative_force_fail__` env; `WILL_FAIL` documented.
- **Display smoke**: PASS from clean source build. SDL_RENDER=ON with SDL_RENDER_METAL auto-detected ON needed for macOS Cocoa window-surface presentation. Nothing else changed.
- **Timestamps**: Production `MonotonicNowUs()` guarantees strictly increasing via `steady_clock` + bump. E2E test injects deterministic clock. No `const_cast` on event timestamps anywhere.
- **Noto Sans**: `file(DOWNLOAD)` with `EXPECTED_HASH SHA256=d78a4640e19e06c128e2041d480d5ddfd8db4fdecb3d582ca12b26aef1548bf9`. `NOTO_SANS_SRC` FILEPATH override. Hash-verified at configure time. No BravuraText/system-font fallback.
- **Shape cache**: Heterogeneous transparent lookup via `string_view` — zero string allocation on cache hits. `ShapeKeyAllocCount`/`ShapeCacheMissCount` instrumentation.
- **Golden glyphs**: GIDs {43,72,79,79,82}, per-glyph advances {23.712, 18.048, 8.256, 8.256, 19.360} for "Hello" at 32px.
- **Golden hash**: Offscreen `0x16120cb8d680d0c6`, text pixel `0xca4a2991491e720f`.
- **Accessibility**: 23 semantic nodes, NSAccessibility bridge with press/custom actions, 0 automated failures. Report stdout + report file saved separately.
- **Fidelity pipeline**: real-path verdict tests dispatch `SDL_Event` values through
   `ProcessViewportSDLEvent`, accumulate each `ViewportEventResult` with the
   shared `ViewportOutcomeAccumulator`, then call `InputLogger::ComputeFidelitySummary`.
   The only summary call with hand-constructed outcome values is the clearly scoped
   empty-capture/constant unit test.
- **Physical input gate**: PASS requires `totalEvents >= 50` and
  `scrollEventCount >= 30`, in addition to duration, rate, monotonic timestamp,
  valid-record, subpixel, gesture, viewport-response, applied-pan, and
  applied-pinch criteria. `fingerEventCount` measures only low-level
  `SDL_EVENT_FINGER_DOWN`, `_MOTION`, `_UP`, and `_CANCELED` telemetry and has
  no required minimum: SDL/macOS can deliver correct two-finger pan/pinch
  through scroll and pinch streams. Actual SDL-event boundary tests cover
  49/50 total, 29/30 scroll, zero-finger PASS, all delivered finger variants,
  and the fact that finger telemetry does not inflate scroll counts.

## Accessibility Report Structure

The automated accessibility report `--accessibility-report` produces:
- **Stdout** (`11-a11y-bridge-report.log`): Full command-attributed NSAccessibility traversal with
  element roles/labels/frames, action test (press, custom actions), zoom test,
  transform update, frame conversion (SDL→Cocoa Y-invert), mutation/rebuild,
  selection/focus, hierarchy verification, and final 0-failures verdict.
- **Report file** (`11-a11y-report-file.txt`): Semantic tree dump + macOS bridge summary.

## Manual Evidence

### VoiceOver Manual Gate
Adam accepted the gate via his earlier “Looks good to me. Proceed” approval.
No spoken announcements or individual actions are fabricated; see
`14-manual-voiceover.log`.

### Trackpad/Input Fidelity (Physical Capture — PASS)

Adam's second capture (2026-07-21) reported 512 total events, 488 scroll
events, 0 finger events, nonzero viewport response, applied pan=true, applied
pinch=true, and dropped/invalid=0. It passes Adam's revised acceptance: working
two-finger pan in all directions without unwanted zoom and pinch zoom are the
physical requirements; three-finger behaviour is not required. See
`15-manual-trackpad.log` for factual detail and the retained retry command.
```sh
cd build
./m0_spike --input-log --input-log-path /tmp/trackpad.csv \
    --input-device trackpad \
    --bravura-font bravura_font/Bravura.otf --text-font noto_font/NotoSans-Regular.ttf
```

## DEFERRED — Per Adam's Scope

See `00-cross-platform-status.txt` for Windows/Linux/macOS x86-64/arm64 build instructions,
Windows UIA/Narrator, Linux AT-SPI2/Orca implementation status.

## Historical Evidence

Removed on 2026-07-21. Earlier passes recorded superseded self-test counts
(362, 436) that conflicted with the final 476 and repeatedly caused confusion.
The logs in this directory are the only authoritative record. Prior passes are
in the git history.
