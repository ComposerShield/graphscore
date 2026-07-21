# ADR 0004: Rendering and Accessibility Spike Results

| Status | Author | Date |
|--------|--------|------|
| Accepted | Phase C spike | 2026-07-19 |

## Context

Milestone 00 Phase C requires retiring the highest-risk rendering, windowing,
input, and accessibility-platform questions before production architecture
depends on them. A quarantined spike under `spikes/m0/rendering-accessibility/`
was executed on macOS arm64 (Adam's physical Mac). Windows and Linux
GUI/screen-reader tests are deferred per Adam's explicit scope decision:
ordinary GitHub CI cannot exercise them.

## Decisions

### 1. Owned CPU Render-List/Raster Fallback Adopted

This spike uses a GraphScore-owned CPU rasterizer (filled rects, filled circles,
thick lines, glyph blitting, clipping) backed by a retained render-list
abstraction. Reason:

- ThorVG requires Meson/Ninja toolchain pins (not yet specified in ADR 0002).
  Until those pins exist, ThorVG cannot be evaluated.
- The owned render-list approach validates the abstract render-command
  abstraction (ADR 0003 §4.6) before committing to a third-party renderer.
- The CPU rasterizer is intentionally minimal and is **not** a production-grade
  vector renderer. It exists to retire interaction risk, not rendering quality.

The owned render-list/tessellation fallback (ADR 0002 §3b) is confirmed as the
active Phase C path. ThorVG remains PROVISIONAL (excluded from default closure)
pending Meson/Ninja pin resolution.

### 2. Native OS Accessibility Bridge Selected

**AccessKit is EXCLUDED** from the default build (ADR 0002 §2, confirmed here).
The four blockers identified in ADR 0002 §2 remain unresolved:

1. Mutable Corrosion git tag (`v0.6.1` → SHA `1499b14e4906...`)
2. Rust toolchain requirement
3. Incomplete `cargo vendor` + `cargo-deny` transitive audit
4. No `FetchContent`-ready CMake target

**Platform-native accessibility APIs** are selected as the production path:

| Platform | API | Status |
|----------|-----|--------|
| macOS | NSAccessibility (Cocoa) | **Implemented and tested** in this spike |
| Windows | UI Automation (UIA) | **Documented mapping; not implemented** — deferred to physical Windows hardware |
| Linux | AT-SPI2 via D-Bus | **Documented mapping; not implemented** — deferred to physical Linux hardware |

The macOS bridge was implemented as an Objective-C++ adapter (`accessibility_bridge_mac.mm`)
that:
- Creates `NSAccessibilityElement` objects for each semantic-tree node
- Attaches to the SDL3 Cocoa `NSWindow` contentView via an invisible container
  `NSView` subclass
- Exposes roles, labels, values, descriptions, bounds, actions, focus/selection
  state, and child hierarchy to VoiceOver
- Supports `accessibilityPerformPress` action with state-transition callback
- Emits `NSAccessibilityFocusedUIElementChangedNotification`,
  `NSAccessibilitySelectedChildrenChangedNotification`,
  `NSAccessibilityValueChangedNotification`, and
  `NSAccessibilityLayoutChangedNotification`
- Demonstrates safe lifetime: detach removes the container view and releases all
  elements; no dangling Cocoa objects after shutdown
- Confines all Cocoa/AppKit/NSAccessibility types to the `.mm` translation unit;
  the public C++ header (`accessibility_bridge.hpp`) exposes only
  GraphScore-owned types

The non-macOS `IAccessibilityBridge` implementation is a no-op that compiles on
all platforms and documents the intended future mappings.

### 3. SDL3 Window Handle Access Verified

The native-handle probe (`native_handle.cpp`) confirms that SDL3 exposes
platform window handles through the properties API at the pinned SHA
`08b9c55393be5cb08fbec12ca431470faba3c8c9`:

- **macOS**: `SDL_PROP_WINDOW_COCOA_WINDOW_POINTER` → valid `NSWindow*`
- **Windows** (not tested): `SDL_PROP_WINDOW_WIN32_HWND_POINTER`
- **Linux Wayland** (not tested): `SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER`
- **Linux X11** (not tested): `SDL_PROP_WINDOW_X11_WINDOW_NUMBER`

This is sufficient for native editor handle attachment (VST3 plugins) and
native accessibility bridge attachment.

### 4. Deterministic Offscreen Rendering

The owned CPU rasterizer produces byte-for-byte identical output under normal
and ASan+UBSan builds, confirmed by the full-scene golden pixel hash:
`0x16120cb8d680d0c6`.  This hash is asserted at runtime in `RunOffscreenRender`
and must match exactly.

### 5. Vector Rendering and Text Shaping Validated

FreeType (POLICY-CLEARED) and HarfBuzz (PROVISIONAL) successfully:
- Load Bravura SMuFL font (OFL 1.1, spike-only) for music glyphs
- Rasterize SMuFL glyphs through the music FT_Face (G clef, F clef,
  noteheads, accidentals)
- Shape text via HarfBuzz (5 glyphs for "Hello") through the Noto Sans text face
- Enforce face-correct rasterization: GIDs from HarfBuzz text shaping are
  rasterized exclusively through the text FT_Face; SMuFL music codepoints
  are rasterized exclusively through the Bravura music FT_Face. The two
  paths never cross.

The HarfBuzz-FreeType isolation invariant (ADR 0002 §4) is enforced at CMake
configure time: `get_target_property` verifies HarfBuzz has no `freetype` in
`LINK_LIBRARIES`.

### 6. Reproducible Latin Text Font Pinned

Noto Sans Regular is pinned from the notofonts/noto-fonts repository at immutable
SHA `ffebf8c1ee449e544955a7e813c54f9b73848eac` (OFL 1.1, committed at
`docs/licenses/NotoSans-OFL.txt`). This font provides deterministic Latin text
shaping and glyph rasterization for all self-tests.

- Downloaded as a single immutable artifact via `file(DOWNLOAD)` from the
  exact SHA-pinned raw URL, with `EXPECTED_HASH SHA256=d78a4640e19e06c128e2041d480d5ddfd8db4fdecb3d582ca12b26aef1548bf9`.
  NOTO_SANS_SRC can be overridden via cache variable for offline/disconnected builds.
- Every acquisition path (download, NOTO_SANS_SRC override, pre-existing destination)
  verifies SHA-256 at configure time and fails with `FATAL_ERROR` on mismatch.
- `--text-font` controls the text font path; passed to SelfTest and all
  relevant CTest targets.  Default: `noto_font/NotoSans-Regular.ttf`.
- No system-font fallback (Helvetica, CWD, etc.) is permitted. The exact pinned
  artifact is required.
- HarfBuzz metrics, exact GID assertions {43,72,79,79,82} for "Hello" at 32px,
  golden pixel hash `0xca4a2991491e720f`, placement/clipping/transform, and
  stable hash assertions are verified against this exact font artifact.
- Bravura remains exclusively for SMuFL music glyph use; the Noto
  Sans text face is a distinct FreeType FT_Face.

### 7. Green Standard CTest

The `SPIKE_ENABLE_DISPLAY_TESTS` CMake option (default OFF) gates the
display-attached `spike_smoke` test. Standard `ctest --output-on-failure`
exits zero with all tests passing.

- `spike_smoke_headless_negative` forces an invalid `SDL_VIDEO_DRIVER` env
  so smoke deterministically exits nonzero even on a display machine;
  `WILL_FAIL` documents this as expected behaviour.
- Display smoke is VERIFIED PASS from clean source build (exit 0, genuine
  Cocoa window presentation on Adam's physical Mac).  The CMakeLists.txt
  requires `SDL_RENDER=ON` — on macOS Cocoa, this auto-enables
  `SDL_RENDER_METAL`, which provides the Metal texture framebuffer path
  that `SDL_GetWindowSurface` needs.  Without it, Cocoa has no software
  `CreateWindowFramebuffer` and presentation fails.
- ASan+UBSan CTest also exits zero (100% pass, zero sanitizer findings).
- Standard CTest is 100% pass on both display-capable and headless
  environments because the negative test is forced deterministic.

### 8. macOS Trackpad Telemetry Waiver (Adam, 2026-07-21)

Adam explicitly waived the former minimum of two low-level
`SDL_EVENT_FINGER_*` records after a physical macOS retest. SDL/macOS may
represent working two-finger trackpad pan and pinch through scroll and pinch
streams without delivering those low-level finger events. `fingerEventCount`
therefore remains reported diagnostic telemetry, not a fidelity pass threshold.

When available, each supported low-level event (`SDL_EVENT_FINGER_DOWN`,
`_MOTION`, `_UP`, and `_CANCELED`) is logged as distinct diagnostic telemetry.
These records do not contribute to scroll counts. Up and cancellation only end
the identified tracked finger and never synthesize viewport motion.

The meaningful physical acceptance is two-finger pan in every direction without
unwanted zoom and pinch zoom with an actual scale change. Adam observed both.
Three-finger behaviour is not required and is not claimed or implemented.

## Semantic Accessibility Model

A GraphScore-owned, toolkit-neutral semantic accessibility model was designed
and implemented (`semantic_tree.hpp`/`.cpp`):

- **Stable IDs**: string UUIDs (e.g. `gs_node_A`, `gs_note_m1E4`)
  — `AddNode` rejects duplicates
- **Roles**: Application, Window, Group, GraphNode, Connector, Measure, Note,
  Control, Selection, ActionItem, Label, Staff
- **Properties**: name, value, description, world-space bounds
- **State**: Selected, Focused, Disabled, Editable, Checked (bitmask)
- **Keyboard focus**: separate boolean
- **Actions**: id + description pairs (e.g. `{"select","Select this node"}`)
- **Hierarchy**: `SetParentChild`, `Reparent`, `DetachChild` — atomic mutations
  that update both old/new parent and child vectors, reject cycles, and
  increment the monotonic generation counter
- **Hit testing**: screen-space point → node index via `ComputeScreenBounds`
- **Zoom/pan integration**: `ComputeScreenBounds(nodeIdx, transform)` converts
  world bounds to screen coordinates

The demo semantic tree contains 23 nodes representing: application root, 4 graph
nodes, 3 connectors, 3 measures, 8 notes, 1 control, 1 selection placeholder,
1 action item, and 2 staff groups.

### Platform Mappings

| Semantic Role | macOS (NSAccessibility) | Windows (UIA) | Linux (AT-SPI2) |
|---------------|------------------------|---------------|-----------------|
| Application | `NSAccessibilityApplicationRole` | `Window` control type | `ROLE_APPLICATION` |
| GraphNode | `NSAccessibilityGroupRole` | `Group` control type | `ROLE_PANEL` |
| Connector | `NSAccessibilityImageRole` | `Image` control type | `ROLE_IMAGE` |
| Measure | `NSAccessibilityGroupRole`, subrole `GSMeasure` | `Group` | `ROLE_PANEL` |
| Note | `NSAccessibilityButtonRole`, subrole `GSNote` | `Button` | `ROLE_PUSH_BUTTON` |
| Control | `NSAccessibilityButtonRole` | `Button` | `ROLE_PUSH_BUTTON` |
| ActionItem | `NSAccessibilityButtonRole` | `Button` | `ROLE_PUSH_BUTTON` |
| Selection | `NSAccessibilityGroupRole` | `List` | `ROLE_LIST` |
| Label | `NSAccessibilityStaticTextRole` | `Text` | `ROLE_LABEL` |

**Windows UIA and Linux AT-SPI2 mappings are documented for implementation
reference only. They have not been implemented or tested.**

## Test Coverage

### Platform-Neutral Self-Tests

All pass deterministically under normal and ASan+UBSan builds (no window required).
Exact test count recorded in evidence logs; test names enumerated in
`evidence/04-self-test.log`. Key categories include:
- Vec2 math, Rect ops, Transform round-trip — 8 tests
- Hit test deterministic, rounded rect, orthogonal connector — 5 tests
- Clip rect, clip pixel assertions, staff lines — 6 tests
- Font loading, HarfBuzz shaping, SMuFL codepoints — 6 tests
- Offscreen render hash deterministic — 2 tests
- Shaped glyph rendering, glyph pixel assertions, glyph transform/clip — 6 tests
- Glyph text-face vs music-face distinction, face-correct rasterization — 2 tests
- Text/Music font distinct faces, `RasterizeGlyphByIndex` face parameter — 2 tests
- Orthogonal fillets: all 4 turns, reversed traversal, short clamp, zero-length,
  duplicate, non-axis, arc continuity, pixel-level raster, command geometry
  inspection, quadrant-specific pixel assertions — 31 tests
- Orthogonal fillet signed-sweep arc: ±π/2 containment, per-quadrant pixel proof — 16 tests
- Semantic tree: representative types — 8 tests
- Semantic tree: stable IDs unique/non-empty, FindByStableId — 4 tests
- Semantic tree: hierarchy, parent-child consistency — 4 tests
- Semantic tree: labels and values — 3 tests
- Semantic tree: selection/focus/keyboard focus state — 4 tests
- Semantic tree: actions present — 2 tests
- Semantic tree: transformed bounds — 5 tests
- Semantic tree: hit testing — 4 tests
- Semantic tree: action transition — 3 tests
- Semantic tree: mutation validation (cycles, OOB, self-parent, duplicate) — 9 tests
- Semantic tree: generation counter monotonicity — 4 tests
- Semantic tree: invalid action rejection — 5 tests
- Semantic tree: lifetime/detach (clear, rebuild, Reparent, DetachChild) — 8 tests
- Semantic tree: duplicate stable ID rejection — 2 tests
- Semantic tree: Reparent/DetachChild atomic integrity — 8 tests
- Semantic tree: empty ID rejection, SetRoot validation, Reparent exception-safety — 10 tests
- Bridge SyncGeometry spy: pan/zoom/reset/keyboard — 8 tests
- Input pan/pinch/scroll math — 14 tests
- Input fidelity: pass/fail, physical-capture thresholds, timestamp drops, viewport response — 18 tests
- Real-path fidelity: typed-result accumulation, pan-only/pinch-only/keyboard fail, pan+pinch pass — 5 tests
- Input event focal priority: event branch vs centroid/center — 1 test
- Real SDL event E2E: wheel, finger-down/motion/up/canceled, pinch, keyboard, quit —
  transform mutation, SyncGeometry spy, honest source, summary from resulting
   log (droppedOrInvalid==0, verdict exact PASS), strictly monotonic timestamps — 31 tests
- Text face golden proof (Noto Sans): golden GIDs {43,72,79,79,82}, UPEM-scaled
  advances, per-glyph rasterization, golden pixel hash `0xca4a2991491e720f`,
  horizontal placement, transform movement, clipping, zero-coverage,
  music-face distinctness — 19 tests
- Native handle: null window, unavailable, negative-path — 8 tests
- Rasterizer: non-null glyph cache invariant — 1 test
- Glyph/shape cache stability: repeated frames reuse entries, sizes stabilize,
  new text/pixel-size predictable growth, invalid FontFace fails — 12 tests

### macOS Native Accessibility Bridge Test (Automated, In-Process)

Requires a real SDL window (CTest: `spike_accessibility_report`):
- Bridge attaches to SDL3 Cocoa NSWindow contentView
- 23 NSAccessibilityElement objects exposed via stable-ID mapping
- Each element verified: role, label, identifier, frame, actions
- Press actions exposed via standard `NSAccessibilityPressAction`
- Select/zoom actions exposed via `NSAccessibilityCustomAction` objects
- Action test: play press (value: "" → "playing" → "stopped"), select→announce
  →deselect cycle, zoom in/out via custom actions
- Notifications emitted: `FocusedUIElementChanged`, `LayoutChanged`,
  `SelectedChildrenChanged`, `ValueChanged`
- Exact frame proof: X/Y/width/height assertions with Y-inversion and
  backing-scale tolerance against known content-view geometry
- Mutation test: add node → duplicate ID rejection → Reparent → DetachChild
  → clear → rebuild → action post-rebuild succeeds
- Bridge detaches cleanly, no dangling Cocoa objects; double-detach idempotent
- CTest returns nonzero on failure; zero findings under ASan+UBSan

**This is automated, in-process NSAccessibility API verification — not VoiceOver
navigation. The bridge API is verified programmatically. See VoiceOver manual
checklist below for interactive screen-reader testing.**

### VoiceOver Manual Gate (Accepted by Adam)

The bridge infrastructure is fully implemented and verified in-process (accessible
to VoiceOver via the standard NSAccessibility protocol). Adam accepted this
physical gate with his earlier “Looks good to me. Proceed” approval, recorded in
`evidence/14-manual-voiceover.log`. That acceptance does not fabricate a
VoiceOver navigation transcript or specific announcements. The automated
accessibility report does not exercise VoiceOver speech output; it verifies the
underlying API objects exactly as VoiceOver would access them.

## Deterministic Verification Modes

| Flag | Description | Window? | CI-Safe? |
|------|-------------|---------|----------|
| `--self-test` | Deterministic tests (no window needed); exact count in evidence logs | No | Yes |
| `--offscreen` | Render to PPM, print pixel hash | No | Yes |
| `--smoke` | Real window, present to surface | Yes | Opt-in only |
| `--native-handle` | Probe NSWindow/HWND/wl_surface | Yes (hidden) | Yes |
| `--accessibility-report <path>` | Semantic tree + native bridge report | Yes (hidden) | macOS only |

## Windows and Linux Deferral

Per Adam's explicit scope decision: empirical GUI and screen-reader interaction
is required now only on his physical Mac. The following remain **future physical
gates** — they are not failures:

| Gate | Platform | Status |
|------|----------|--------|
| Windows UIA/Narrator integration | Windows 11 x86-64 | DEFERRED (physical device) |
| Linux AT-SPI2/Orca integration | Ubuntu 24.04 x86-64 | DEFERRED (physical device) |
| Wayland window open and render | Ubuntu 24.04 | DEFERRED |
| X11/XWayland window open and render | Ubuntu 24.04 | DEFERRED |
| Windows arm64 build | Windows 11 arm64 | DEFERRED (build-only) |
| Linux arm64 build | Ubuntu 24.04 arm64 | DEFERRED (build-only) |

The signal-chain for each deferred platform is documented with exact
reproducible build commands and API paths. No fabricated Narrator/Orca
transcripts are included.

## macOS Implementation: Credible Path

The macOS bridge is fully implemented, compiles cleanly with `-Weverything
 -Werror`, passes all 476 self-tests in normal and ASan+UBSan builds, executes the automated
accessibility report mode successfully (0 failures), and demonstrates the
complete lifecycle (attach → traverse → select → custom action → notify →
detach). The bridge exposes `NSAccessibilityPressAction` for press-type
actions and `NSAccessibilityCustomAction` objects for select/zoom actions,
matching exactly the API surface VoiceOver uses. The bridge can be exercised
through VoiceOver on Adam's Mac using the interactive mode (`./m0_spike`).

## Unresolved Risks

1. **ThorVG Meson/Ninja dependency** remains un-pinned. Owned render-list
   fallback is explicitly adopted. ThorVG remains PROVISIONAL.
2. **AccessKit** remains EXCLUDED. Native OS accessibility APIs are the
   production path. The non-macOS implementations are documented but not
   implemented.
3. **VoiceOver speech-level detail** was not transcribed. Adam explicitly
   accepted the physical gate; the bridge API is verified separately.
4. **Windows and Linux native accessibility bridges** require physical test
   devices. Credible implementation mappings exist but have not been tested.
5. **Font rendering quality** with the owned CPU rasterizer (nearest-neighbor
   blitting, no sub-pixel antialiasing) is insufficient for production. This
   is acceptable for a spike.

## Consequences

- The owned render-list/raster fallback is the active Phase C rendering path.
  ThorVG is documented as PROVISIONAL. The ADR 0002 §3b fallback is confirmed.
- Platform-native accessibility APIs are the production path. AccessKit remains
  EXCLUDED from the default build. The ADR 0003 §8 unresolved row is resolved
  by this ADR.
- `graphscore_accessibility_platform` (ADR 0003 §4.5) will depend privately on
  exactly one native OS accessibility framework per platform (NSAccessibility on
  macOS, UI Automation on Windows, AT-SPI2 on Linux). No vendored CMake target
  is required; system frameworks are not a permissive licensing concern.
- The semantic accessibility model (`graphscore_accessibility` target) is
  validated with representative graph node, connector, measure, note, control,
  selection, and action-item types. It is ready for production use.
- Windows and Linux screen-reader integration gates remain deferred to
  physical-device testing. This does not block Phase C exit.
- ASan+UBSan CTest passes with zero findings. TSan is N/A (no concurrency).

## Evidence Artifacts

| File | Description |
|------|-------------|
| `evidence/01-configure-summary.log` | Current configure summary — Bravura OTF copied, Noto Sans artifact integrity verified |
| `evidence/03-ctest.log` | CTest — 100% passed (6/6), including the expected invalid PPM-output-path failure; headless negative forced deterministic via SDL_VIDEO_DRIVER |
| `evidence/04-self-test.log` | Self-test — 476/476 PASSED (normal build) |
| `evidence/05-offscreen.log` | Offscreen PPM — hash `0x16120cb8d680d0c6` (golden constant, deterministic across normal+ASan) |
| `evidence/06-native-handle.log` | Cocoa NSWindow* handle — non-null, valid |
| `evidence/07-smoke.log` | Smoke — clean source build, exit 0, Metal-accelerated |
| `evidence/09-asan-self-test.log` | ASan+UBSan self-test — 476/476 PASSED, zero findings |
| `evidence/09-asan-offscreen.log` | ASan+UBSan offscreen — same hash `0x16120cb8d680d0c6` |
| `evidence/10-clean-build-provenance.log` | Command-attributed clean-build transcript — source manifest SHA-256, full configure/build/test/smoke/ASan provenance |
| `evidence/11-a11y-bridge-report.log` | macOS bridge stdout — NSAccessibility API verification, 0 failures |
| `evidence/12-asan-ctest-a11y.log` | ASan+UBSan CTest — 6/6 (100%) passed, zero sanitizer findings |
| `evidence/13-manual-display-smoke.log` | Manual display smoke — clean source, exit 0, genuine Cocoa presentation |
| `evidence/14-manual-voiceover.log` | Adam's explicit VoiceOver-gate acceptance; no fabricated announcement transcript |
| `evidence/15-manual-trackpad.log` | Adam's second macOS capture — PASS under the revised diagnostic-only finger telemetry rule |
| `build/spike_output.ppm` | Deterministic offscreen PPM (hash `0x16120cb8d680d0c6`) |

## VoiceOver Manual Checklist (Reference Only)

This checklist exercises the macOS accessibility bridge through real VoiceOver
interaction. Run after building the spike:

```sh
cd spikes/m0/rendering-accessibility/build
# Enable VoiceOver: System Settings → Accessibility → VoiceOver → On
# (or Cmd+F5 on most Mac keyboards)
./m0_spike
```

Steps:

1. [ ] With VoiceOver active, focus the GraphScore spike window. Verify
   VoiceOver announces "GraphScore Spike, application".
2. [ ] Use VO+Right Arrow to navigate through graph nodes. Verify VoiceOver
   reads "Node A, group", "Node B, group", etc.
3. [ ] Navigate to a note (e.g., "E4, button"). Verify the note pitch and
   duration are announced.
4. [ ] Navigate to the Play button ("Play, button"). Verify it's presented as
   an actionable item.
5. [ ] Press VO+Space on the Play button. Verify the action is triggered
   (check stdout for "Action callback" line).
6. [ ] Navigate through the connector elements. Verify VoiceOver announces
   connector names ("A→B", "B→C", "B→D").
7. [ ] Use VO+Shift+Down to interact with the staves group. Navigate into
   measures and notes within.
8. [ ] Close the window (Escape or Cmd+W). Verify no crash or dangling
   accessibility reference.
9. [ ] Check `Console.app` for any accessibility-related errors from the
   `m0_spike` process.

**Gate status: accepted by Adam's earlier “Looks good to me. Proceed” approval;
no individual spoken-announcement observations are asserted.**

### Trackpad Input Capture (PASS — 2026-07-21 revised acceptance)

Adam's first physical attempt (2026-07-19) FAILED:
- Interactive mode: two-finger same-direction pan also triggered zoom
  (finger-motion handler derived scale from per-finger distance updates)
- --input-log mode did not appear to respond to the trackpad
  (RunInputLog processed events but never re-rendered)

Both issues are fixed (2026-07-20) with the following authoritative changes:
1. Finger motion now does centroid pan ONLY; all zoom is exclusively via
   SDL_EVENT_PINCH_UPDATE (single authoritative pinch path).
2. PINCH_UPDATE focal point cascade: event focus_x/focus_y → active
   two-finger centroid → window center (documented fallback).
3. Fidelity summary now separately requires at least one pan outcome
   (nonzero offset change) AND at least one pinch outcome (nonzero scale
   change) for PASS.
4. 476 self-tests pass (normal + ASan), CTest 6/6 green, offscreen hash
   0x16120cb8d680d0c6 deterministic.

Adam completed a second physical macOS capture after the codefix. Its factual
summary was: 512 total events, 488 scroll events, 0 finger events, nonzero
viewport response, applied pan=true, applied pinch=true, and dropped/invalid=0.
The old verdict failed only on the removed finger-event threshold. Under Adam's
2026-07-21 explicit waiver, this capture is **PASS**: it demonstrates observed
two-finger pan in all directions without unwanted zoom and observed pinch zoom.
`fingerEventCount` is retained as SDL telemetry only. Three-finger behaviour is
not required and is not claimed.

The physical retry command remains available for future captures:

```sh
cd spikes/m0/rendering-accessibility/build
./m0_spike --input-log --input-log-path /tmp/trackpad_capture.csv \
    --input-device trackpad \
    --bravura-font bravura_font/Bravura.otf \
    --text-font noto_font/NotoSans-Regular.ttf
```

**Required physical actions:**
1. Two-finger pan in multiple directions (must change offset, NOT scale)
2. Pinch-zoom gesture (must change scale, NOT offset alone)
3. Combined two-finger pan + pinch sequences (no double-zoom)
4. Close window (Esc); verify stdout shows:
   ```
   verdict: PASS — all fidelity thresholds met
   pan outcome: yes
   pinch outcome: yes
   ```

**Required summary fields for a future capture:**
- `totalEvents` ≥ 50
- `scrollEventCount` ≥ 30
- `fingerEventCount` reported diagnostically (no required minimum)
- `captureDurationS` ≥ 0.5
- `peakEventRateHz` ≥ 0.5
- `monotonicTimestamps`: true
- `hasSubpixelDeltas`: true
- `droppedOrInvalid`: 0
- `hasNonzeroViewportResponse`: true
- `hasAppliedPanOutcome`: true
- `hasAppliedPinchOutcome`: true
- `verdict`: "PASS — all fidelity thresholds met"

See `evidence/15-manual-trackpad.log` for the preserved first attempt, second
capture metrics, and revised evaluation.

## Reproducible Build Commands

### Normal build
```sh
cd spikes/m0/rendering-accessibility
cmake -B build -G Ninja
cmake --build build
cd build && ctest --output-on-failure
```

### ASan+UBSan build
```sh
cd spikes/m0/rendering-accessibility
cmake -B build -G Ninja -DSPIKE_ENABLE_ASAN=ON
cmake --build build
cd build && ASAN_OPTIONS=detect_leaks=0 ctest --output-on-failure
```

### Display-attached smoke (Adam's Mac only)
```sh
cd spikes/m0/rendering-accessibility
cmake -B build -G Ninja
cmake --build build
./build/m0_spike --smoke --quit-after-ms 2000 \
    --bravura-font build/bravura_font/Bravura.otf \
    --text-font build/noto_font/NotoSans-Regular.ttf
```
SDL_RENDER=ON is required for macOS Cocoa window-surface presentation.
SDL_RENDER_METAL is auto-detected ON via SDL_RENDER+APPLE.

### Accessibility report (macOS only)
```sh
cd build
./m0_spike --accessibility-report /tmp/a11y_report.txt \
    --bravura-font bravura_font/Bravura.otf \
    --text-font noto_font/NotoSans-Regular.ttf
cat /tmp/a11y_report.txt
```

### Interactive mode (with VoiceOver)
```sh
./m0_spike --bravura-font bravura_font/Bravura.otf \
    --text-font noto_font/NotoSans-Regular.ttf
```
