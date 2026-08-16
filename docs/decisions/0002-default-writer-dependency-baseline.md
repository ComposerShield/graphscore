# ADR 0002: Default Writer Dependency Baseline

| Status | Author | Date |
|--------|--------|------|
| Accepted | Phase A evaluation | 2026-07-19 |

## Context

GraphScore M0 Phase A requires evaluation of a permissive writer shell
assembled from candidates: SDL3, AccessKit, a backend-neutral vector
renderer, HarfBuzz, FreeType, an audio-device library, and a MIDI
utility library. This ADR records the evidence-backed evaluation,
immutable pins, and a fallback matrix.

The evaluation is based on authoritative upstream sources current as of
2026-07-19. No runtime or platform integration spikes have been
performed. All statuses are policy/license review only, not empirical
build or platform verification. Where a dependency requires a CMake
adapter (build-system integration code), the adapter is specified here
as an UNEXECUTED M1 requirement; the dependency remains PROVISIONAL
until the adapter is implemented and a configure-cache evidence report
confirms the options apply correctly.

**Repository status note**: `docs/plan/README.md`, `CHECKLIST.md`, and
the `00-` through `12-` milestone plan files are pre-existing canonical
project inputs that provide the task specification. They are currently
untracked because the repository has not yet committed documentation;
they are not Phase A implementation and are not altered here.

## Status Boundaries

- **POLICY-CLEARED**: License at the pinned SHA is permissive; known
  transitive dependencies reviewed; patent and redistribution terms
  are compatible with Apache-2.0 default build. No CMake adapter or
  empirical build verification has been performed. Dependencies that
  require a CMake adapter remain PROVISIONAL until the adapter is
  implemented and verified.
- **PROVISIONAL**: License passes, but a build-system adapter or
  platform spike is required before final acceptance, or an unresolved
  blocker prevents inclusion.
- **EXCLUDED**: Explicitly excluded from the default build. Reason
  and blocker recorded. License files committed for reference.
- **REJECTED**: GPL/AGPL/copyleft, mandatory commercial terms, or no
  viable integration path.
- **DEFERRED**: Evaluation belongs to a dedicated phase (e.g. VST3 host
  spike).

## Dependency Evaluation

### 1. SDL3 — Windowing, Input, GPU Abstraction, Audio Device

| Property | Value |
|----------|-------|
| Repository | https://github.com/libsdl-org/SDL |
| Pinned commit SHA | `08b9c55393be5cb08fbec12ca431470faba3c8c9` (2026-07-18) |
| License | zlib License |
| License URL at SHA | https://raw.githubusercontent.com/libsdl-org/SDL/08b9c55393be5cb08fbec12ca431470faba3c8c9/LICENSE.txt |
| Committed license | `docs/licenses/SDL3-LICENSE.txt` |
| License SPDX | Zlib |
| Patent grant | None (zlib license; no patent clause) |
| Transitive dependencies | **Vendored HIDAPI** (`src/hidapi/`, tri-licensed: GPLv3 / BSD 3-clause / Original HIDAPI). GraphScore selects the **BSD 3-clause** license when HIDAPI is enabled. HIDAPI is **disabled by default** in the explicit GraphScore option set because no subsystem requiring it (Joystick, Haptic, Sensor) is enabled. No `src/3rdparty/` directory exists at this SHA. All other backends (X11, Wayland, Cocoa, Win32, DirectSound, WASAPI, etc.) are platform SDKs/OS services, not vendored dependencies. |
| Build tools | CMake 3.16+, C compiler |
| FetchContent ready | Yes. SDL3 supports `add_subdirectory` and exposes `SDL3::SDL3`, `SDL3::SDL3-static`, `SDL3::SDL3-shared` targets. |
| Platform support | macOS, Windows, Linux (X11 + Wayland), iOS, tvOS, visionOS, Android, Emscripten, FreeBSD, Haiku, and others |
| Architecture support | x86-64, arm64 (Apple Silicon, Windows arm64, Linux arm64), x86 (32-bit), ARM32, LoongArch64 |
| Notices | Copyright (C) 1997-2026 Sam Lantinga. No mandatory attribution required. Notice retained in `docs/licenses/SDL3-LICENSE.txt`. |
| Warning isolation | SDL3 can be consumed as a SYSTEM include. |

**Decision**: PROVISIONAL. SDL3 is license-cleared (zlib) and provides
the baseline platform shell (windowing and input). SDL3 is **not** a
vector renderer and does not expose a path-submission or canvas API
for notation rendering. See Section 3.

**Status rationale**: The option tables below cover the reviewed
dependency, backend, and subsystem options needed to define the
permissive closure. They are an UNEXECUTED adapter blueprint for M1,
not an applied configuration, and are **not exhaustive** — additional
upstream CMake defaults (see §Remaining Unaedited Defaults) must be
audited during the M1 configure-cache pass. No options have been
empirically verified via a CMake configure run. SDL3 remains
PROVISIONAL until M1 produces a configure-cache evidence report
confirming the specified options take effect and the remaining
unaudited defaults are resolved.

**Deterministic permissive closure — reviewed dependency/backend
options at pinned SHA** `08b9c55393be5cb08fbec12ca431470faba3c8c9`:

Platform SDKs (Cocoa, Win32, Wayland, X11) remain enabled through
platform-defaulted `dep_option` and `set_option` calls that bind to
system services, not vendored libraries. All optional integrations
whose transitive license, patent, or redistribution terms have not
been fully reviewed are explicitly disabled.

##### Remaining Unaudited Defaults (M1 Audit Required)

The following upstream CMake options default ON (or have default values
that affect the build artifact composition) and have NOT been audited
in this Phase A review. They must be decided and documented during the
M1 configure-cache pass before SDL3 can be accepted as POLICY-CLEARED:

| Category | Options | Current Default |
|----------|---------|-----------------|
| Build artifacts | `SDL_TEST_LIBRARY` | ON (builds `SDL3_test` static lib) |
| Build artifacts | `SDL_EXAMPLES` | OFF (default) |
| Library type | `SDL_SHARED` / `SDL_STATIC` | Shared ON, static OFF (default) |
| Assembly / SIMD | `SDL_ASSEMBLY`, `SDL_MMX`, `SDL_SSE`, `SDL_SSE2`, `SDL_SSE3`, `SDL_SSE4_1`, `SDL_SSE4_2`, `SDL_AVX`, `SDL_AVX2`, `SDL_AVX512F`, `SDL_ALTIVEC`, `SDL_ARMNEON`, `SDL_ARMSVE2`, `SDL_LSX`, `SDL_LASX` | All ON (availability-tested) |
| Threading | `SDL_PTHREADS`, `SDL_PTHREADS_SEM` | ON on Unix/macOS |
| Libc/toolchain | `SDL_LIBC` | ON |
| Libc/toolchain | `SDL_GCC_ATOMICS` | ON on GCC/Clang |
| Libc/toolchain | `SDL_LIBICONV` | OFF (default) |
| Libc/toolchain | `SDL_CLOCK_GETTIME` | ON on Unix/Android |
| Assertions | `SDL_ASSERTIONS` | "auto" (release-mode dependent) |
| Installation | `SDL_INSTALL`, `SDL_INSTALL_CPACK`, `SDL_INSTALL_DOCS` | ON for top-level project |
| Misc | `SDL_RPATH` | ON on Unix |
| Misc | `SDL_RELOCATABLE` | OFF (default, ON for MSVC top-level) |
| Misc | `SDL_CCACHE` | OFF (default) |

**Immediate blueprint decisions** (set now, verified in M1):

| Option | Value | Rationale |
|--------|-------|-----------|
| `SDL_TEST_LIBRARY` | `OFF` | No SDL3 test library needed in GraphScope |
| `SDL_EXAMPLES` | `OFF` | No SDL3 examples needed |
| `SDL_STATIC` | `ON` | Prefer static linkage for deterministic GraphScore binary |
| `SDL_SHARED` | `OFF` | No shared SDL3 library needed |

The remaining unaudited options (SIMD, threading, libc, assertions,
installation, etc.) must be resolved in the M1 configure-cache pass
against the target platform matrix. Any option not explicitly decided
here continues at its upstream default.

##### Core Subsystem Options

| Option | Value | Category / Rationale |
|--------|-------|----------------------|
| `SDL_VIDEO` | `ON` | Required — windowing and input events |
| `SDL_AUDIO` | `OFF` | Audio device handled by miniaudio |
| `SDL_GPU` | `OFF` | Deferred to Phase C; no GPU needed for Phase A shell |
| `SDL_RENDER` | `OFF` | Deferred to Phase C |
| `SDL_CAMERA` | `OFF` | Not required |
| `SDL_JOYSTICK` | `OFF` | Not required for writer shell |
| `SDL_HAPTIC` | `OFF` | Not required |
| `SDL_HIDAPI` | `OFF` | Licensed BSD 3-clause (inventoried; permissive) but disabled — no consuming subsystem is enabled |
| `SDL_HIDAPI_LIBUSB` | `OFF` | Not required (also disabled implicitly when HIDAPI=OFF) |
| `SDL_POWER` | `OFF` | Not required |
| `SDL_SENSOR` | `OFF` | Not required |
| `SDL_DIALOG` | `OFF` | Not required for Phase A |
| `SDL_TRAY` | `OFF` | Not required for Phase A |
| `SDL_NOTIFICATION` | `OFF` | Not required |
| `SDL_VULKAN` | `OFF` | Deferred |
| `SDL_OPENGL` | `OFF` | Deferred |
| `SDL_METAL` | `ON` (macOS), `OFF` elsewhere | **Corrected in the M06 SDL_METAL fix round (see §A5).** Required ON on macOS at the pinned SHA: `SDL_RENDER=ON` alone compiles the Metal render driver, but the driver cannot create a renderer at runtime without `SDL_METAL=ON` (its Cocoa hook is guarded by `SDL_VIDEO_METAL`, set only when `SDL_METAL` is ON). `OFF` on Windows/Linux, where no Metal driver exists |
| `SDL_OPENGLES` | `OFF` | Default ON via `dep_option("SDL_VIDEO;NOT VISIONOS;NOT TVOS;NOT WATCHOS")`; disabled — OpenGL ES not required |
| `SDL_DUMMYVIDEO` | `OFF` | Default ON via `dep_option("SDL_VIDEO")`; disabled — dummy video driver not needed |
| `SDL_OFFSCREEN` | `OFF` | `set_option(ON)` — defaults ON unconditionally; disabled — offscreen rendering not needed for writer shell with real windowing |
| `SDL_SYSTEM_ICONV` | `OFF` | Default ON on Linux, OFF on Windows/macOS via `SDL_SYSTEM_ICONV_DEFAULT`; disabled everywhere for deterministic cross-platform build |
| `SDL_DLOPEN_NOTES` | `OFF` | Default ON on Unix via `dep_option(TRUE UNIX_SYS OFF)`; disabled — not needed for deterministic build |
| `SDL_XINPUT` | `OFF` | Default ON on Windows via `dep_option("WINDOWS OR CYGWIN")`; disabled — no joystick subsystem enabled |
| `SDL_OPENVR` | `OFF` | Already default OFF; not needed |

##### Platform Backends — Enabled (System SDKs, no vendored code)

| Option | Value | Default Source | Rationale |
|--------|-------|----------------|-----------|
| `SDL_COCOA` | `ON` (macOS) | `dep_option(ON "APPLE" OFF)` | Required for macOS windowing/input |
| `SDL_DIRECTX` | `OFF` | `dep_option(ON "SDL_AUDIO OR SDL_VIDEO;WINDOWS OR CYGWIN" OFF)`; default ON on Windows | Disabled — DirectX provides DirectSound (audio) and Direct3D (render) backends, neither of which is active (`SDL_AUDIO=OFF`, `SDL_RENDER=OFF`). Win32 windowing uses native `CreateWindow`, not DirectX. The `dep_option` guard is satisfied by `SDL_VIDEO=ON` but no consuming subsystem requires it |
| `SDL_X11` | `ON` (Linux) | `dep_option(${UNIX_SYS} "SDL_VIDEO" OFF)` | Required for X11/XWayland windowing |
| `SDL_WAYLAND` | `ON` (Linux) | `dep_option(${UNIX_SYS} "SDL_VIDEO" OFF)` | Required for Wayland windowing |
| `SDL_KMSDRM` | `OFF` | `dep_option(${UNIX_SYS} "SDL_VIDEO" OFF)`; default ON on Linux | Disabled — KMS/DRM is a raw framebuffer video driver requiring EGL, libdrm, and GBM. `SDL_OPENGL` and `SDL_OPENGLES` are both OFF, so no GL/EGL context is available. Wayland and X11 cover all required windowing paths |

##### Dynamic Loading — Enabled (Standard SDL Behavior)

| Option | Value | Default Source | Rationale |
|--------|-------|----------------|-----------|
| `SDL_X11_SHARED` | `ON` (Linux+X11) | `dep_option(ON "SDL_X11;SDL_DEPS_SHARED" OFF)` | Standard SDL dynamic X11 loading |
| `SDL_WAYLAND_SHARED` | `ON` (Linux+Wayland) | `dep_option(ON "SDL_WAYLAND;SDL_DEPS_SHARED" OFF)` | Standard SDL dynamic Wayland loading |
| `SDL_KMSDRM_SHARED` | implicit OFF | Inherits from `SDL_KMSDRM=OFF` | — |

##### X11 Extensions — Explicitly Decided

| Option | Value | Default Source | Rationale |
|--------|-------|----------------|-----------|
| `SDL_X11_XCURSOR` | `ON` (Linux+X11) | `dep_option(ON SDL_X11 OFF)` | Cursor support; required for windowing |
| `SDL_X11_XDBE` | `ON` (Linux+X11) | `dep_option(ON SDL_X11 OFF)` | Double-buffer extension; required for flicker-free rendering |
| `SDL_X11_XINPUT` | `ON` (Linux+X11) | `dep_option(ON SDL_X11 OFF)` | Extended input device support (XInput2) |
| `SDL_X11_XFIXES` | `ON` (Linux+X11) | `dep_option(ON SDL_X11 OFF)` | Region/cursor fixes extension |
| `SDL_X11_XRANDR` | `ON` (Linux+X11) | `dep_option("${SDL_X11_XRANDR_DEFAULT}" SDL_X11 OFF)`; default ON (OFF on Solaris) | Display/monitor management |
| `SDL_X11_XSCRNSAVER` | `OFF` | `dep_option(ON SDL_X11 OFF)`; default ON | Screensaver control not needed for writer shell |
| `SDL_X11_XSHAPE` | `OFF` | `dep_option(ON SDL_X11 OFF)`; default ON | Shaped windows not needed |
| `SDL_X11_XSYNC` | `OFF` | `dep_option(ON SDL_X11 OFF)`; default ON | Sync extension not needed |
| `SDL_X11_XTEST` | `OFF` | `dep_option(ON SDL_X11 OFF)`; default ON | Test extension not needed |

##### Linux Audio/Integration — All Disabled (Unreviewed or Unnecessary)

| Option | Value | Default Source | Rationale |
|--------|-------|----------------|-----------|
| `SDL_ALSA` | `OFF` | `dep_option(${UNIX_SYS} "SDL_AUDIO" OFF)`; default ON on Linux | Audio handled by miniaudio; SDL_AUDIO already OFF |
| `SDL_PULSEAUDIO` | `OFF` | `dep_option(${UNIX_SYS} "SDL_AUDIO" OFF)`; default ON on Linux | Same rationale |
| `SDL_JACK` | `OFF` | `dep_option(${UNIX_SYS} "SDL_AUDIO" OFF)`; default ON on Linux | Same rationale |
| `SDL_SNDIO` | `OFF` | `dep_option(${UNIX_SYS} "SDL_AUDIO" OFF)`; default ON on Linux | Same rationale |
| `SDL_OSS` | `OFF` | Platform-dependent default; `dep_option` with `UNIX_SYS OR RISCOS;SDL_AUDIO` guard | Same rationale |
| `SDL_PIPEWIRE` | `OFF` | `set_option(${UNIX_SYS})`; **defaults ON on Linux without SDL_AUDIO dependency guard** — must be explicitly forced OFF (see residual risk below) | Audio handled by miniaudio; SDL_PIPEWIRE must be explicitly OFF because it has no SDL_AUDIO guard |
| `SDL_PIPEWIRE_SHARED` | `OFF` | `dep_option(ON "SDL_PIPEWIRE;SDL_DEPS_SHARED" OFF)`; inherits from SDL_PIPEWIRE=OFF | Implicitly OFF |
| `SDL_ALSA_SHARED` | implicit OFF | Inherits from SDL_ALSA=OFF | — |
| `SDL_JACK_SHARED` | implicit OFF | Inherits from SDL_JACK=OFF | — |
| `SDL_PULSEAUDIO_SHARED` | implicit OFF | Inherits from SDL_PULSEAUDIO=OFF | — |
| `SDL_SNDIO_SHARED` | implicit OFF | Inherits from SDL_SNDIO=OFF | — |
| `SDL_FRIBIDI` | `OFF` | `dep_option(ON SDL_X11 OFF)`; default ON with X11 | Unreviewed (X11 text shaping); not needed |
| `SDL_FRIBIDI_SHARED` | implicit OFF | Inherits from SDL_FRIBIDI=OFF | — |
| `SDL_LIBTHAI` | `OFF` | `dep_option(ON SDL_X11 OFF)`; default ON with X11 | Unreviewed (X11 Thai support); not needed |
| `SDL_LIBTHAI_SHARED` | implicit OFF | Inherits from SDL_LIBTHAI=OFF | — |
| `SDL_DBUS` | `OFF` | `dep_option(ON "${UNIX_SYS}" OFF)`; default ON on Linux | Not needed; adds unreviewed system integration |
| `SDL_IBUS` | `OFF` | `dep_option(ON "${UNIX_SYS}" OFF)`; default ON on Linux | Not needed; adds unreviewed input method |
| `SDL_LIBURING` | `OFF` | `dep_option(ON "${UNIX_SYS}" OFF)`; default ON on Linux | Not needed |
| `SDL_LIBUDEV` | `OFF` | `dep_option(ON ${UNIX_SYS} OFF)`; default ON on Linux | Not needed |
| `SDL_RPI` | `OFF` | `dep_option(ON "SDL_VIDEO;UNIX_SYS;SDL_CPU_ARM32 OR SDL_CPU_ARM64" OFF)`; auto-enables on Linux arm64/arm32 | Unreviewed; not needed |
| `SDL_ROCKCHIP` | `OFF` | `dep_option(ON "SDL_VIDEO;UNIX_SYS;SDL_CPU_ARM32 OR SDL_CPU_ARM64" OFF)`; auto-enables on Linux arm64/arm32 | Unreviewed; not needed |
| `SDL_VIVANTE` | `OFF` | `dep_option(ON "${UNIX_SYS};SDL_CPU_ARM32" OFF)`; auto-enables on ARM32 Linux | Unreviewed ARM-only video driver; not needed |
| `SDL_WAYLAND_LIBDECOR` | `OFF` | `dep_option(ON "SDL_WAYLAND" OFF)`; default ON with Wayland | Unreviewed; dynamically loads external libdecor (MIT) for Wayland client-side decorations |
| `SDL_WAYLAND_LIBDECOR_SHARED` | implicit OFF | Inherits from `SDL_WAYLAND_LIBDECOR=OFF` | — |

##### Vendored Component — HIDAPI License Inventory

`src/hidapi/` at the pinned SHA is tri-licensed under GPLv3, BSD
3-clause (`src/hidapi/LICENSE-bsd.txt`), and the original HIDAPI
license. GraphScore selects BSD 3-clause. All three license files
are committed in the SDL3 source tree. The HIDAPI subsystem is
disabled in the default GraphScore build because no enabled subsystem
consumes it. A future milestone that re-enables HIDAPI must record the
BSD 3-clause notice in `docs/NOTICES.md`.

No other vendored libraries (no `src/3rdparty/` directory) exist at
this SHA. The `cmake/3rdparty.cmake` file is a CMake helper script, not
a vendored source directory.

##### Residual Risk — SDL_PIPEWIRE No SDL_AUDIO Guard

`SDL_PIPEWIRE` is set via `set_option(${UNIX_SYS})` at line ~480 of
CMakeLists.txt — a bare `set_option()` without an `SDL_AUDIO` dependency
guard. On Linux this option defaults to ON regardless of whether
`SDL_AUDIO` is ON or OFF. The adapter blueprint explicitly sets it to OFF,
but this must be confirmed via a configure-cache evidence report in M1.

##### M1 Verification Gate — SATISFIED

`cmake/SDL3.cmake` implements the adapter. The gate is enforced on every
configure rather than by a snapshot taken once: after
`FetchContent_MakeAvailable`, the adapter reads every option in the reviewed
tables back out of the cache and fails configure on any mismatch, writing the
full evidence to `<build>/sdl3_option_evidence.txt`. `SDL_PIPEWIRE=OFF` is
covered by that pass on Linux like every other option, so the residual risk
recorded above cannot regress silently.

The check also requires each declared option name to appear in SDL's own
`CMakeLists.txt` at the pinned SHA. Without this, an option renamed upstream —
or misspelled here — would be created in the cache by the adapter itself,
read back unchanged, and "verified" while having no effect on the build. The
check found exactly that case during M1 implementation.

##### Upstream Defect at the Pinned SHA — macOS Link Failure

At `08b9c55393be5cb08fbec12ca431470faba3c8c9`, SDL's Cocoa video-driver
source glob compiles `src/video/cocoa/SDL_cocoanotification.m`
unconditionally, and `SDL_cocoamouse.m` references GameController's `GCMouse`
unconditionally — but the corresponding `-framework` link dependencies are
added only when `SDL_NOTIFICATION` and `SDL_JOYSTICK` are ON. Under the
reviewed option set both are OFF, so those objects are present in
`libSDL3.a` with no framework to resolve against, and the writer fails to
link on macOS.

`cmake/SDL3.cmake` links `UserNotifications`, `Security`, and
`GameController` on Apple platforms to resolve them. This changes nothing
about which SDL code is compiled or which licences are engaged — the objects
are in the archive either way — and it is narrower than the alternative of
turning the two subsystems ON, which would enable code paths this section
deliberately excluded. These are OS frameworks, which are system-provided
services rather than vendored dependencies (ADR 0003 §2.2). The workaround is
scoped to this pin and is to be removed when the pin moves to a SHA where
those sources are conditional.

SDL3 remains PROVISIONAL pending the Phase C rendering/GPU decisions; the M1
build gate itself is satisfied.

---

### 2. AccessKit — Accessibility Bridge

| Property | Value |
|----------|-------|
| Repository | https://github.com/AccessKit/accesskit-c |
| Pinned commit SHA | `826d672661f9453c8b269ab3946dbcbae6300555` (release 0.22.3) |
| License | MIT OR Apache-2.0 (user's choice) |
| License URLs at SHA | https://raw.githubusercontent.com/AccessKit/accesskit-c/826d672661f9453c8b269ab3946dbcbae6300555/LICENSE-APACHE, https://raw.githubusercontent.com/AccessKit/accesskit-c/826d672661f9453c8b269ab3946dbcbae6300555/LICENSE-MIT |
| Committed licenses | `docs/licenses/AccessKit-LICENSE-MIT.txt`, `docs/licenses/AccessKit-LICENSE-APACHE.txt`, `docs/licenses/AccessKit-LICENSE-chromium.txt` |
| License SPDX | MIT OR Apache-2.0 |
| Patent grant | Via Apache-2.0 option |
| Notices | Copyright The AccessKit contributors. Chromium-derived code carries a BSD-style notice (`docs/licenses/AccessKit-LICENSE-chromium.txt`). |

**Corrosion mutable fetch — detailed analysis**:

The accesskit-c CMakeLists.txt at the pinned SHA (line 26) contains:

```cmake
FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
  GIT_TAG v0.6.1
)
```

This `GIT_TAG v0.6.1` is a **mutable git tag**. The actual immutable
SHA at this tag is `1499b14e4906a2890f5cee1547c8848db261753d` (verified
via GitHub API `refs/tags/v0.6.1`, 2026-07-18). Corrosion is
MIT-licensed (`docs/licenses/Corrosion-LICENSE.txt`). Any downstream
consumer would inherit a floating Corrosion reference — a corrective
patch must pin the exact SHA.

**Cargo.lock and offline/transitive audit**: 80+ transitive Rust crates.
All direct AccessKit crates are MIT OR Apache-2.0. Initial review of
transitives identifies no GPL/LGPL crates. Formal `cargo vendor` +
`cargo-deny` audit not performed.

**LGPL material**: Meson build system files are LGPL-2.1 — applies only
to build-system files, not the library binary.

**Blockers**:

1. Mutable Corrosion git tag (`v0.6.1` → SHA `1499b14e4906a2890f5cee1547c8848db261753d`).
2. Rust toolchain requirement.
3. Cargo.lock transitive audit incomplete.
4. No `FetchContent`-ready CMake target.

| Build tools | Rust toolchain required. CMake 3.20+ for C binding wrapper. |
| FetchContent ready | **No.** Requires adapter plus mutable-fetch patch. |
| Platform support | macOS, Windows, Linux (at-spi2), Android, iOS |
| Warning isolation | C API compiled from Rust; C headers are clean. |

**Decision**: **EXCLUDED** from the default build. AccessKit remains
PROVISIONAL pending a Phase C spike that resolves all four blockers.
Fallback: platform-native accessibility APIs.

---

### 3. Backend-Neutral Vector Renderer

#### 3a. ThorVG — Primary Candidate

| Property | Value |
|----------|-------|
| Repository | https://github.com/thorvg/thorvg |
| Pinned commit SHA | `6d5933c9d1aca94635c6ad8129f3530ae554d423` (2026-07-18) |
| License | MIT |
| License URL at SHA | https://raw.githubusercontent.com/thorvg/thorvg/6d5933c9d1aca94635c6ad8129f3530ae554d423/LICENSE |
| Committed license | `docs/licenses/ThorVG-LICENSE.txt` |
| License SPDX | MIT |
| Patent grant | None |
| Transitive dependencies | None mandatory. Core is self-contained C++14. Optional loaders (FreeType, libpng, libjpeg-turbo, libwebp) not enabled. |
| Build tools | **Meson is the primary build system.** No CMakeLists.txt provided at the pinned SHA. |
| Platform support | macOS, Windows, Linux, iOS, Android, Emscripten, ESP32. Verified in upstream CI. |
| Architecture support | x86-64, arm64. SIMD opt-in. Pure C++ fallback. |

**Acceptance blockers**:

1. **Meson and Ninja toolchain**: Their exact immutable revisions,
   licenses, and offline-acquisition procedure are not yet specified.
   This is a blocking requirement — an ADR or ADR amendment must pin
   Meson and Ninja to exact SHAs/tags and record their licenses before
   ThorVG can be accepted.
2. **CMake adapter**: A `cmake/ThorVG.cmake` adapter must be designed
   and tested in the Phase C spike.
3. **Empirical validation**: Enabled features, binary size, and platform
   compatibility must be validated in the spike.

**Decision**: PROVISIONAL, **excluded from the selected default
closure**. ThorVG is the primary vector renderer candidate and remains
under evaluation. The owned toolkit-neutral vector render-list/
tessellation fallback (Section 3b) is explicitly adopted as the
Phase C fallback if ThorVG cannot meet the Meson/Ninja toolchain
requirements.

#### 3b. Owned Render-List/Tessellation Fallback — Explicitly Adopted

A GraphScore-owned toolkit-neutral vector render list and tessellation
layer that:

- Accepts a retained list of filled/stroked path commands, glyph
  positions, and clip regions.
- Decomposes paths into triangle meshes via an owned tessellator.
- Emits vertex/index buffers to SDL3's GPU API or a CPU rasterizer.

This fallback is explicitly adopted pending Phase C. It eliminates a
third-party renderer dependency entirely.

---

### 4. HarfBuzz — Text Shaping

| Property | Value |
|----------|-------|
| Repository | https://github.com/harfbuzz/harfbuzz |
| Pinned commit SHA | `af192b7e0f49a9965220ba3f18473e3f8e28b8b9` |
| License | Old MIT |
| License URL at SHA | https://raw.githubusercontent.com/harfbuzz/harfbuzz/af192b7e0f49a9965220ba3f18473e3f8e28b8b9/COPYING |
| Committed license | `docs/licenses/HarfBuzz-COPYING.txt` |
| License SPDX | MIT |
| Patent grant | None |
| Transitive dependencies | None mandatory. All optional deps disabled. |

**Constrained CMake options** (verified at the pinned SHA CMakeLists.txt):

| Option | Value | Rationale |
|--------|-------|-----------|
| `HB_HAVE_FREETYPE` | `OFF` | FreeType also has HarfBuzz OFF — avoid circular dependency |
| `HB_HAVE_CORETEXT` | `OFF` | Verified exists: `option(HB_HAVE_CORETEXT "Enable CoreText shaper backend on macOS" ON)` at `APPLE` guard. Set OFF for deterministic cross-platform build; no platform shaping. |
| `HB_BUILD_SUBSET` | `OFF` | Not needed |
| `HB_BUILD_RASTER` | `OFF` | Not needed |
| `HB_BUILD_VECTOR` | `OFF` | Not needed |
| `HB_BUILD_GPU` | `OFF` | Not needed |
| `HB_BUILD_UTILS` | `OFF` | Not needed |
| `HB_HAVE_GLIB` | `OFF` | Prevents LGPL GLib |
| `HB_HAVE_ICU` | `OFF` | Prevents ICU (Unicode License) |
| `HB_HAVE_CAIRO` | `OFF` | Prevents LGPL/MPL Cairo |
| `HB_HAVE_GRAPHITE2` | `OFF` | Prevents LGPL Graphite2 |
| `HB_HAVE_GOBJECT` | `OFF` | Prevents GObject |
| `HB_HAVE_UNISCRIBE` | `OFF` | Not needed |
| `HB_HAVE_GDI` | `OFF` | Not needed |
| `HB_HAVE_DIRECTWRITE` | `OFF` | Not needed |

| Build tools | Meson (official). CMake community-maintained (3.14+). CMakeLists.txt warns: "The main build system for HarfBuzz is Meson. CMake build support is community-maintained." |
| FetchContent ready | Requires adapter. Thin `cmake/HarfBuzz.cmake` shim sets options, calls `add_subdirectory`. No fork needed. |
| Fallback | None. HarfBuzz has no plausible permissive alternative. |

**Decision**: **PROVISIONAL**. HarfBuzz core library is license-cleared
(Old MIT) with all optional deps disabled. However, HarfBuzz cannot be
accepted as POLICY-CLEARED because it requires a CMake adapter that has
not yet been implemented or verified.

**Forced-FreeType risk — enforceable adapter invariant** (UNEXECUTED M1
requirement):

HarfBuzz CMakeLists.txt at pinned SHA `af192b7e...` contains a
non-overridable guard (lines ~27-30 and ~165-167, verified via source
inspection at the pinned SHA on 2026-07-19):

```cmake
option(HB_HAVE_FREETYPE "Enable freetype interop helpers" OFF)
...
if (TARGET freetype)
  set (HB_HAVE_FREETYPE ON)
  add_compile_definitions(HAVE_FREETYPE=1)
endif ()
...
if (HB_HAVE_FREETYPE AND TARGET freetype)
  target_link_libraries(harfbuzz freetype)
endif ()
```

Even when `HB_HAVE_FREETYPE=OFF` is passed, the `if (TARGET freetype)`
guard silently flips it back ON and links the `freetype` target. The
`option()` line is advisory only; the guard below it is unconditional.
This creates a risk that if FreeType's CMake target is defined before
HarfBuzz is configured (e.g., due to CMake `add_subdirectory` ordering),
HarfBuzz will silently link to FreeType, violating the intended isolated
closure.

**Enforceable invariant** (to be implemented by `cmake/HarfBuzz.cmake`
in M1):

1. Configure and `add_subdirectory` HarfBuzz **before** any `freetype`
   CMake target exists. This can be done by ordering in
   `cmake/dependencies.cmake`, or by configuring HarfBuzz in an
   isolated `ExternalProject` scope.
2. After HarfBuzz is configured, verify with CMake introspection:
   ```cmake
   get_target_property(_hb_link harfbuzz LINK_LIBRARIES)
   if("freetype" IN_LIST _hb_link)
     message(FATAL_ERROR "HarfBuzz unexpectedly linked to FreeType")
   endif()
   ```
3. Then configure FreeType with `FT_DISABLE_HARFBUZZ=ON` — FreeType's
   HarfBuzz auto-hinting integration is disabled, and FreeType's own
   CMakeLists.txt has no equivalent forced-link guard, so the
   circularity is broken in both directions.

**M1 verification gate**: The `cmake/HarfBuzz.cmake` adapter must be
implemented and a configure-cache evidence report must prove that
`harfbuzz` target has no `freetype` in its `LINK_LIBRARIES`. Until this
verification is completed, HarfBuzz's isolated closure remains
theoretical (source inspection only).

---

### 5. FreeType — Font Loading and Glyph Rasterization

| Property | Value |
|----------|-------|
| Repository | https://github.com/freetype/freetype |
| Pinned commit SHA | `f01dec5e676847267834b881b25f6e8c79581163` |
| License | FTL (FreeType License) or GPLv2 (user's choice). GraphScore selects FTL. |
| License URL at SHA | https://raw.githubusercontent.com/freetype/freetype/f01dec5e676847267834b881b25f6e8c79581163/docs/FTL.TXT |
| Committed license | `docs/licenses/FreeType-FTL.TXT` |
| License SPDX | FTL |
| Patent grant | FreeType Patents Grant (https://freetype.org/patents.html): FreeType-related patents held by FreeType developers are freely licensed. No general grant from all contributors. |

**Constrained CMake options — deterministic closure** (verified at
pinned SHA CMakeLists.txt):

| Option | Value | Rationale |
|--------|-------|-----------|
| `FT_DISABLE_ZLIB` | `ON` | Use internal zlib; no system dep |
| `FT_DISABLE_BZIP2` | `ON` | No BZip2 support |
| `FT_DISABLE_PNG` | `ON` | No PNG-embedded bitmap support |
| `FT_DISABLE_HARFBUZZ` | `ON` | HarfBuzz auto-hinting disabled — HarfBuzz also has FreeType OFF; avoids circular dependency |
| `FT_DISABLE_BROTLI` | `ON` | No WOFF2 support |
| `FT_DISABLE_HVF` | `ON` | Apple-only; disable for portability |
| All `FT_REQUIRE_*` | `OFF` | No hard requirements on system libraries |

This produces a self-contained FreeType with zero optional system
dependencies. `FT_DISABLE_ZLIB=ON` enables FreeType's bundled zlib 1.3.1
(internal, zlib license, `docs/licenses/FreeType-zlib-license.txt`,
extracted from `src/gzip/zlib.h` at the pinned SHA). M1 may loosen
constraints per-platform with documented rationale. The HarfBuzz
relationship is intentionally severed in both directions to avoid any
circular dependency.

**Mandatory binary-distribution disclaimer** (FTL §2):

> This software is based in part of the work of the FreeType Team.

This text must appear in documentation accompanying any binary
distribution that includes FreeType. See `docs/licenses/FreeType-FTL.TXT`
for the full license text.

| Build tools | CMake (CMakeLists.txt provided since FreeType 2.10+). C compiler. |
| FetchContent ready | Yes. Supports `add_subdirectory`, exports `freetype` target. |
| Warning isolation | Separate CMake target, SYSTEM includes. |

**Decision**: **POLICY-CLEARED** under FTL. FreeType requires no CMake
adapter — it supports `add_subdirectory` directly. The constrained
closure removes all optional system dependencies and the HarfBuzz
integration point. FreeType credits must appear in binary distribution
documentation. The HarfBuzz isolation invariant (Section 4) ensures that
HarfBuzz does not silently link to FreeType; FreeType's own options
handle the reverse direction. No empirical build verification has been
performed; FreeType's CMake integration is straightforward and does not
require the same M1 adapter gate as HarfBuzz or SDL3.

---

### 6. Audio-Device Library

**Wayland/X11 N/A**: Audio and MIDI device libraries operate below the
windowing system. Selection is independent of whether the writer uses
Wayland or X11/XWayland.

#### 6a. miniaudio — Primary Selection

| Property | Value |
|----------|-------|
| Repository | https://github.com/mackron/miniaudio |
| Pinned commit SHA | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` (v0.11.25, 2026-03-04) |
| License | Public Domain (Unlicense) OR MIT-0. GraphScore selects Unlicense. |
| License file at SHA | https://raw.githubusercontent.com/mackron/miniaudio/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d/LICENSE |
| Committed license | `docs/licenses/miniaudio-LICENSE.txt` |
| License SPDX | Unlicense OR MIT-0 |
| Patent grant | None |
| Transitive dependencies | None. Single-header library (C source in `miniaudio.h`). |
| FetchContent ready | Requires adapter. A `cmake/miniaudio.cmake` file must create an `INTERFACE` or `STATIC` target with the header-only library. |
| Warning isolation | Separate translation unit compiling `miniaudio.h` behind a `-Wno-*` flag set (UNEXECUTED — M1 requirement). The implementation translation unit that `#include`s `miniaudio.h` and defines `MINIAUDIO_IMPLEMENTATION` must be isolated from GraphScore warning flags. |

**Decision**: **PROVISIONAL**. miniaudio is license-cleared (Unlicense,
no mandatory attribution) with zero vendored dependencies, but:
1. No empirical build or audio-device test has been performed.
2. The CMake adapter and warning-isolation translation unit are
   UNEXECUTED M1 requirements.
3. Device enumeration, low-latency callback stability, and sample-rate
   conversion quality must be tested on each target with real hardware
   in the Phase C spike.

miniaudio will be promoted to POLICY-CLEARED after M1 produces a
configure-cache evidence report and the adapter successfully compiles on
all target platforms. Full audio-device validation is a Phase C gate
(real hardware).

**Per-platform adapter blueprint** (UNEXECUTED — specification for M1
`cmake/miniaudio.cmake`):

The adapter must use `MA_ENABLE_ONLY_SPECIFIC_BACKENDS` to select only
the intended backends per platform, rather than relying on miniaudio's
default backend priority order. This prevents unreviewed backends from
being compiled into the binary.

| Target OS | Architecture | Intended Backend(s) | OS Frameworks / System Libraries |
|-----------|-------------|---------------------|----------------------------------|
| macOS 14+ | arm64 | Core Audio (CoreAudio) | `-framework CoreFoundation -framework CoreAudio -framework AudioToolbox` |
| macOS 14+ | x86-64 | Core Audio (CoreAudio) | Same as arm64 |
| Windows 11 | x86-64 | WASAPI | `mmdevapi.lib` (system SDK) |
| Windows 11 | arm64 | WASAPI | `mmdevapi.lib` (system SDK) — build-only; no arm64 device test planned |
| Ubuntu 24.04 | x86-64 | ALSA, PulseAudio | `libasound`, `libpulse`, `libpulse-simple`, `-lpthread -lm -ldl` |
| Ubuntu 24.04 | arm64 | ALSA, PulseAudio | Same as x86-64 — build-only; no arm64 device test planned |

**Compile-time backend flags** (`#define` options passed before
`#include "miniaudio.h"`, §2.7 of `miniaudio.h` at pinned SHA):

With `MA_ENABLE_ONLY_SPECIFIC_BACKENDS=1`, every backend is disabled by
default. The adapter explicitly defines `MA_ENABLE_*` for only the
intended backends per platform. All other backends are not compiled in.

| Per-Platform Compile-Time Defines | macOS | Windows | Linux |
|-----------------------------------|-------|---------|-------|
| `MA_ENABLE_ONLY_SPECIFIC_BACKENDS` | `1` | `1` | `1` |
| `MA_ENABLE_COREAUDIO` | `1` | — | — |
| `MA_ENABLE_WASAPI` | — | `1` | — |
| `MA_ENABLE_ALSA` | — | — | `1` |
| `MA_ENABLE_PULSEAUDIO` | — | — | `1` |

Additional feature flags (all platforms):

| Flag | Adapter Value | Rationale |
|------|--------------|-----------|
| `MA_NO_RUNTIME_LINKING` | `1` on macOS; undefined elsewhere | Required for Apple notarization (static framework linking). On Linux/Windows, default runtime loading is used so that missing audio servers don't prevent startup |
| `MA_NO_DEVICE_IO` | Undefined | Audio I/O required for writer playback |
| `MA_NO_DECODING` | `1` | Not needed for real-time audio device output |
| `MA_NO_ENCODING` | `1` | Not needed |
| `MA_NO_RESOURCE_MANAGER` | `1` | Not using high-level resource manager API |
| `MA_NO_GENERATION` | `1` | Not using waveform generation |
| `MA_NO_SSE2` / `MA_NO_AVX2` / `MA_NO_NEON` | Platform-appropriate | Auto-detect per compiler target; graceful fallback per §2.7 |

**M1 implementation translation unit**: A single `.c` file (e.g.,
`src/audio/ma_impl.c`) defines `MINIAUDIO_IMPLEMENTATION` and all
per-platform `#define` flags, then `#include`s `miniaudio.h`. This
one translation unit is compiled with isolated warning flags
(`-Wno-*` or `/wd*`) and linked into the writer audio library.
All other translation units include `miniaudio.h` without
`MINIAUDIO_IMPLEMENTATION` to use the API types.

**Runtime backend selection**: The adapter must pass an explicit ordered
`ma_backend[]` array to `ma_context_init` so that runtime priority and
fallback are deterministic regardless of platform defaults:

| Platform | Runtime `ma_backend[]` (priority order) |
|----------|------------------------------------------|
| macOS | `{ma_backend_coreaudio}` |
| Windows | `{ma_backend_wasapi}` |
| Linux | `{ma_backend_alsa, ma_backend_pulseaudio}` |

**Direct-link vs. runtime-loading per platform**:

| Platform | Linking Model | System Library Requirements |
|----------|--------------|----------------------------|
| macOS | Direct-link (`MA_NO_RUNTIME_LINKING=1`) | `-framework CoreFoundation -framework CoreAudio -framework AudioToolbox` required on link line |
| Windows | Runtime-load (default) | WASAPI calls resolve through `mmdevapi.dll` at runtime. No additional link flags needed beyond the standard Windows SDK. UWP direct-link is out of current scope |
| Linux | Runtime-load (default) | ALSA and PulseAudio loaded via `dlopen` at runtime. Build host needs `libasound-dev` and `libpulse-dev` for headers only; no link-time dependency. If ALSA or PulseAudio is absent at runtime, miniaudio skips that backend and falls to the next in the array |

**Pending empirical validation** (M1 build gate + Phase C device gate):
- **M1 build gate**: Source compilation on all six targets with warning
  isolation applied. No device present.
- **Phase C device gate**: Audio device enumeration, low-latency callback
  stability, sample-rate conversion quality on real hardware.

#### 6b. PortAudio — Fallback

| Property | Value |
|----------|-------|
| Repository | https://github.com/PortAudio/portaudio |
| Pinned commit SHA | `f88b5841575b43bfa024a6861635b69d7eb98d6d` |
| License | MIT |
| License file at SHA | https://raw.githubusercontent.com/PortAudio/portaudio/f88b5841575b43bfa024a6861635b69d7eb98d6d/LICENSE.txt |
| Committed license | `docs/licenses/PortAudio-LICENSE.txt` |
| Transitive dependencies | ASIO SDK **must be disabled** (`PA_USE_ASIO=OFF`). ASIO is under a proprietary Steinberg license and is not permissively redistributable. All other backends use OS system APIs only. |
| FetchContent ready | Yes. CMakeLists.txt provided; exports `portaudio` target. |
| Warning isolation | Separate CMake target, SYSTEM includes. |

**Backend architecture evidence matrix** (from source inspection at
pinned SHA CMakeLists.txt):

| Target OS | Architecture | Status | Native Backend(s) | System Deps / Notes |
|-----------|-------------|--------|-------------------|---------------------|
| macOS 14+ | arm64 | PROVISIONAL | Core Audio (always active; no option to disable) | System frameworks (CoreAudio, AudioToolbox, AudioUnit, CoreFoundation, CoreServices). JACK must be explicitly OFF. |
| macOS 14+ | x86-64 | PROVISIONAL | Core Audio | Same as arm64. |
| Windows 11 | x86-64 | PROVISIONAL | MME, DirectSound, WASAPI, WDM-KS | System APIs (`winmm`, `dsound`, `ole32`, `uuid`, `setupapi`). ASIO disabled. |
| Windows 11 | arm64 | PROVISIONAL (build-only) | MME, DirectSound, WASAPI, WDM-KS | Same as x86-64. |
| Ubuntu 24.04 | x86-64 | PROVISIONAL | ALSA | System library `libasound`. `-lm -lpthread`. PulseAudio and JACK are OFF in the default Linux specification (future opt-in behind explicit GraphScore options). |
| Ubuntu 24.04 | arm64 | PROVISIONAL (build-only) | ALSA | Same as x86-64. |

**Deterministic backend flags** (PortAudio CMake options at pinned SHA
`f88b5841575b43bfa024a6861635b69d7eb98d6d`). All `cmake_dependent_option(... ON ... OFF)` entries are
availability-dependent — they default ON only if the corresponding
system package is found; otherwise they default OFF silently. The M1
adapter blueprint below makes this deterministic:

| Flag | GraphScore Value | Pinned Default | Rationale |
|------|-----------------|----------------|-----------|
| `PA_USE_ASIO` | `OFF` (all platforms) | `OFF` | Proprietary Steinberg license; excluded from permissive closure |
| `PA_USE_WASAPI` | `ON` (Windows) | `ON` | Windows WASAPI audio |
| `PA_USE_WDMKS` | `ON` (Windows) | `ON` | Windows WDM Kernel Streaming |
| `PA_USE_WDMKS_DEVICE_INFO` | `ON` (Windows) | `ON` | Use WDM/KS API for device information enumeration |
| `PA_USE_DS` | `ON` (Windows) | `ON` | Windows DirectSound fallback |
| `PA_USE_WMME` | `ON` (Windows) | `ON` | Windows MME legacy fallback |
| `PA_USE_ALSA` | `ON` (Linux, required) | `ON if ALSA_FOUND` | Linux ALSA audio — the only backend enabled in the default Linux specification. `cmake_dependent_option(ON ALSA_FOUND OFF)`. M1 adapter must `find_package(ALSA REQUIRED)` before `add_subdirectory` and assert resolved ON |
| `PA_USE_PULSEAUDIO` | `OFF` (all platforms) | `ON if PulseAudio_FOUND` | Future opt-in behind explicit GraphScore option `GRAPHSCORE_PORTAUDIO_PULSEAUDIO`. When enabled, same `find_package` + assert pattern applies |
| `PA_USE_JACK` | `OFF` (all platforms) | `ON if JACK_FOUND` (any platform) | Future opt-in behind explicit GraphScore option `GRAPHSCORE_PORTAUDIO_JACK`. When enabled on Linux, same `find_package` + assert pattern applies. Must remain explicitly OFF on macOS regardless of option (Homebrew JACK detection risk) |
| `PA_USE_OSS` | `OFF` (all platforms) | `OFF` | Intentionally off by default upstream; Linux OSS devices rare |
| `PA_USE_SNDIO` | `ON` (BSD), `OFF` (Linux/macOS/Windows) | `ON if SNDIO_FOUND` | `cmake_dependent_option(ON SNDIO_FOUND OFF)`. Only active on BSD where sndio is the native audio system |
| `PA_ALSA_DYNAMIC` | `OFF` (Linux) | `OFF` | No dynamic ALSA loading |
| `PA_BUILD_TESTS` | `OFF` | `OFF` | No test programs in default build |
| `PA_BUILD_EXAMPLES` | `OFF` | `OFF` | No example programs in default build |
| `PA_WARNINGS_ARE_ERRORS` | `OFF` | `OFF` | Default; may be reconsidered in M1 |

**macOS JACK prevention**: PortAudio's `cmake_dependent_option(PA_USE_JACK ON JACK_FOUND OFF)`
(line ~102 of CMakeLists.txt at pinned SHA) checks `JACK_FOUND` which is
set by `find_package(JACK)` without an OS guard. If JACK development
libraries are installed on macOS (e.g., via `brew install jack`),
`PA_USE_JACK` defaults to ON. The GraphScore adapter must explicitly
set `PA_USE_JACK=OFF` on macOS via a platform condition.

**macOS Core Audio**: On macOS, CoreAudio is unconditionally enabled
(there is no CMake option to disable it; the `if(APPLE)` block at line
~148 of CMakeLists.txt unconditionally compiles the CoreAudio host API
sources). This is acceptable — CoreAudio is a system framework.

**M1 availability-dependency blueprint**: The default Linux specification
requires only ALSA. The `cmake/PortAudio.cmake` adapter must:

1. **Pre-configure**: Call `find_package(ALSA REQUIRED)` before
   PortAudio's `add_subdirectory`. This fails the build at CMake time
   if `libasound-dev` is absent, rather than silently disabling ALSA.
2. **Post-configure assertion**: After PortAudio is configured, read the
   resolved cache values and `FATAL_ERROR` if ALSA resolved to OFF:
   ```cmake
   if(NOT PA_USE_ALSA)
     message(FATAL_ERROR "PA_USE_ALSA resolved OFF — libasound-dev missing")
   endif()
   ```
3. **Future opt-in backends**: PulseAudio and JACK are explicitly OFF
   in the default specification. They may be enabled behind explicit
   GraphScore CMake options (`GRAPHSCORE_PORTAUDIO_PULSEAUDIO`,
   `GRAPHSCORE_PORTAUDIO_JACK`) in a future milestone. When enabled,
   the same `find_package` + assert pattern applies, and the JACK
   dependency must remain OFF on macOS regardless.

**Decision**: **PROVISIONAL** fallback. Activated only if miniaudio
proves unsuitable for latency, stability, or device-enumeration
requirements. All backends use OS system APIs only; ASIO is explicitly
excluded. JACK must be explicitly OFF on macOS. No empirical
audio-device tests have been run at the pinned SHA.

---

### 7. MIDI Utility Library

**Wayland/X11 N/A**: MIDI device libraries operate directly on the
ALSA sequencer (Linux), CoreMIDI (macOS), or WinMM (Windows). No
windowing-system dependency.

#### 7a. RtMidi — External MIDI Port I/O

| Property | Value |
|----------|-------|
| Repository | https://github.com/thestk/rtmidi |
| Pinned commit SHA | `a3233c22949342f6697681e2cf2403e27fcf0c9e` |
| License | MIT |
| License file at SHA | https://raw.githubusercontent.com/thestk/rtmidi/a3233c22949342f6697681e2cf2403e27fcf0c9e/LICENSE |
| Committed license | `docs/licenses/RtMidi-LICENSE.txt` |
| License SPDX | MIT |
| Patent grant | None |
| Transitive dependencies | None vendored. All backends use OS system APIs or system-installed libraries. |
| FetchContent ready | Yes. CMakeLists.txt provided; exports `rtmidi` target. |
| Warning isolation | Separate CMake target, SYSTEM includes. |

**Backend architecture evidence matrix** (from source inspection at
pinned SHA CMakeLists.txt):

| Target OS | Architecture | Status | Native Backend | System Deps |
|-----------|-------------|--------|----------------|-------------|
| macOS 14+ | arm64 | PROVISIONAL | CoreMIDI | System frameworks (CoreMIDI, CoreAudio, CoreFoundation, CoreServices). JACK explicitly OFF. |
| macOS 14+ | x86-64 | PROVISIONAL | CoreMIDI | Same as arm64. |
| Windows 11 | x86-64 | PROVISIONAL | WinMM | `winmm` (system). |
| Windows 11 | arm64 | PROVISIONAL (build-only) | WinMM | Same as x86-64. |
| Ubuntu 24.04 | x86-64 | PROVISIONAL | ALSA sequencer, JACK | System libraries (`libasound`, `libjack`). ALSA must be explicitly ON. |
| Ubuntu 24.04 | arm64 | PROVISIONAL (build-only) | ALSA sequencer, JACK | Same as x86-64. |

**Deterministic backend flags** (RtMidi CMake options at pinned SHA
`a3233c22949342f6697681e2cf2403e27fcf0c9e`; every option explicitly
decided per platform):

| Flag | Pinned Default | Explicit GraphScore Value | Rationale |
|------|---------------|---------------------------|-----------|
| `RTMIDI_API_CORE` | `${APPLE}` (ON on macOS) | `ON` (macOS) | CoreMIDI on macOS |
| `RTMIDI_API_WINMM` | `${WIN32}` (ON on Windows) | `ON` (Windows) | WinMM MIDI on Windows |
| `RTMIDI_API_ALSA` | `${ALSA}` (**NOT auto-detect**) | `ON` (Linux) | **Must be explicitly ON on Linux.** The pinned CMakeLists.txt uses `option(RTMIDI_API_ALSA ... ${ALSA})` where `${ALSA}` is a CMake variable (not a `find_package` result). It evaluates to empty/undefined by default — it does NOT auto-detect ALSA. The adapter must explicitly pass `-DRTMIDI_API_ALSA=ON` and ensure `find_package(ALSA REQUIRED)` succeeds on the Linux build host. |
| `RTMIDI_API_JACK` | `${HAVE_JACK}` (ON if JACK found, any OS) | `ON` (Linux), `OFF` (macOS, Windows) | JACK MIDI on Linux. **Must be explicitly OFF on macOS/Windows**: the pinned default uses `${HAVE_JACK}` which is set by `find_library(JACK_LIB jack)` and `pkg_check_modules(jack jack)` without any OS guard — JACK can be found on macOS via Homebrew or on Windows via MSYS2. |
| `RTMIDI_API_AMIDI` | `${ANDROID}` | `OFF` (all desktop) | Android only; not applicable |
| `RTMIDI_BUILD_TESTING` | `ON` | `OFF` | No test programs in default build |

**Critical correction — `RTMIDI_API_ALSA` does not auto-detect**:

The pinned CMakeLists.txt line is:
```cmake
option(RTMIDI_API_ALSA "Compile with ALSA support." ${ALSA})
```

The `${ALSA}` variable reference evaluates to the CMake variable named
`ALSA`, not an auto-detection result. Unless `ALSA` is externally set
(e.g., via `find_package(ALSA)` elsewhere in the build), this default
is empty, which CMake treats as `OFF`. The adapter must explicitly pass
`-DRTMIDI_API_ALSA=ON` and ensure `libasound-dev` or equivalent is
available on the Linux build host.

**Decision**: **PROVISIONAL**. External MIDI port I/O only (hardware
MIDI devices connected via USB or MIDI interface). Not needed for the
GraphScore runtime or VST3 audition path (both use internal MIDI
message encoding). May be promoted to POLICY-CLEARED when the explicit
ALSA dependency is verified and empirical device tests are completed.
No empirical MIDI-device tests have been run at the pinned SHA.

#### 7b. MIDI Message Model — Owned Code

GraphScore-owned code (M2). No third-party dependency.

**Decision**: Owned code. POLICY-CLEARED.

---

### 8. VST3 SDK — Plugin Host

**Decision**: DEFERRED to M0 Phase D. SHA to be pinned in that spike.

---

### 9. Bravura — SMuFL Music Font (Spike Asset)

| Property | Value |
|----------|-------|
| Repository | https://github.com/steinbergmedia/bravura |
| Pinned commit SHA | `02e8ed29a29115df35007d1178cebaeee26c20e1` |
| License | SIL Open Font License 1.1 |
| License URL at SHA | https://raw.githubusercontent.com/steinbergmedia/bravura/02e8ed29a29115df35007d1178cebaeee26c20e1/LICENSE.txt |
| Committed license | `docs/licenses/Bravura-OFL.txt` |
| License SPDX | OFL-1.1 |
| Patent grant | None (OFL 1.1; copyright and trademark only) |
| Notices | Copyright © 2019, Steinberg Media Technologies GmbH, with Reserved Font Name "Bravura". Condition 3 of the OFL restricts use of the Reserved Font Name "Bravura" in modified versions without written permission. |
| Used file | `redist/otf/Bravura.otf` |
| Build tool integration | `FetchContent` at pinned SHA; OTF file copied to build directory at configure time. Supports `FETCHCONTENT_SOURCE_DIR_BRAVURA` override for offline builds. |

**Decision**: POLICY-CLEARED as a spike-only font asset. Bravura is the
reference SMuFL font and is used for Phase C rendering/notation spike
demonstrations. It is NOT a default build dependency — production may
use the same or an alternative SMuFL-compatible font. The font file is
fetched from the upstream repository at an immutable commit SHA; no
binary is committed to the repository.

**Font path**: `redist/otf/Bravura.otf` at pinned SHA (`02e8ed29...`).

---

### 10. Noto Sans — Reproducible Latin Text Font (Spike)

| Property | Value |
|----------|-------|
| Repository | https://github.com/notofonts/noto-fonts |
| Pinned commit SHA | `ffebf8c1ee449e544955a7e813c54f9b73848eac` (2023-01-25) |
| License | SIL Open Font License 1.1 |
| License URL at SHA | https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/LICENSE |
| Committed license | `docs/licenses/NotoSans-OFL.txt` |
| License SPDX | OFL-1.1 |
| Patent grant | None (OFL 1.1; copyright and trademark only) |
| Notices | Copyright 2018 The Noto Project Authors. "Noto" is a trademark of Google LLC. |
| Used file | `archive/hinted/NotoSans/NotoSans-Regular.ttf` |
| Build tool integration | `file(DOWNLOAD)` from pinned raw URL with `EXPECTED_HASH` (SHA-256 `d78a4640e19e06c128e2041d480d5ddfd8db4fdecb3d582ca12b26aef1548bf9`); TTF file copied to build directory at configure time. Supports `NOTO_SANS_SRC` FILEPATH cache variable for offline builds. Every acquisition path verified against the pinned SHA-256. |

**Decision**: POLICY-CLEARED as a spike-only reproducible text font. Noto
Sans Regular provides deterministic Latin text shaping and glyph rasterization
for all self-tests. It is NOT a default build dependency — pixel-level text
raster assertions are spike-only. The exact pinned artifact is required for
all text-facing tests; no system-font fallback is permitted.

**Font path**: `archive/hinted/NotoSans/NotoSans-Regular.ttf` at pinned SHA
(`ffebf8c1...`).

---

### 11. GoogleTest — Unit Test Framework (Production, M1)

| Property | Value |
|----------|-------|
| Repository | https://github.com/google/googletest |
| Pinned commit SHA | `6910c9d9165801d8827d628cb72eb7ea9dd538c5` (release 1.16.0) |
| License | BSD 3-Clause |
| License URL at SHA | https://raw.githubusercontent.com/google/googletest/6910c9d9165801d8827d628cb72eb7ea9dd538c5/LICENSE |
| Committed license | `docs/licenses/GoogleTest-BSD-3-Clause.txt` (verified byte-identical to the pinned-SHA license at M1 promotion) |
| License SPDX | BSD-3-Clause |
| Patent grant | None stated in BSD 3-Clause |
| Notices | Copyright 2008, Google Inc. Retain the BSD 3-Clause copyright, conditions, and disclaimer when a binary distribution embeds compiled test binaries (test executables are development-only artifacts and are not shipped in `0.1.0` product archives). |
| Build tool integration | `FetchContent_Declare(googletest GIT_TAG 6910c9d9165801d8827d628cb72eb7ea9dd538c5)` in `cmake/dependencies.cmake`. `BUILD_GMOCK=OFF` (GraphScore does not use gMock). `INSTALL_GTEST=OFF`. `gtest_force_shared_crt` set `ON` on Windows to match the writer/runtime MSVC runtime selection. Supports `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` override for offline builds. |
| Transitive closure | None. Only `GTest::gtest` and `GTest::gtest_main` targets are consumed; gMock is disabled at the source. |

**Decision**: **POLICY-CLEARED.** This entry supersedes the prior spike-only
GoogleTest note (`docs/NOTICES.md` #13): GoogleTest is promoted from
spike-only to the production unit test framework required by every
milestone's Definition Of Done, per ADR 0003 §2.3. Test executables link
`GTest::gtest_main` and are development/CI-only artifacts — they are never
part of a shipped writer or runtime binary distribution, so the BSD 3-Clause
notice obligation applies only to development/CI environments, not `0.1.0`
release archives.

Named test-executable targets (`graphscore_<component>_test` per production
CMake target) and their permitted edges are defined in ADR 0003 §2.1/§2.2 as
amended alongside the M1 target-DAG implementation.

---

## Summary Matrix

| Category | Candidate | Status | SHA |
|----------|-----------|--------|-----|
| Platform shell | SDL3 | PROVISIONAL | `08b9c55393be5cb08fbec12ca431470faba3c8c9` |
| Accessibility | accesskit-c | EXCLUDED | `826d672661f9453c8b269ab3946dbcbae6300555` |
| Vector renderer | ThorVG | PROVISIONAL (excluded) | `6d5933c9d1aca94635c6ad8129f3530ae554d423` |
| Text shaping | HarfBuzz | PROVISIONAL | `af192b7e0f49a9965220ba3f18473e3f8e28b8b9` |
| Font rendering | FreeType | POLICY-CLEARED | `f01dec5e676847267834b881b25f6e8c79581163` |
| Audio device | miniaudio | PROVISIONAL | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` |
| Audio fallback | PortAudio | PROVISIONAL | `f88b5841575b43bfa024a6861635b69d7eb98d6d` |
| MIDI I/O | RtMidi | PROVISIONAL | `a3233c22949342f6697681e2cf2403e27fcf0c9e` |
| MIDI encoding | Owned code | POLICY-CLEARED | N/A |
| VST3 | VST3 SDK | DEFERRED | To be pinned |
| SMuFL font | Bravura | POLICY-CLEARED (spike) | `02e8ed29a29115df35007d1178cebaeee26c20e1` |
| Text font | Noto Sans | POLICY-CLEARED (spike) | `ffebf8c1ee449e544955a7e813c54f9b73848eac` |
| Test framework | GoogleTest | POLICY-CLEARED | `6910c9d9165801d8827d628cb72eb7ea9dd538c5` |

## CMake Adapters Required (M1 Implementation Gates)

| Dependency | Adapter | Status |
|------------|---------|--------|
| SDL3 | `cmake/SDL3.cmake` — sets reviewed dependency/backend options; M1 must also audit remaining unaudited defaults and produce full configure-cache evidence | PROVISIONAL |
| HarfBuzz | `cmake/HarfBuzz.cmake` — sets options, enforces isolation order, verifies `get_target_property` no FreeType link | PROVISIONAL |
| miniaudio | `cmake/miniaudio.cmake` — `INTERFACE` or `STATIC` target, `MA_ENABLE_ONLY_SPECIFIC_BACKENDS`, isolated warning translation unit | PROVISIONAL |
| FreeType | No adapter needed (`add_subdirectory`-ready) | POLICY-CLEARED |
| PortAudio | `cmake/PortAudio.cmake` — sets all options explicitly per platform | PROVISIONAL |
| RtMidi | `cmake/RtMidi.cmake` — sets all API flags explicitly per platform | PROVISIONAL |
| ThorVG, accesskit-c | Adapter required (excluded from default closure) | PROVISIONAL (excluded) |
| GoogleTest | `cmake/dependencies.cmake` — `BUILD_GMOCK=OFF`, `INSTALL_GTEST=OFF`, `gtest_force_shared_crt` on Windows | POLICY-CLEARED |

## License Inventory

Recorded in `docs/NOTICES.md` with committed license files in
`docs/licenses/`.

- `docs/licenses/Bravura-OFL.txt` — SIL Open Font License 1.1
  (source at pinned SHA `02e8ed29a29115df35007d1178cebaeee26c20e1`)
- `docs/licenses/NotoSans-OFL.txt` — SIL Open Font License 1.1
  (Noto Sans Regular source at pinned SHA
  `ffebf8c1ee449e544955a7e813c54f9b73848eac`; artifact SHA-256
  `d78a4640e19e06c128e2041d480d5ddfd8db4fdecb3d582ca12b26aef1548bf9`)

## Fallback Matrix

| If this fails... | Use this alternative... | Reason |
|------------------|------------------------|--------|
| accesskit-c (Rust build) | Platform-native accessibility APIs | NSAccessibility / UI Automation / AT-SPI2 |
| ThorVG (Meson/Ninja blocker) | Owned render-list/tessellation | Explicitly adopted fallback for Phase C |
| miniaudio (latency/stability) | PortAudio | Mature, MIT. Exclude ASIO. |
| HarfBuzz CMake adapter (unmaintainable) | Meson wrap + `ExternalProject_Add` | Still permissive |
| SDL3 (adapter verification fails) | Direct platform API calls | Win32/Cocoa/Wayland/X11 per platform |

---

## Amendment (2026-08-03): M05 Rendering Dependency Resolution

Decisions in this section are Adam's, recorded ahead of the CMake bring-out
of M05's rendering dependencies (see `docs/plan/README.md`'s "Execution
Order (Adam, 2026-08-03)" section, which names exactly the four items
resolved here — not `docs/plan/05-notation-editor.md`, which does not
contain that section). This amendment is purely additive: no pin, table, or decision text in
§1, §3a, §3b, §4, §5, or §9 above is edited, reordered, or removed. Where a
decision recorded above is superseded, that supersession is stated here,
alongside the section it supersedes, following the precedent set by ADR 0003
§2.3 ("RESOLVED in M1") for a prior deferred item. The corresponding
ADR 0003 §8 target-DAG row is resolved in a matching 2026-08-03 amendment to
that ADR.

This amendment is a documentation-only round: it specifies pins, licenses,
and an implementable adapter blueprint for Round 2 (M05 Phase 1's CMake
bring-up) to build against. No CMake, C, or C++ file changes are made here.

### A1. Meson and Ninja — ThorVG Build Toolchain (discharges §3a blocker #1)

§3a blocker #1 required "an ADR or ADR amendment must pin Meson and Ninja to
exact SHAs/tags and record their licenses before ThorVG can be accepted."
Both are recorded below. Neither tool is linked into, `#include`d by, or
shipped inside any GraphScore or ThorVG binary artifact — both are
build-time-only tooling that drives ThorVG's own build, exactly as CMake
itself is build-time-only tooling for the rest of this repository.

#### A1.1 Meson

| Property | Value |
|----------|-------|
| Repository | https://github.com/mesonbuild/meson |
| Pinned commit SHA | `ff84a1ab2699385f67eea990260a20beb2b46c98` (release `1.11.2`, tagged 2026-07-11 — the latest non-release-candidate tag as of 2026-08-03) |
| License | Apache License 2.0 |
| License URL at SHA | https://raw.githubusercontent.com/mesonbuild/meson/ff84a1ab2699385f67eea990260a20beb2b46c98/COPYING |
| Committed license | `docs/licenses/Meson-COPYING.txt` |
| License SPDX | Apache-2.0 |
| Patent grant | Yes — Apache-2.0 §3, the same terms GraphScore itself is licensed under (ADR 0001) |
| Role | Build-tool dependency only. Never a link-time or compile-time dependency of any GraphScore or ThorVG target. |
| Minimum Python | `>= 3.7.0`, enforced at the top of `meson.py` and `setup.py` at the pinned SHA: `if sys.version_info < (3, 7): raise SystemExit(...)` / `sys.exit(1)`. The repository's Python at review time is 3.9.6, which satisfies this. **Not a blocker.** |
| Build tools | None of its own — Meson is pure Python (`import` statements in `mesonbuild/mesonmain.py` at the pinned SHA are standard-library only). It is invoked directly from its populated source checkout; `pip install` is never run and no PyPI access ever occurs. |
| Acquisition | `FetchContent_Declare(Meson GIT_REPOSITORY https://github.com/mesonbuild/meson.git GIT_TAG ff84a1ab2699385f67eea990260a20beb2b46c98)` followed by `FetchContent_MakeAvailable(Meson)` — **corrected in this fix round**; the original draft of this row specified the single-argument `FetchContent_Populate(Meson)` form, which is deprecated (CMake warns on it starting with the release this repository's review host runs, CMake 4.4.0) and is not needed here regardless: CMake's own `FetchContent.cmake` (`__FetchContent_MakeAvailable`, verified against the `v3.31.0` tag) only calls `add_subdirectory()` when `EXISTS <populated-source-dir>/CMakeLists.txt`; Meson's repository has no `CMakeLists.txt` anywhere in its tree, so `FetchContent_MakeAvailable(Meson)` populates the source, sets `meson_SOURCE_DIR`/`meson_BINARY_DIR`, and safely skips `add_subdirectory` — identical externally-observable behaviour to `Populate`, without using the deprecated single-argument form or needing a `SOURCE_SUBDIR` guard. The adapter invokes it as `${Python3_EXECUTABLE} ${meson_SOURCE_DIR}/meson.py <args>`. |
| Offline override | `FETCHCONTENT_SOURCE_DIR_MESON`. See §A1.3 for how the existing `graphscore_offline_dependencies` test covers this without modification. |

The tag `1.11.2` is an **annotated** tag: `GET /repos/mesonbuild/meson/git/refs/tags/1.11.2` resolves to tag object
`c3da14cedc9e818961222bdf930c3d3b7e016714`, and `GET /repos/mesonbuild/meson/git/tags/c3da14cedc9e818961222bdf930c3d3b7e016714`
dereferences that tag object to commit `ff84a1ab2699385f67eea990260a20beb2b46c98` — the commit pinned above. Both
API calls were made on 2026-08-03. This is analogous to — not the same
verification as — the AccessKit/Corrosion `v0.6.1` pin in §2: §2 resolves a
*mutable lightweight tag/branch reference* to the immutable commit it
currently happens to point at, while this dereferences an *immutable
annotated tag object* to the commit it points at (a tag object, once
created, does not move; the two-step API call here is confirming what the
tag object records, not chasing a moving reference).

#### A1.2 Ninja

**Correction (fix round, 2026-08-03)**: the original draft of this
subsection specified building Ninja from source via `FetchContent`. That
design is withdrawn here and replaced with locating an already-installed
`ninja` via `find_program`, for the reasons given after the table. The pin
below is retained purely as the reviewed license/provenance record — the SHA
at which Ninja's Apache-2.0 license was reviewed — not as a build recipe and,
per the second correction below, not as the source of the version floor
either; the floor is independently derived from what actually consumes the
Ninja executable (Meson's own `--backend=ninja` step), not from this
provenance SHA.

**Second correction (Round 1 review, 2026-08-03)**: the version floor below
was originally set to `1.13.2` — the version tagged at the pinned provenance
SHA — with the stated rationale "rather than silently producing a different
ThorVG build than the one this ADR reviews." That rationale does not hold: a
build tool's own version does not change the artifact it produces the way a
library's version does, so the SHA a license was reviewed at is not evidence
for what version floor the build actually needs. The floor instead comes
from what Meson itself requires: `mesonbuild/backend/ninjabackend.py` at the
Meson pin (`ff84a1ab2699385f67eea990260a20beb2b46c98`) writes
`ninja_required_version = 1.8.2` into every generated build file and raises
`MesonException('Could not detect Ninja v1.8.2 or newer')` if the located
`ninja --version` resolves below that. `1.13.2` is also unsatisfiable on two
of this repository's own CI images: Ubuntu 24.04's `apt install ninja-build`
provides `ninja` 1.11.1 (the `test` job, `.github/workflows/ci.yml:174`, and
the `cross-build` job, line 257, both configure with
`GRAPHSCORE_BUILD_WRITER=ON` and would hit this), and the VS 2022 toolchain
`ilammy/msvc-dev-cmd` provisions on Windows (lines 183-187) also ships below
1.13.2. The floor below is corrected to `1.8.2` — Meson's own documented
minimum — which every current CI image already satisfies without a package
change.

| Property | Value |
|----------|-------|
| Repository | https://github.com/ninja-build/ninja |
| Pinned commit SHA (license/provenance record, not built; not the source of the version floor below — see the second correction above) | `3441b633c2fe2c494e958780ba0f4227b1327634` (release `v1.13.2`, tagged 2025-11-20; a lightweight tag pointing directly at this commit — `GET /repos/ninja-build/ninja/git/refs/tags/v1.13.2` returns `object.sha = 3441b633...` with `object.type = "commit"`, no dereferencing needed) |
| License | Apache License 2.0 |
| License URL at SHA | https://raw.githubusercontent.com/ninja-build/ninja/3441b633c2fe2c494e958780ba0f4227b1327634/COPYING |
| Committed license | `docs/licenses/Ninja-COPYING.txt` |
| License SPDX | Apache-2.0 |
| Patent grant | Yes — Apache-2.0 §3 |
| Role | Build-tool dependency only. Never a link-time or compile-time dependency of any GraphScore or ThorVG target. |
| Acquisition | `find_program(GRAPHSCORE_NINJA_EXECUTABLE NAMES ninja ninja-build REQUIRED)`, followed by running `${GRAPHSCORE_NINJA_EXECUTABLE} --version` and comparing it against a `1.8.2` floor (`if(result VERSION_LESS "1.8.2")` `FATAL_ERROR`) — **corrected, Round 1 review**: `1.8.2` is Meson's own documented minimum (`mesonbuild/backend/ninjabackend.py` at the Meson pin, `ninja_required_version = 1.8.2`), so a Ninja executable too old for Meson's `--backend=ninja` step to drive fails CMake configure with an actionable message rather than an opaque `MesonException` surfacing from deep inside `meson setup`. The previously drafted `1.13.2` floor was unsatisfiable by two of this repository's own CI images (Ubuntu 24.04's packaged `ninja-build` gives 1.11.1; the VS 2022 toolchain's bundled Ninja is also older) and rested on a rationale — that a build tool's own version selects which artifact is produced — that does not hold. No source is fetched or compiled for Ninja itself. |
| Offline/air-gap posture | No `FetchContent` entry, so no `FETCHCONTENT_SOURCE_DIR_NINJA` override and no `_deps/ninja-src` directory exist; `graphscore_offline_dependencies` (§A1.3) has nothing to discover here, which is correct — `find_program` never touches the network in the first place. |

**Why `find_program` instead of building from source**: every GraphScore
CMake preset (`CMakePresets.json`, the shared `common` preset object)
already declares `"generator": "Ninja"` — a working `ninja` binary on the
build host is already a hard precondition of running `cmake --preset
<any>` at all, before ThorVG enters the picture. CI already installs it
independently for that reason (`ninja-build`/`ninja` packages,
`.github/workflows/ci.yml` lines 65, 174, 181, 257, 292, and 336, one per
job that needs it).
Building a second, separate copy from pinned source solely to hand its path
to Meson's `--backend=ninja` step reproduces tooling every build host is
already required to provide, and it is not free: it re-introduces exactly
the class of defect §A7 below documents generally for any
`FetchContent`-added dependency that carries its own `CTest` integration —
Ninja's own `CMakeLists.txt` gates `ninja_test` behind `include(CTest);
if(BUILD_TESTING)`, and inside that block, absent a system `GTest`, declares
its own independent `FetchContent_Declare(googletest URL
https://github.com/google/googletest/releases/download/v1.16.0/googletest-1.16.0.tar.gz
URL_HASH SHA256=78c676fc63881529bf97bf9d45948d905a66833fbfa5318ea2cd7478cb98f399)`
— a second, GraphScore-unreviewed acquisition of the exact GoogleTest
release this repository already pins itself (§11), fetched from a release
tarball rather than `git`, under the same CMake-wide `googletest`
FetchContent name GraphScore's own `cmake/dependencies.cmake` already
declares. Two `FetchContent_Declare(googletest ...)` calls with different
arguments under the same project is a name collision, not merely a
redundant fetch: whichever call's properties CMake retains determines which
GoogleTest GraphScore's own test suites end up linking, silently. Suppressing
it requires exactly the directory-scope `BUILD_TESTING` handling that §A7
below specifies as a general obligation for *every* dependency added to this
build — an extra moving part this dependency does not need to exist at all,
since a host-provided `ninja` this project already requires makes the whole
from-source path unnecessary. `re2c` availability, Ninja's own minimum CMake
floor, and `NINJA_BUILD_BINARY`'s default-ON gating of `add_executable(ninja
...)` (all true statements about Ninja's build, verified at the pinned SHA)
are consequently no longer GraphScore build concerns, because GraphScore
never builds Ninja.

#### A1.3 Configure-time vs. build-time execution, and the offline test

Meson's own source is populated at CMake **configure** time via
`FetchContent_MakeAvailable` (§A1.1, corrected in this fix round),
identically timed to every other `FetchContent` dependency in this
repository; it is never itself "built." Ninja is not fetched or built at
all (§A1.2, corrected in this fix round): it is located on the host via
`find_program` at configure time, with a version-floor check, exactly like
locating any other required build tool (a C++ compiler, Python).

The new timing model belongs to **ThorVG's own compilation** (§A2), driven
through `ExternalProject_Add`. Its `CONFIGURE_COMMAND` (`meson setup`) and
`BUILD_COMMAND` (`${GRAPHSCORE_NINJA_EXECUTABLE} -C <builddir>`, using the
host `ninja` located in §A1.2) run as steps in the generated build graph —
i.e. **during `cmake --build`**, not during the outer CMake **configure**
pass. Every other adapter in this repository is a native CMake subproject
configured in the same configure pass as GraphScore itself; ThorVG is the
first dependency here whose own build is a separate, out-of-band toolchain
invocation that happens later, at build time.

Consequence for `graphscore_offline_dependencies`
(`tests/repository/check_offline_dependencies.cmake`): that test is
explicitly **configure-only** by its own header comment ("It is a
configure-only test: building the dependencies again would add minutes
without testing anything the main build does not already cover"). It
re-runs only the outer `cmake -S/-B` configure step, with
`FETCHCONTENT_FULLY_DISCONNECTED=ON` and a `FETCHCONTENT_SOURCE_DIR_<NAME>`
override per dependency it discovers by globbing `<build>/_deps/*-src`.
Because ThorVG's and Meson's *source population* both happen through the
ordinary `FetchContent_MakeAvailable` machinery at configure time, this test
picks up both automatically — exactly as its own design intends ("a
milestone that adds a dependency then gets offline coverage for it
automatically") — with **no change to the test itself** required. Ninja is
not part of this coverage and needs none: `find_program` never touches the
network, so there is nothing for a fully-disconnected configure to break.

What that test does **not** exercise is the `ExternalProject_Add` steps,
because it never calls `cmake --build` on the scratch tree it configures. It
therefore cannot, by itself, prove that `meson setup` stays network-silent
under a fully disconnected host. Source inspection at the pinned SHA closes
that gap by construction rather than by test coverage: with the option set
in §A2.2 (`loaders=`, `savers=`, `bindings=`, and `tools=` all empty),
`meson.build` and every `subdir()`-included file it reaches at the pinned
SHA contain **zero** `subproject()` calls and zero
`dependency(..., fallback: ...)` calls. The only such calls anywhere in the
tree live in the `external_png`/`external_jpg`/`external_webp` loader
directories and the `lottie` loader's vendored `jerryscript` subtree — none
of which are ever `subdir()`-entered, because their gating loader/extra
flags (`png_loader`, `jpg_loader`, `webp_loader`, `lottie_loader`,
`lottie_exp`) are all false under this option set. There is no code path by
which `meson setup` could reach the network with GraphScore's chosen
options, independent of whether this specific test's scope happens to cover
the build phase. The adapter additionally passes `--wrap-mode=nodownload`
unconditionally as defense in depth, so a future pin bump that *does*
introduce a wrap dependency fails configure loudly rather than reaching the
network.

This is a **known scope boundary, not a blocker**: CI's ordinary networked
build already exercises `meson setup`/`ninja install` on every run, and the
option set above makes that step provably incapable of a network access
regardless of whether the offline test's scope is later extended to cover
it. A later hardening pass may still choose to make
`check_offline_dependencies.cmake` build the ThorVG `ExternalProject` step
in its scratch tree for defense in depth; this amendment does not require
it, and flags it here so the gap is a recorded decision rather than an
oversight.

### A2. ThorVG — Accepted as the Production Vector-Rendering Backend

**Decision (Adam, 2026-08-03)**: ThorVG is **ACCEPTED** as the production
rasterization backend for `graphscore_rendering`. This supersedes §3a's
"PROVISIONAL, excluded from the selected default closure" decision. The
owned render-list/tessellation fallback (§3b) remains valid GraphScore-owned
code and is retained as the documented regression path (Fallback Matrix,
unchanged) if a future ThorVG pin regresses the build gate below — it is no
longer the production rendering backend. ADR 0003 §8's "ThorVG vs. owned
render-list" row and ADR 0004 §1's "confirmed... active Phase C path"
characterization of the owned rasterizer are both superseded for production
by matching 2026-08-03 amendments to those two documents; neither amendment
rewrites the spike's historical findings, which remain a true record of what
Milestone 00 built and validated.

Retirement of §3a's three stated acceptance blockers:

1. **Meson and Ninja toolchain** — retired by §A1. Both tools are pinned to
   immutable commit SHAs, both are Apache-2.0 (the same license GraphScore
   itself uses), and both are reproducible offline through the same
   `FetchContent`-based mechanism as every other dependency here.
2. **CMake adapter** — specified as an implementable blueprint below. Round 2
   (M05 Phase 1's CMake bring-up) implements `cmake/ThorVG.cmake` against it.
3. **Empirical validation** (feature set, binary size, platform
   compatibility) — remains a Round 2 build-gate requirement, in the same
   spirit as SDL3's own M1 Verification Gate (§1) and ADR 0004's spike
   validation of the owned rasterizer. This amendment specifies the option
   set and the verification design; it does not itself constitute empirical
   validation, since no CMake or build-tool code is written in this round.

#### A2.1 Integration mechanism

Two mechanisms were evaluated, per Adam's direction to prefer the
Meson-driven path unless a concrete blocker exists. None was found.

**(a) Meson-driven `ExternalProject_Add`, consumed as an `IMPORTED` library
— CHOSEN.**

ThorVG's own source tree at the pinned SHA has no `CMakeLists.txt` anywhere
(confirmed by listing the root tree: `meson.build`, `meson_options.txt`,
`inc/`, `src/`, `test/`, `tools/`, `cross/`, `LICENSE` — no CMake file).
`ExternalProject_Add` drives Meson's own `setup`/`compile`/`install` cycle
as build-graph steps and exposes the result to the rest of the CMake project
as a normal `IMPORTED` target:

1. `FetchContent_Declare(ThorVG GIT_REPOSITORY https://github.com/thorvg/thorvg.git GIT_TAG 6d5933c9d1aca94635c6ad8129f3530ae554d423)`
   + `FetchContent_MakeAvailable(ThorVG)` to obtain `thorvg_SOURCE_DIR`.
   **Corrected in this fix round**: the original draft specified
   `FetchContent_Populate(ThorVG)`, the deprecated single-argument form.
   `MakeAvailable` is the non-deprecated replacement here for the same
   reason given in §A1.1's corrected Meson row: `FetchContent.cmake`'s
   `__FetchContent_MakeAvailable` (verified at the `v3.31.0` tag) only calls
   `add_subdirectory()` when a `CMakeLists.txt` exists at the populated
   source directory, and ThorVG's tree has none anywhere.
2. `ExternalProject_Add(ThorVGBuild DOWNLOAD_COMMAND "" SOURCE_DIR "${thorvg_SOURCE_DIR}" ...)`
   with:
   - `CONFIGURE_COMMAND ${Python3_EXECUTABLE} ${meson_SOURCE_DIR}/meson.py setup <build-dir> <source-dir> --backend=ninja --wrap-mode=nodownload --buildtype=<mapped from CMAKE_BUILD_TYPE> -Dwerror=false -Dlibdir=lib -Dincludedir=include -Dnamingscheme=classic --prefix=<install-dir> -D<option>=<value> [...]` — the option set is §A2.2 below. `-Dwerror=false` (corrected from the originally drafted `--werror=false`): `werror` is one of Meson's own builtin options (`mesonbuild/options.py` at the Meson pin: `UserBooleanOption('werror', 'Treat warnings as errors', False)`), set through the same generic `-D<name>=<value>` mechanism as every other option in this table, not a dedicated `--werror` CLI flag. `-Dlibdir=lib -Dincludedir=include -Dnamingscheme=classic` are **added, Round 1 review** — see the corrected `libdir`/`includedir`/`namingscheme` rows in §A2.2 and the rationale in the `BUILD_BYPRODUCTS` bullet below for why each must be pinned explicitly rather than left at Meson's host-derived default.
   - `BUILD_COMMAND ${GRAPHSCORE_NINJA_EXECUTABLE} -C <build-dir>`, using the
     host `ninja` located per §A1.2 (corrected in this fix round: §A1.2 no
     longer builds a `ninja` CMake target, so there is nothing to
     `add_dependencies(ThorVGBuild ninja)` on; the located executable path is
     already available before this step runs because `find_program` resolves
     it at configure time).
   - `INSTALL_COMMAND ${GRAPHSCORE_NINJA_EXECUTABLE} -C <build-dir> install`.
   - Environment passthrough beyond `CC`/`CXX` below: on the `asan-ubsan` and
     `tsan` presets, ThorVG's separate Meson invocation must explicitly pass
     `-Db_sanitize=none` — Meson has no mapping from GraphScore's own
     sanitizer presets to its own `b_sanitize` option, and an uninstrumented
     third-party static archive linked into an otherwise-instrumented binary
     is the standard source of TSan false positives (unsynchronized-looking
     accesses inside code the sanitizer never saw instrumented). This
     preserves `threads=true`'s own `std::thread` scheduler (§A2.2) as
     uninstrumented third-party code, consistent with `AGENTS.md`'s
     "third-party warnings/instrumentation are isolated" precedent for
     SDL3/FreeType/HarfBuzz. On Windows, `CMAKE_MSVC_RUNTIME_LIBRARY`
     (GraphScore's own CRT selection) must be translated to Meson's
     equivalent `-Db_vscrt=<value>` (`mesonbuild/options.py`:
     `'b_vscrt', 'VS run-time library type to use.', 'from_buildtype'` —
     confirmed a real builtin option at the Meson pin); a mismatched CRT
     between ThorVG's separate build and the rest of GraphScore is a link
     failure on Windows, the same class of defect the compiler-passthrough
     paragraph below addresses for the compiler identity itself. On macOS,
     `CMAKE_OSX_DEPLOYMENT_TARGET`, `CMAKE_OSX_SYSROOT`, and
     `CMAKE_OSX_ARCHITECTURES` must be translated to the corresponding
     Meson/compiler flags (`-mmacosx-version-min=`, `-isysroot`, `-arch`) so
     ThorVG's separate build targets the same deployment target, SDK, and
     architecture set as the rest of the writer binary it is linked into.
     None of this environment/flag translation is executed by this
     documentation-only round; it is specified here as a Round 2 build-gate
     requirement, the same status as the option set itself (§A2.2).
   - `BUILD_BYPRODUCTS <install-dir>/lib/libthorvg-1.a` on **every** target
     platform, including Windows — not merely "the exact versioned filename
     Round 2 must confirm" as the original draft stated, and **corrected,
     Round 1 review, to also pin the directory component (`lib/`), not only
     the filename**. Verified directly against Meson's own naming logic at
     the Meson pin (`ff84a1ab2699385f67eea990260a20beb2b46c98`): the
     `namingscheme` builtin option defaults to `'classic'`
     (`mesonbuild/options.py`: `UserComboOption('namingscheme', ..., 'classic',
     choices=['platform', 'classic'])`), and
     `StaticLibrary.determine_default_prefix_and_suffix` (`mesonbuild/build.py`)
     only consults the OS-specific `DEFAULT_STATIC_LIBRARY_NAMES` table
     (which would give `('', 'lib')` — i.e. `thorvg-1.lib` — on Windows) when
     `namingscheme == 'platform'`; under the `'classic'` scheme it falls
     through to the unconditional `prefix = 'lib'` / `suffix = 'a'` defaults
     with the code comment explaining why: "a static library is named
     libfoo.a even on Windows because MSVC does not have a consistent
     convention for what static libraries are called... we cannot use foo.lib
     because that's the same as the import library." ThorVG's `library()`
     call in `src/meson.build` does not override `namingscheme`, `prefix`, or
     `suffix`, so `libthorvg-1.a` is the produced *filename* on macOS, Linux,
     and Windows alike (the `1` is the major-version component of
     `meson.project_version()`, currently `1.0.0` at the pinned SHA, from the
     target name `'thorvg-' + vmaj` in `src/meson.build`) — **but this is true
     only because GraphScore passes `-Dnamingscheme=classic` explicitly, added
     to the invocation above; the original draft observed the current default
     value rather than pinning it, which is the same exposure the directory
     component has (next paragraph) and belongs to the same defect family
     §A7.3 names**.

     The *directory* component of this path is a separate hazard the original
     draft did not address at all: `<install-dir>/lib/` is only correct if
     Meson's `libdir` builtin option resolves to the literal string `lib`,
     which it does **not** do unconditionally. `mesonbuild/utils/universal.py`'s
     `default_libdir()` at the Meson pin returns `'lib/' + archpath` (e.g.
     `lib/x86_64-linux-gnu` or `lib/aarch64-linux-gnu`) on Debian-like systems
     when `dpkg-architecture -qDEB_HOST_MULTIARCH` succeeds, `'lib64'` when
     `/usr/lib64` exists as a real directory (not a symlink) and the host is
     not Debian-like or the `dpkg-architecture` call failed, and only falls
     back to the bare `'lib'` this ADR assumed in every other case. `libdir`
     is a **builtin**, prefix-independent option — `mesonbuild/options.py`'s
     `BUILTIN_DIR_NOPREFIX_OPTIONS` table (which lists `sysconfdir`,
     `localstatedir`, `sharedstatedir`, `python.platlibdir`, and
     `python.purelibdir`) does **not** include `libdir`, so passing
     `--prefix=<install-dir>` alone, as the original draft did, does nothing
     to pin it: on `ubuntu-latest`/`ubuntu-24.04-arm` CI images with
     `dpkg-architecture` present, `ninja install` would have written
     `<install-dir>/lib/x86_64-linux-gnu/libthorvg-1.a` (or the `aarch64`
     equivalent), not the path this section's `BUILD_BYPRODUCTS` and §A2.1
     step 3's `IMPORTED_LOCATION` name — a host-dependent failure absent from
     a plain Debian slim container (where `dpkg-architecture` is not
     installed and the fallback silently produces `lib`) but present on a
     full Ubuntu image, exactly the kind of environment-dependent regression
     that passes locally and fails in CI. `-Dlibdir=lib`, added to the
     invocation above, pins this explicitly so the path is host-independent.
   - This filename and directory depend on `default_library=static` and
     `libdir=lib` actually resolving — see the corrected `default_library`,
     `static`, `libdir`, `includedir`, and `namingscheme` rows in §A2.2 and
     the artifact-existence check §A2.3 adds for exactly this reason.
   - `CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER}` passed through the
     step environment, so Meson's own compiler auto-detection resolves to
     the **same** compiler (Clang/AppleClang, per this project's own
     toolchain requirement) building the rest of GraphScore. A mismatched
     compiler/standard-library pairing between ThorVG's separate build and
     GraphScore's own would produce an ABI-incompatible static archive when
     linked into `graphscore_rendering`.
3. `add_library(thorvg STATIC IMPORTED GLOBAL)` with:
   - `IMPORTED_LOCATION` pointing at the `libthorvg-1.a` byproduct above.
   - `INTERFACE_COMPILE_DEFINITIONS TVG_STATIC` (**added in this fix round**;
     absent from the original draft). `inc/thorvg.h` at the pinned SHA reads
     `#ifndef TVG_STATIC` / `#ifdef _WIN32` / `#if TVG_BUILD` /
     `#define TVG_API __declspec(dllexport)` / `#else` /
     `#define TVG_API __declspec(dllimport)` — i.e. every `TVG_API`-decorated
     declaration in the public header defaults to `dllimport` semantics on
     Windows unless the consumer also defines `TVG_STATIC`. ThorVG's own
     Meson build supplies this automatically for its consumers through
     `declare_dependency(compile_args: (lib_type == 'static') ?
     ['-DTVG_STATIC'] : [], ...)` in `src/meson.build`, where
     `lib_type = get_option('default_library')` — but that
     `declare_dependency` exists only for consumers reached through Meson's
     own dependency resolution (`meson.override_dependency`), which this
     hand-rolled `IMPORTED` target is not. Without this definition,
     `graphscore_rendering`'s `.cpp` files linking the static archive on
     Windows would see `dllimport`-decorated declarations resolving against
     a static archive that exports no import library, a link failure.
   - `INTERFACE_INCLUDE_DIRECTORIES` pointing at the installed header
     location `<install-dir>/include/thorvg-1/` — **corrected in this fix
     round**: `inc/meson.build` at the pinned SHA is
     `install_headers(header_files, subdir: 'thorvg-' + vmaj)`, so
     `thorvg.h` installs into a version-qualified subdirectory
     (`thorvg-1/thorvg.h`), not directly under `include/`. The original
     draft's "the installed `inc/thorvg.h` location" understated this;
     Round 2's adapter must add `<install-dir>/include/thorvg-1` (not
     `<install-dir>/include`) to `INTERFACE_INCLUDE_DIRECTORIES` so
     `#include <thorvg.h>` resolves. ThorVG's `inc/` directory contains
     exactly one public header, confirmed by listing it. **The `include/`
     directory component is now pinned explicitly, Round 1 review, not
     merely asserted as a default**: unlike `libdir`, Meson's
     `default_includedir()` (`mesonbuild/utils/universal.py` at the Meson
     pin) returns the constant `'include'` on every platform except Haiku,
     so this path held even before this correction — but it held because it
     matched Meson's *current default*, exactly the exposure `libdir` turned
     out to have in practice (see §A2.1 step 2's `BUILD_BYPRODUCTS`
     correction above). `-Dincludedir=include`, added to the invocation in
     step 2, closes the same gap here by pinning the value this section
     relies on instead of assuming it.
   - Marked `SYSTEM` so ThorVG's own header content is exempt from
     GraphScore's `-Werror` flag set when `graphscore_rendering`'s `.cpp`
     files `#include <thorvg.h>`.
   - `add_dependencies(thorvg ThorVGBuild)` orders the external build before
     any GraphScore target links `thorvg`.
4. `graphscore_rendering` links `thorvg` PRIVATE (ADR 0003 §2.2: ThorVG stays
   an external-private edge; `tvg::Canvas`, `tvg::Shape`, and every other
   ThorVG type remain confined to `.cpp` files, never a public header).
   Because ThorVG compiles with `-fno-exceptions -fno-rtti` whenever
   `b_sanitize == 'none'` (`src/meson.build`, cited again in the rejected
   alternative (b) below) — true for every GraphScore build configuration
   except the two sanitizer presets, which this section's environment
   passthrough now sets to `b_sanitize=none` explicitly regardless —
   `graphscore_rendering`'s `.cpp` files must not `dynamic_cast` any ThorVG
   type and must not rely on ThorVG throwing or being able to propagate a
   C++ exception across the library boundary; ThorVG itself never compiles
   RTTI or exception-unwinding support into the archive `graphscore_rendering`
   links.

**Warning isolation**: because ThorVG's own compilation happens entirely
inside the separate Meson+Ninja invocation in step 2 — a wholly different
compiler invocation from GraphScore's own CMake-driven compile commands —
none of GraphScore's `-Wall -Wextra -Wpedantic ... -Werror` flag set (root
`CMakeLists.txt` / `cmake/Warnings.cmake`) ever reaches ThorVG's translation
units at all; there is no shared compile-command line to isolate flags
within, unlike SDL3/FreeType/HarfBuzz where isolation means marking the
`FetchContent_Declare` `SYSTEM` and consuming the resulting targets as
`SYSTEM` include directories. The only isolation surface that remains is the
consuming side — `graphscore_rendering`'s own `#include <thorvg.h>` — handled
by the `SYSTEM` marking on the `IMPORTED` target's include directory in step 3,
the same mechanism the other adapters use.

**(b) GraphScore-authored CMake build of ThorVG's self-contained C++14
sources — REJECTED.**

This was evaluated seriously because the reduced-feature source set is not
large: with `engines=cpu`, `loaders=`, `savers=`, `bindings=`, `tools=`, the
compiled units are `common/*.cpp` (3 files), `renderer/*.cpp` (14 files) and
`renderer/cpu_engine/*.cpp` (13 files) — roughly 30 translation units,
confirmed by listing the source tree at the pinned SHA and excluding the
`gpu_engine/`, `bindings/`, and every loader/saver directory this option set
never compiles. A hand-authored `graphscore_rendering`-adjacent CMake target
listing exactly those files is not, by itself, an unreasonable amount of
code.

It is rejected anyway, because the file list is the smaller half of what
`meson.build` does for this configuration, not the whole of it:

- `src/meson.build` selects SIMD compiler flags per architecture
  (`-mavx`/`-mfpu=neon` vs. `/clang:-mavx`/`/clang:-mfpu=neon` for
  `clang-cl`) and toggles `-fno-exceptions -fno-rtti
  -fno-asynchronous-unwind-tables ...` specifically when
  `get_option('b_sanitize') == 'none'` — a GraphScore-authored CMake file
  would have to reproduce this branching by hand and would silently drift
  from whatever the actual upstream build does on every future pin bump,
  since nothing would force a re-read of `meson.build` at that point.
- The root `meson.build`'s `configuration_data()` block generates
  `config.h` from the exact option combination requested
  (`THORVG_CPU_ENGINE_SUPPORT`, `THORVG_PARTIAL_RENDER_SUPPORT`,
  `THORVG_THREAD_SUPPORT`, `THORVG_FILE_IO_SUPPORT`, and so on). Replicating
  this by hand means GraphScore would own a second, parallel
  option-to-macro translation layer that must be kept byte-for-byte
  consistent with what Meson would have generated — doubling the surface
  for silent drift rather than eliminating it, and doing so for the one
  dependency in this repository where every other adapter (SDL3, FreeType,
  HarfBuzz, GoogleTest) instead passes options *through* the library's own
  build system and reads results back out, never re-deriving the option
  semantics itself.
- More generally, GraphScore would become the de facto second maintainer of
  ThorVG's own build description — an obligation this repository's existing
  per-dependency-adapter pattern (`cmake/<Name>.cmake`, one thin file per
  dependency) deliberately avoids everywhere else, and Adam's own direction
  requires a concrete blocker to accept that trade. With §A1 retiring the
  Meson/Ninja blocker, no such blocker remains.

(b) is not discarded outright: it remains the documented alternative to
revisit if a future ThorVG pin introduces a real Meson-build regression
(e.g. an unavoidable network-touching `subproject()`) that (a) cannot work
around — that would be the concrete blocker Adam's bar requires.

#### A2.2 Feature/option set

Every option below is a `-D<name>=<value>` flag to `meson setup`, verified
against `meson_options.txt` and `meson.build` at the pinned SHA
`6d5933c9d1aca94635c6ad8129f3530ae554d423`:

| Option | GraphScore value | Upstream default | Rationale |
|--------|------------------|-------------------|-----------|
| `engines` | `cpu` | `cpu` | Matches upstream default. CPU software rasterizer only — no GL/WebGPU engine; no GPU API dependency, consistent with `SDL_GPU=OFF` (§1) and "no GPU needed" for M05 |
| `loaders` | _(empty)_ | `svg,lottie,ttf` | GraphScore never asks ThorVG to load SVG/Lottie/font files from disk — `graphscore_notation` produces path/glyph geometry from its own domain model, and glyph outlines come from FreeType (§5), not ThorVG's own `ttf`/`otf`/`sfnt` loader. Disabling every loader also means `png_loader`/`jpg_loader`/`webp_loader`/`lottie_loader` are all false, so none of `external_png`/`external_jpg`/`external_webp`/`lottie`'s vendored `jerryscript` subtree is ever compiled or reviewed — see §A1.3 |
| `savers` | _(empty)_ | _(empty)_ | Matches upstream default. No GIF export needed |
| `bindings` | _(empty)_ | _(empty)_ | Matches upstream default. GraphScore consumes ThorVG's native C++ API (`tvg::*`) directly from `.cpp` files (ADR 0003 §2.2); the `capi` C binding layer is never built |
| `tools` | _(empty)_ | _(empty)_ | Matches upstream default. No `svg2png`/`lottie2gif` CLI needed |
| `tests` | `false` | `false` | Matches upstream default. No ThorVG unit tests built |
| `log` | `false` | `false` | Matches upstream default. GraphScore has its own diagnostic conventions (AGENTS.md realtime-diagnostics rules for the runtime path do not apply here — `graphscore_rendering` is writer-only — but there is still no need for ThorVG's own log channel) |
| `default_library` | `static` | `shared` | **Added in this fix round — this, not `static` below, is what actually selects the produced artifact type.** `default_library` is one of Meson's own **builtin** options, not a `meson_options.txt` project option: `mesonbuild/options.py` at the Meson pin declares `UserComboOption('default_library', 'Default library type', 'shared', choices=['shared', 'static', 'both'])`. `src/meson.build` reads it directly (`lib_type = get_option('default_library')`) and passes no explicit type to its `library('thorvg-' + vmaj, ...)` call, so Meson's own default-library machinery decides shared vs. static; left at the upstream default this produces `libthorvg-1.dylib`/`.so`/`.dll`, not the `.a` archive §A2.1 step 2's `BUILD_BYPRODUCTS` names, and the `IMPORTED_LOCATION` in step 3 would point at a file that was never built. Must be passed as `-Ddefault_library=static` (same generic `-D` mechanism as every project option in this table; builtin options use it too, per the corrected `-Dwerror=false` note below) |
| `static` | `true` | `false` | **Rationale corrected in this fix round: this option has zero effect on the produced artifact under GraphScore's chosen configuration.** ThorVG's `meson_options.txt` describes it as "Force to use static linking modules in thorvg", but its only consumer is `src/loaders/meson.build`, which reads it exactly three times, each inside a loader-specific `if` block: `if png_loader: if get_option('static'): subdir('png') else: subdir('external_png') ...` (and the same pattern for `jpg_loader`/`webp_loader`) — i.e. it chooses between ThorVG's bundled codec sources and an external system codec library, and only when the corresponding loader is enabled. Because §A2.2's `loaders` row above sets `loaders=` (empty), `png_loader`/`jpg_loader`/`webp_loader` are all `false`, so none of these three `if` blocks is ever entered — `static` is read by zero live code under this option set. It is kept at `true` here only to record intent (bundled codecs if a loader is ever added later) and must not be read as controlling whether `libthorvg-1.a` is static; that is `default_library`'s job, added above |
| `libdir` | `lib` | Host-dependent | **Added, Round 1 review.** `libdir` is a Meson **builtin** option (`mesonbuild/options.py`: `UserStringOption('libdir', 'Library directory', default_libdir())`), not a `meson_options.txt` project option, and it is **not** in `BUILTIN_DIR_NOPREFIX_OPTIONS` — the small table of builtins (`sysconfdir`, `localstatedir`, `sharedstatedir`, `python.platlibdir`, `python.purelibdir`) whose resolved value can vary independently of an explicit setting. `default_libdir()` (`mesonbuild/utils/universal.py`) returns `'lib/' + archpath` (e.g. `lib/x86_64-linux-gnu`) on Debian-like hosts when `dpkg-architecture -qDEB_HOST_MULTIARCH` succeeds, `'lib64'` when a real (non-symlink) `/usr/lib64` exists, and only `'lib'` otherwise — i.e. it is a function of the build host, not of `--prefix`. Left unpinned, `ninja install` writes ThorVG's static archive to a different path on `ubuntu-latest`/`ubuntu-24.04-arm` (`lib/x86_64-linux-gnu/` or `lib/aarch64-linux-gnu/`) than the `<install-dir>/lib/libthorvg-1.a` §A2.1 step 2's `BUILD_BYPRODUCTS` and step 3's `IMPORTED_LOCATION` name. Must be passed as `-Dlibdir=lib` so the artifact path is host-independent |
| `includedir` | `include` | `include` (constant, non-Haiku) | **Added, Round 1 review.** Also a Meson builtin option (`mesonbuild/options.py`: `UserStringOption('includedir', 'Header file directory', default_includedir())`). `default_includedir()` (`mesonbuild/utils/universal.py`) returns the constant `'include'` on every platform this repository targets, so §A2.1 step 3's `<install-dir>/include/thorvg-1/` already held under the unpinned default — but only because it happened to match that default, the same exposure `libdir` has in practice. Pinned explicitly via `-Dincludedir=include` so the path does not rely on an unstated assumption about Meson's current default |
| `namingscheme` | `classic` | `classic` | **Added, Round 1 review.** A Meson builtin option (`mesonbuild/options.py`: `UserComboOption('namingscheme', 'How target file names are formed', 'classic', choices=['platform', 'classic'])`). Under `'classic'`, `StaticLibrary.determine_default_prefix_and_suffix` (`mesonbuild/build.py`) uses the unconditional `prefix = 'lib'` / `suffix = 'a'` pair on every platform including Windows, which is what makes `libthorvg-1.a` (not `thorvg-1.lib`) the produced filename on Windows — §A2.1 step 2's `BUILD_BYPRODUCTS` correction depends on this. Pinned explicitly via `-Dnamingscheme=classic` rather than left to match Meson's current default, for the same reason as `includedir` above |
| `threads` | `true` | `true` | Matches upstream default. Enables ThorVG's internal `std::thread`-based task scheduler; no external dependency, and `graphscore_rendering` carries no realtime constraint (AGENTS.md's realtime rules apply only to the `graphscore_runtime_impl` call path) |
| `simd` | `false` | `false` | Matches upstream default. Kept opt-in per §3a's original framing ("SIMD is opt-in") pending per-platform CI evaluation |
| `partial` | `true` | `true` | Matches upstream default. Internal partial-rendering optimization; no external dependency |
| `file` | `true` | `true` | Matches upstream default. No external dependency implied either way |
| `extra` | _(empty)_ | `lottie_exp,openmp` | **Explicit override of the upstream default.** `lottie_exp` is moot once `loaders` excludes `lottie` (its own guard is `lottie_loader and get_option('extra').contains('lottie_exp')`), and `openmp` is an unreviewed second threading backend beyond ThorVG's own `threads` scheduler — leaving it at the upstream default would silently require an OpenMP-capable compiler/runtime on every CI image without ever having been reviewed here |

Additional `meson setup` flags (not `meson_options.txt` entries, but part of
the invocation per §A2.1): `--backend=ninja` (forces the Ninja generator
rather than Meson's other backends); `--wrap-mode=nodownload` (defense in
depth, §A1.3); `-Dwerror=false` (**corrected from the originally drafted
`--werror=false`** — `werror` is a Meson builtin option
(`mesonbuild/options.py`: `UserBooleanOption('werror', 'Treat warnings as
errors', False)`) set through the generic `-D<name>=<value>` mechanism, not
a dedicated `--werror` flag; there is no dedicated CLI flag for it at this
Meson pin. The value matches upstream's own project-level
`default_options: werror=false` — ThorVG's own warnings are never promoted
to a GraphScore build failure, consistent with ADR 0001's "third-party
warnings are isolated" principle, and are moot regardless since GraphScore's
own `-Werror` flags never reach this separate compiler invocation);
`--buildtype=<mapped>` (**corrected in this fix round**: Meson's own
*global* builtin default is `debug`, not `debugoptimized` —
`mesonbuild/options.py`: `UserComboOption('buildtype', 'Build type to use',
'debug', choices=buildtypelist)`, confirmed at the Meson pin. `debugoptimized`
is specifically *ThorVG's own project-level override* of that global
default, set in its root `meson.build`'s `project(...)` call:
`default_options : ['buildtype=debugoptimized', 'b_sanitize=none',
'werror=false', 'optimization=3', 'cpp_std=c++14']`. Either way, Round 2
must map the active `CMAKE_BUILD_TYPE`/config generator expression to
Meson's `plain`/`debug`/`debugoptimized`/`release` buildtype explicitly,
since a separate Meson build does not automatically inherit
`CMAKE_BUILD_TYPE` the way a native CMake subproject does, and ThorVG's own
project-level default only applies when GraphScore passes no `--buildtype`
at all — which the blueprint does not do).

`find_package(Python3 3.7 COMPONENTS Interpreter REQUIRED)` must run,
**scoped to the ThorVG adapter** (e.g. inside `cmake/ThorVG.cmake` itself),
before invoking Meson, so a Python interpreter below Meson's minimum fails
CMake configure with an actionable message rather than an opaque
`SystemExit` surfacing from deep inside `meson.py`. **Correction (fix
round)**: the original draft offered raising the existing bare
`find_package(Python3 COMPONENTS Interpreter QUIET)` in
`cmake/ArchitectureAudit.cmake` to a `3.7` floor as an alternative to a
ThorVG-scoped check. That alternative is withdrawn — `cmake/ArchitectureAudit.cmake`
is `include()`d **last** from the root `CMakeLists.txt` (its own file header:
"Must come last... so every target it constrains has to exist"), after
`src/` — and therefore after ThorVG (writer-only, gated per §A7) — has
already configured and needed Python. Raising that call's floor would not
run early enough to give ThorVG's own Python requirement an actionable
error, and it would incorrectly tie a hard `3.7` requirement to the
architecture audit's own Python usage, which today tolerates absence
entirely (`Python3_Interpreter_FOUND` is optional there, with a graceful
`message(WARNING ...)` degradation path) — raising its floor to `REQUIRED`
would regress that deliberately optional design for a constraint that
belongs to ThorVG, not the audit. Round 2 must therefore add the `3.7`
`REQUIRED` check to `cmake/ThorVG.cmake` and must not modify
`cmake/ArchitectureAudit.cmake`'s existing `QUIET`, optional check.

#### A2.3 Build verification design

Round 2's `cmake/ThorVG.cmake` must follow the same "declare, then verify by
reading results back" discipline as the SDL3 M1 Verification Gate (§1):
after the `ExternalProject_Add` install step, a verification step reads
Meson's own `<build-dir>/meson-info/intro-buildoptions.json` (written by
`meson setup` for every configured option) and cross-checks every entry in
the §A2.2 table's declared value — **including `default_library`, added to
this cross-check list in this fix round alongside the option itself, and
`libdir`, `includedir`, and `namingscheme`, added to this cross-check list in
the Round 1 review alongside those three options** — against the actual
resolved value, writing full evidence to
`<build>/thorvg_option_evidence.txt` and failing the build with
`FATAL_ERROR` on any mismatch — the same two-sided check SDL3's adapter
performs (declared vs. actual, plus confirming each declared option name is
one Meson at this pin actually recognises, so a renamed or misspelled option
cannot silently do nothing while being reported "verified"). This is an
`ExternalProject_Add_Step(ThorVGBuild verify_options DEPENDEES install ...)`
custom step, since ThorVG's own configure/build happen at build time (§A1.3)
rather than at CMake configure time where SDL3's equivalent check runs.

**Added in this fix round — a derived-artifact check, not only a declared-option
readback.** The JSON cross-check above proves only that Meson's own option
store reports the values GraphScore requested; it cannot, by itself, prove
the *artifact* those options were supposed to produce actually exists in the
form expected. The `static`/`default_library` confusion corrected above is
exactly this failure mode: `intro-buildoptions.json` would have reported
`static=true`
truthfully in the original draft's design while `default_library`'s
upstream-default `shared` value silently produced a `.dylib`/`.so`/`.dll`
instead of the `.a` the `IMPORTED_LOCATION` in §A2.1 step 3 points at — a
declared-value check that never asks whether the *build actually succeeded
at producing that file* cannot catch that class of defect, only a
name/value mismatch. **The `libdir`/`namingscheme` gap corrected in the
Round 1 review is the same failure mode one level up**: `intro-buildoptions.json`
would report whatever `libdir` and `namingscheme` values were actually passed
truthfully, but if Round 2's adapter passed neither (the state this ADR was
in before the Round 1 review), Meson's own host-dependent `default_libdir()`
would have resolved silently to something other than `lib` on a Debian-like
CI image, and the JSON readback of an option GraphScore never declared has
nothing to compare against — this is precisely why §A2.2 now pins `libdir`,
`includedir`, and `namingscheme` explicitly rather than leaving Round 2 to
discover the gap only when `verify_options` fails on a specific CI image. The
`verify_options` step must therefore also assert, after cross-checking the
JSON, that the file named in `BUILD_BYPRODUCTS`
(`<install-dir>/lib/libthorvg-1.a`) exists on disk and is not, for example,
a same-named shared object with the wrong suffix left over from a prior
misconfigured run; `FATAL_ERROR` with the same evidence-file treatment as
every other mismatch on this check if it does not.

### A3. FreeType and HarfBuzz — Implementable Adapter Blueprint

This section converts §4 and §5's UNEXECUTED paper specifications into an
implementable blueprint for Round 2. It supplements those sections; the
"Constrained CMake options" tables in §4 and §5, and every other paragraph
in those sections, are unchanged and unedited above.

**Status**: FreeType remains **POLICY-CLEARED** (§5) — it requires no
adapter beyond `add_subdirectory`, unchanged. HarfBuzz remains
**PROVISIONAL** (§4) pending the adapter Round 2 implements against this
blueprint; nothing in this amendment promotes HarfBuzz to POLICY-CLEARED,
because that promotion is contingent on the empirical
`get_target_property` verification described below actually running, which
requires the CMake code this documentation-only round does not write.

**Corrected in this fix round — gating and file placement.** The original
draft of this paragraph placed both declarations "in `cmake/dependencies.cmake`
(currently containing only the GoogleTest declaration)". That file is
`include()`d from the root `CMakeLists.txt` only inside `if
(GRAPHSCORE_BUILD_TESTS) ... include(dependencies) ... endif()` — verified
directly against the current root `CMakeLists.txt`. `graphscore_rendering`
(which needs both FreeType and HarfBuzz) is a writer-only target, not a
test-only one: with `GRAPHSCORE_BUILD_TESTS=OFF` and `GRAPHSCORE_BUILD_WRITER=ON`,
placing FreeType/HarfBuzz in that file would leave `graphscore_rendering`
with no `freetype`/`harfbuzz` targets to link at all, and with
`GRAPHSCORE_BUILD_TESTS=ON` and `GRAPHSCORE_BUILD_WRITER=OFF` (the
runtime-only configuration `-DGRAPHSCORE_BUILD_WRITER=OFF` AGENTS.md
documents as fetching "no writer dependency at all") it would fetch both
into a build that has no writer target to consume them. It would also
contradict this repository's own one-adapter-file-per-dependency convention
(`cmake/SDL3.cmake` is the existing precedent AGENTS.md names for every
other dependency).

Round 2 must instead add **`cmake/FreeType.cmake`** and **`cmake/HarfBuzz.cmake`**,
one dependency per file per the established convention, and `include()` them
from the root `CMakeLists.txt` inside the existing `if (GRAPHSCORE_BUILD_WRITER)`
block that already gates `include(SDL3)` — not inside the
`GRAPHSCORE_BUILD_TESTS` gate, and not merged into `cmake/dependencies.cmake`
(which continues to hold only the `GRAPHSCORE_BUILD_TESTS`-gated GoogleTest
declaration, unchanged).

§4's enforceable invariant — HarfBuzz configured before any `freetype`
target exists — is therefore no longer "literally earlier in that one file"
as the original draft put it (there is no longer one file); it is instead an
**`include()` order invariant at the root `CMakeLists.txt` call site**:

```cmake
if (GRAPHSCORE_BUILD_WRITER)
  include(HarfBuzz)   # must precede FreeType: §4's forced-link guard
  include(FreeType)
  include(SDL3)
  include(ThorVG)     # order relative to the above is immaterial; §A7
endif()
```

CMake still processes `include()` calls in the textual order the including
script executes them, so this is the same underlying ordering mechanism the
original draft described, restated at the granularity that actually governs
it now that FreeType and HarfBuzz are separate adapter files rather than
two declarations inside one. `graphscore_rendering`'s own `CMakeLists.txt`
(added via `add_subdirectory(src)`, which the root file reaches only after
this `if (GRAPHSCORE_BUILD_WRITER)` block) is still configured only after
both adapters have run, in that order.

**The verification step, restated as an executable requirement**: after
HarfBuzz's `FetchContent_MakeAvailable` call and before FreeType's, Round 2's
`cmake/HarfBuzz.cmake` must run:

```cmake
get_target_property(_hb_link harfbuzz LINK_LIBRARIES)
if ("freetype" IN_LIST _hb_link)
  message(FATAL_ERROR "HarfBuzz unexpectedly linked to FreeType")
endif()
```

exactly as §4 specifies, immediately followed by FreeType's own
`FetchContent_Declare`/`FetchContent_MakeAvailable` with `FT_DISABLE_HARFBUZZ=ON`
set beforehand (§5's existing option table, unedited). ADR 0004 §5 already
records this precedent working: "The HarfBuzz-FreeType isolation invariant
(ADR 0002 §4) is enforced at CMake configure time:
`get_target_property` verifies HarfBuzz has no `freetype` in
`LINK_LIBRARIES`." That was proven inside the M0 spike's own CMake tree,
which is disposable and never shipped; this blueprint is what carries the
same proven invariant into the production `cmake/HarfBuzz.cmake` /
`cmake/FreeType.cmake` files Round 2 writes (root-`CMakeLists.txt`-gated per
the correction above, not `cmake/dependencies.cmake`).
`cmake/FreeType.cmake` needs no equivalent introspection check in the
reverse direction — §5 already notes FreeType's own CMakeLists.txt has no
forced-link guard analogous to HarfBuzz's, so `FT_DISABLE_HARFBUZZ=ON` alone
is sufficient there.

**HarfBuzz's own promotion to POLICY-CLEARED** happens when Round 2's
`cmake/HarfBuzz.cmake` exists, the invariant check above passes on a real
configure, and a configure-cache evidence report is produced — the same bar
SDL3's M1 Verification Gate already met for its own dependency. Until then
HarfBuzz stays PROVISIONAL, as §4 already states.

### A4. Bravura — Promoted to Default Build Dependency

**Decision (Adam, 2026-08-03)**: Bravura is promoted from the §9 spike-only
font asset to a **default build dependency** of `graphscore_rendering`,
providing the shipped SMuFL music font for Milestone 05 notation rendering.
This supersedes §9's "NOT a default build dependency" characterization;
§9's pin, license text, and evaluation are otherwise unchanged — this is the
same font at the same commit, acquired a different way (below) because it
now ships in every default build rather than only a Phase C spike
demonstration.

This is also the independent font evaluation ADR 0005 calls for: ADR 0005's
"No SMuFL font is acquired or redistributed by this spike... A future
selected font must be evaluated and noticed independently" is discharged by
this section. ADR 0005's engraving-engine spike itself required no font (its
public model carries SMuFL code points only, never glyph outlines), so
nothing in that ADR's text is superseded — this is the independent
evaluation it explicitly deferred, not a correction of it.

| Property | Value |
|----------|-------|
| Repository | https://github.com/steinbergmedia/bravura |
| Pinned commit SHA | `02e8ed29a29115df35007d1178cebaeee26c20e1` (unchanged from §9) |
| License | SIL Open Font License 1.1 |
| License URL at SHA | https://raw.githubusercontent.com/steinbergmedia/bravura/02e8ed29a29115df35007d1178cebaeee26c20e1/LICENSE.txt |
| Committed license | `docs/licenses/Bravura-OFL.txt` (already committed at §9; re-verified byte-identical against this URL on 2026-08-03) |
| License SPDX | OFL-1.1 |
| Patent grant | None (OFL 1.1; copyright and trademark only) |
| Used file | `redist/otf/Bravura.otf` at the pinned SHA |
| Artifact SHA-256 | `dca2d90c88437a701b1c2e71fa54e76f9fa41d7deee935d74dc871ea66ecfdd2`, computed 2026-08-03 by downloading the raw file at the URL above |

**Acquisition — the ADR 0004 §6 Noto Sans precedent, applied exactly**:
`file(DOWNLOAD https://raw.githubusercontent.com/steinbergmedia/bravura/02e8ed29a29115df35007d1178cebaeee26c20e1/redist/otf/Bravura.otf <dest> EXPECTED_HASH SHA256=dca2d90c88437a701b1c2e71fa54e76f9fa41d7deee935d74dc871ea66ecfdd2)`,
with a `BRAVURA_FONT_SRC` (naming it in parallel to `NOTO_SANS_SRC`) `FILEPATH`
cache-variable override for offline/air-gapped builds, exactly like Noto
Sans. This supersedes §9's git-`FetchContent`-plus-`FETCHCONTENT_SOURCE_DIR_BRAVURA`
acquisition design (a whole-repository checkout to reach one file), matching
instead ADR 0004 §6's single-artifact pattern:

- Every acquisition path — fresh `file(DOWNLOAD)`, a `BRAVURA_FONT_SRC`
  override, or a pre-existing destination file from a prior configure —
  verifies the SHA-256 above at CMake **configure** time and fails with
  `FATAL_ERROR` on any mismatch, exactly as Noto Sans's adapter does for
  every acquisition path.
- **No system-font fallback** of any kind is permitted, matching ADR 0004
  §6's explicit rule for Noto Sans. A missing or corrupted Bravura artifact
  is a configure failure, never a silent substitution.
- The `graphscore_offline_dependencies`/`FETCHCONTENT_SOURCE_DIR_<NAME>`
  machinery (AGENTS.md, `tests/repository/check_offline_dependencies.cmake`)
  is `FetchContent`-specific and does not apply to a `file(DOWNLOAD)`
  acquisition; `BRAVURA_FONT_SRC` is the offline path here, exactly as
  `NOTO_SANS_SRC` already is for Noto Sans. Neither font needs a
  `FETCHCONTENT_SOURCE_DIR_<NAME>` entry, and neither is discovered by that
  test's `_deps/*-src` glob.

  **Correction (fix round) — this is a bigger gap now than it was for Noto
  Sans, and Round 2 must close it, not merely note it.** Noto Sans is
  spike-only: a build that cannot reach the network for it is a spike
  concern, not a default-build one, so the missing coverage was an accepted,
  low-stakes gap. Bravura is a **default** build dependency as of this
  section — every `cmake --preset debug` configure now performs a
  `file(DOWNLOAD)` with no `FETCHCONTENT_FULLY_DISCONNECTED`-equivalent
  escape hatch of its own. On a genuinely air-gapped host, an ordinary
  default configure fails outright unless `BRAVURA_FONT_SRC` is already set
  — and because `check_offline_dependencies.cmake` never sets it, that test
  passes (a false green) even if a future change broke the
  `BRAVURA_FONT_SRC` override path entirely, silently regressing exactly the
  "avoid hidden network dependencies" guarantee this repository's
  cross-milestone Definition of Done requires. Round 2 must extend the
  offline-configure test to also exercise `BRAVURA_FONT_SRC`: configure its
  scratch tree with `-DBRAVURA_FONT_SRC=<the already-downloaded
  Bravura.otf from the main build tree>` (discovered the same way the test
  already discovers `FETCHCONTENT_SOURCE_DIR_<NAME>` overrides, or hand-added
  alongside them) under the same `FETCHCONTENT_FULLY_DISCONNECTED=ON`
  network-denial condition, and assert the override was honoured (no fresh
  download attempted) the same way the existing test asserts for every
  `FetchContent` dependency. Until Round 2 does this, `BRAVURA_FONT_SRC` is
  an unverified code path in a mandatory part of every default configure.

**OFL 1.1 Reserved Font Name and no-standalone-sale compliance, made
precise for a binary distribution**: §9 already records that Bravura carries
the Reserved Font Name "Bravura" (OFL 1.1 condition 3) and that the OFL
"allows the licensed fonts to be used, studied, modified and redistributed
freely as long as they are not sold by themselves" (OFL preamble, quoted
verbatim in `docs/licenses/Bravura-OFL.txt`). For a GraphScore Writer binary
distribution that embeds the unmodified `Bravura.otf` artifact:

- **Permitted**: bundling the unmodified font file inside the GraphScore
  Writer installer/application bundle as a resource the application loads
  at runtime; this is "used" and "redistributed," not "sold by itself,"
  because the font is not itself the product being sold — GraphScore Writer
  is.
- **Permitted**: retaining the exact copyright/Reserved-Font-Name notice and
  the full OFL 1.1 text alongside the distribution, satisfying the OFL's
  license-text-inclusion requirement the same way `docs/NOTICES.md` already
  does for every other font/library obligation in this repository.
- **Not permitted**: naming any GraphScore-modified derivative of the font
  file "Bravura" or any confusingly similar name without Steinberg's written
  permission (OFL condition 3) — GraphScore does not currently modify the
  font at all, so this is a forward-looking constraint on any future glyph
  patch, not a live obligation today.
- **Not permitted**: selling `Bravura.otf` by itself, unbundled from
  GraphScore Writer, as a standalone product (OFL preamble). Nothing in this
  amendment proposes doing so.

**Added in this fix round — where the font actually ships and how the
running application finds it. Corrected, Round 1 review, on the state of
the existing install tree.** The compliance statement above is necessary but
not sufficient without saying *where in the built artifact* the font and
its required notice live. The original draft of this paragraph claimed
"this repository has no existing installed-resource convention to point to
(`cmake/RuntimePackage.cmake`'s only `install(FILES ...)` call installs the
runtime's C ABI header, not a writer asset...)". That is factually wrong on
inspection of the file: `cmake/RuntimePackage.cmake` has **two**
`install(FILES ...)` calls (lines 91-96 and 98-102 at time of review), and
**neither** installs the C ABI header — that header is installed by a
separate `install(DIRECTORY ...)` call at line 68. The second `install(FILES
...)` call is exactly the missing precedent the original draft said did not
exist:

```cmake
install(FILES
  "${CMAKE_SOURCE_DIR}/LICENSE"
  "${CMAKE_SOURCE_DIR}/NOTICE"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/GraphScore"
)
```

So this repository already has working, tested machinery for shipping
license text inside an install tree — Round 2 does not start from nothing.
That destination is, however, scoped to the **runtime** package
(`GraphScoreRuntime`, ADR 0003 §3.1's closure): `graphscore_rendering`,
Bravura, and the writer executable are not part of that closure and must
never become part of the runtime's install tree or its exported package. To
be precise about why: ADR 0003 §2 and `audit_permitted_edges` govern CMake
**target edges**, and an `install(FILES)` of a text file creates no edge that
any audit would see — so this is an install-tree hygiene requirement, not a
boundary violation the mechanical audit would catch. The operative rule is
that a runtime-only install (`GRAPHSCORE_BUILD_WRITER=OFF`, under which
Bravura is never downloaded at all per §A7.1) must not ship a font license
for a font it does not ship. That gates the install rule regardless of which
destination is chosen. Round 2
must therefore add an **analogous** `install(FILES "docs/licenses/Bravura-OFL.txt"
... DESTINATION <writer-resource-location>)` call scoped to the writer
executable's own install rule (`apps/`'s `CMakeLists.txt`, which installs no
resources today) — mirroring the pattern this file already establishes, not
literally reusing `${CMAKE_INSTALL_DATAROOTDIR}/licenses/GraphScore`, which
remains reserved for GraphScore's own top-level `LICENSE`/`NOTICE` in the
runtime's install tree. The font's own license travels beside the font
itself, in the same `share/graphscore/fonts/` (or macOS bundle-resource)
location as `Bravura.otf` below, rather than in that runtime-scoped
directory: the OFL's own license-text-inclusion condition is per-font, and
placing it beside the file it licenses is the more direct way to satisfy
"provide... a copy of this license document" for whichever resource
directory ships to an end user, without implying (by proximity to
`LICENSE`/`NOTICE`) that it is a GraphScore project-level notice rather than
a third-party font's own.

- The acquired `Bravura.otf` (§A4's `file(DOWNLOAD)`/`BRAVURA_FONT_SRC`
  path) must be installed alongside the writer executable at a
  fixed, versioned, install-relative location — e.g.
  `<install-prefix>/share/graphscore/fonts/Bravura.otf` on Linux/Windows, and
  the equivalent bundle-resource location on macOS
  (`GraphScore Writer.app/Contents/Resources/fonts/Bravura.otf`, added via
  `MACOSX_BUNDLE_RESOURCE`/`RESOURCE` target properties on the writer
  executable target) — not merely left inside the build tree's
  `_deps`-adjacent download cache, which is never part of an installed or
  packaged distribution.
- `docs/licenses/Bravura-OFL.txt` (the full OFL 1.1 text and the
  Reserved-Font-Name copyright notice) must be installed alongside it in the
  same resource location, satisfying the license-text-inclusion requirement
  *inside the shipped artifact itself* — `docs/NOTICES.md` living in the
  source repository satisfies a source-distribution obligation, but a binary
  distribution's own OFL condition to "provide... a copy of this license
  document" needs the license text to travel inside that binary's own
  install tree, not only in a repository the binary's end user may never
  see.
- At runtime, the font path must be computed relative to the running
  executable's own location, not a path hardcoded to the build host's
  install prefix; a fixed absolute build-time path would break the moment
  the installed bundle is relocated, which installers routinely do.
  **Corrected, Round 1 review, on which target does this lookup**: the
  original draft attributed this to "`graphscore_rendering`/the writer shell
  ... via `SDL_GetBasePath` — already a `graphscore_writer_shell`-private SDL3
  API surface." `graphscore_rendering` has no SDL3 edge at all —
  `cmake/architecture_contract.cmake`'s
  `GRAPHSCORE_EXTERNAL_EDGES_graphscore_rendering` is `freetype harfbuzz
  thorvg`, while `GRAPHSCORE_EXTERNAL_EDGES_graphscore_writer_shell` is
  `SDL3::SDL3 SDL3::SDL3-static SDL3::SDL3-shared` — so a `graphscore_rendering`
  call to `SDL_GetBasePath` would be exactly the ADR 0003 §2 boundary
  violation `audit_permitted_edges` exists to reject. The correct division is
  the one `GRAPHSCORE_PUBLIC_EDGES_graphscore_writer_shell` already permits
  (`graphscore_writer_shell` may depend on `graphscore_rendering`, the
  reverse is not declared): `graphscore_writer_shell` resolves the base path
  via `SDL_GetBasePath` (or an equivalent platform base-path query) and
  passes the resulting font path into `graphscore_rendering`, which never
  calls SDL3 itself. No system-font-directory fallback is permitted, for the
  same reason §A4's Noto Sans precedent already forbids one: a
  silently-substituted SMuFL font would render musically incorrect glyphs
  without any visible failure.

None of this asset-installation wiring is executed by this
documentation-only round; it is specified here as a concrete Round 2
obligation the same way the CMake adapter blueprints above are.

`docs/NOTICES.md` #11 is updated in this amendment to reflect the promoted
status and the artifact-hash acquisition method (see the diff to that file;
`docs/NOTICES.md` is a living inventory, not an ADR, so it is edited in
place rather than amended alongside).

**Latin text font for production**: ADR 0004 §6 pinned Noto Sans Regular for
the *spike's* deterministic text-shaping self-tests only ("NOT a default
build dependency — pixel-level text raster assertions are spike-only").
M05's deliverables (`docs/plan/05-notation-editor.md`) are exclusively
music-notation engraving — staff systems, noteheads, accidentals, beams,
dynamics markings, and other SMuFL-glyph content — with no user-facing Latin
text rendering requirement in scope (palette labels and other UI chrome are
writer-shell concerns tracked elsewhere in the milestone plan, not
`graphscore_notation`/`graphscore_rendering`'s M05 responsibility). A
production Latin text font is therefore **deferred past M05**: there is no
M05 deliverable that needs one, and pinning one now, unused, would be a
dependency this amendment cannot justify against any concrete M05
requirement. When a milestone does need production Latin text rendering, it
must record its own font evaluation with the same rigor as this section —
immutable SHA, `EXPECTED_HASH` verification on every acquisition path, no
system-font fallback — rather than promoting Noto Sans by default; Noto
Sans's spike-only status (ADR 0004 §6) is unchanged by this amendment.

### A5. SDL3 Presentation Path

This amends the reviewed option set in §1 — the tables in §1 are otherwise
unedited above. ThorVG's `cpu` engine (§A2.2) rasterizes into a CPU-side
pixel buffer that must reach the screen through SDL3's own renderer/texture
path; `SDL_GetWindowSurface`, the API this path relies on, is itself backed
by an `SDL_Renderer` on at least Cocoa (confirmed empirically by ADR 0004
§7). §1's current `SDL_RENDER=OFF` (and every graphics-API flag it
gates) was correct for the M0 spike's Phase-A shell, which had nothing to
present; M05 does.

**Presentation API decision (Adam, 2026-08-03, this fix round)**: GraphScore
presents via GPU-backed SDL renderers. ThorVG's `cpu` engine rasterizes into
a CPU-side pixel buffer (§A2.2's `engines=cpu`); that buffer is uploaded to a
streaming `SDL_Texture` (`SDL_CreateTexture` with `SDL_TEXTUREACCESS_STREAMING`,
updated per frame via `SDL_LockTexture`/`SDL_UnlockTexture` or
`SDL_UpdateTexture`) and presented through an `SDL_Renderer` obtained from
`SDL_CreateRenderer`, backed by Metal on macOS, D3D11 on Windows, and OpenGL
on Linux. This resolves the presentation-API question the original draft of
this section left open (previously "There is no narrower Linux path to a
working `SDL_Renderer` than enabling the base OpenGL subsystem itself",
framed as if no alternative existed at all) with an explicit choice among
real alternatives, not merely the absence of an alternative: `SDL_VIDEO_RENDER_SW`
(SDL's built-in software renderer) is itself unconditionally compiled in on
every platform — `src/SDL_internal.h` at the pinned SHA:
`#if !defined(SDL_VIDEO_RENDER_SW) && !defined(SDL_LEAN_AND_MEAN)` /
`#define SDL_VIDEO_RENDER_SW 1`, gated only by the (unset, in this project's
option set) `SDL_LEAN_AND_MEAN` macro — so a working `SDL_Renderer` already
exists everywhere without enabling any GPU API at all. **The corrected
rationale for the GPU path is performance headroom** (compositing a
CPU-rasterized notation-engraving surface through a GPU-backed renderer
rather than SDL's own software blit path), consistent with Adam's direction
to enable Metal/D3D11/OpenGL specifically, not a claim that software
rendering was unavailable.

Adam's decision accepts the following costs explicitly: a wider reviewed
option closure per platform (below); GL development packages added to the
Linux CI jobs (§A7); and mandatory assertions on SDL's own *derived* build
results for the newly enabled graphics APIs, not merely readback of the
option values GraphScore itself declared (§A7 restates why the latter is
insufficient, generalizing the correction this fix round makes to the Linux
evidence below).

The graphics-API subsystem flags needed to obtain a working renderer backend
under `SDL_RENDER=ON` differ per platform, verified by source inspection of
SDL's CMakeLists.txt (and, for Linux, `cmake/sdlchecks.cmake`) at the pinned
SHA (`08b9c55393be5cb08fbec12ca431470faba3c8c9`):

| Platform | Flags flipped ON | Still OFF/deferred | Evidence |
|----------|-------------------|---------------------|----------|
| macOS (Cocoa) | `SDL_RENDER=ON`, `SDL_METAL=ON` | `SDL_GPU`, `SDL_VULKAN`, `SDL_OPENGL`, `SDL_OPENGLES` all remain `OFF` | **Corrected in the M06 SDL_METAL fix round.** The original draft recorded `SDL_METAL` remaining `OFF` and claimed the Metal render driver is gated on `SDL_RENDER` and `APPLE` only, "**not** on the separate `SDL_METAL` windowing flag" — wrong at the pinned SHA. `dep_option(SDL_RENDER_METAL "Enable the Metal render driver" ON "SDL_RENDER;APPLE" OFF)` does auto-enable the Metal render *driver* from `SDL_RENDER=ON` alone, but compilation of the render driver is not enough: at runtime `METAL_CreateRenderer` (`src/render/metal/SDL_render_metal.m`) calls `SDL_Metal_CreateView`, whose Cocoa hook (`src/video/cocoa/SDL_cocoavideo.m`: `device->Metal_CreateView = Cocoa_Metal_CreateView`) is guarded by `#ifdef SDL_VIDEO_METAL`, and `SDL_VIDEO_METAL` is set only when `SDL_METAL` is ON (`CMakeLists.txt` lines 2865-2867). With `SDL_METAL=OFF` that hook stays NULL, `SDL_Metal_CreateView` returns NULL via `SDL_Unsupported()` ("That operation is not supported"), and `METAL_CreateRenderer` fails — so `SDL_METAL=ON` is required alongside `SDL_RENDER=ON` on macOS. As on the other two platforms, the option alone is not proof of a working driver: the Metal render sources compile only behind a probe — `if(SDL_VULKAN OR SDL_METAL OR SDL_RENDER_METAL) ... check_objc_source_compiles("#import <Metal/Metal.h> ... #if (!TARGET_CPU_X86_64 && !TARGET_CPU_ARM64) #error Metal doesn't work on this configuration #endif" HAVE_FRAMEWORK_METAL) ... if(HAVE_FRAMEWORK_METAL) ... if(SDL_RENDER_METAL) sdl_glob_sources(".../src/render/metal/*.m") set(SDL_VIDEO_RENDER_METAL 1)` (`CMakeLists.txt` lines 2844-2877 at the pinned SHA). On a host where `HAVE_FRAMEWORK_METAL` fails — a Command Line Tools-only machine of the class AGENTS.md already documents for VST3, or an unsupported architecture — `SDL_RENDER_METAL` still reads `ON`, no Metal source is compiled, and presentation fails at runtime with no build-time signal; **Round 2 must assert `HAVE_FRAMEWORK_METAL`, not `SDL_RENDER_METAL` alone (§A7)**. macOS is the one platform in this table with a physical-device empirical precedent; Windows and Linux do not (see the closing paragraph) |
| Windows | `SDL_RENDER=ON`, `SDL_DIRECTX=ON` (flipped from §1's `OFF`); explicitly force `SDL_RENDER_D3D=OFF` and `SDL_RENDER_D3D12=OFF` so only `SDL_RENDER_D3D11` is active | `SDL_VULKAN`, `SDL_OPENGL`, `SDL_OPENGLES`, `SDL_GPU`, `SDL_METAL` remain `OFF` | `dep_option(SDL_RENDER_D3D11 "Enable the Direct3D 11 render driver" ON "SDL_RENDER;SDL_DIRECTX" OFF)` — the D3D11 render driver needs both `SDL_RENDER` and `SDL_DIRECTX`. Resolving to an actual working renderer additionally requires `HAVE_D3D11_H` — `if(SDL_RENDER_D3D11 AND HAVE_D3D11_H) set(SDL_VIDEO_RENDER_D3D11 1) ...` (`CMakeLists.txt` lines 2446-2449 at the pinned SHA) — a compiled probe for the Windows SDK's D3D11 header, not merely the `SDL_RENDER_D3D11` option value itself; **Round 2 must assert `SDL_VIDEO_RENDER_D3D11`/`HAVE_D3D11_H`, not `SDL_RENDER_D3D11` alone (§A7)**. `SDL_DIRECTX`'s own DirectSound *audio* compilation is separately gated by a nested `if(SDL_AUDIO)` (unrelated `if` block, same file), which stays `OFF` — flipping `SDL_DIRECTX` does not reopen §1's "audio handled by miniaudio" rationale, only the render driver it also gates. `SDL_RENDER_D3D`/`SDL_RENDER_D3D11`/`SDL_RENDER_D3D12` each default `ON` once the `SDL_RENDER;SDL_DIRECTX` guard is satisfied; explicitly forcing the legacy D3D9 and the D3D12 drivers `OFF` keeps exactly one backend active, consistent with this project's practice of deciding every option explicitly rather than accepting an upstream default silently |
| Linux (X11 + Wayland) | `SDL_RENDER=ON`, `SDL_OPENGL=ON` (flipped from §1's `OFF`) | `SDL_OPENGLES`, `SDL_VULKAN`, `SDL_GPU`, `SDL_METAL` remain `OFF` | **Corrected in this fix round.** The original draft cited "per-platform `if(SDL_OPENGL) ... set(SDL_VIDEO_RENDER_OGL 1)` blocks... representative of every platform branch" as the Linux evidence. That evidence does not exist: `set(SDL_VIDEO_RENDER_OGL 1)` appears in SDL's `CMakeLists.txt` at the pinned SHA only inside the Windows (line 2533), Apple (line 2826), Haiku (line 3004), and Vita (line 3219) platform blocks — there is no Linux/X11/Wayland branch that sets it directly. Linux instead resolves the OpenGL render driver through `cmake/sdlchecks.cmake`'s `CheckOpenGL` macro: `if(SDL_OPENGL) ... check_c_source_compiles("#include <GL/gl.h>\n#include <GL/glext.h>..." HAVE_OPENGL) ... if(HAVE_OPENGL) set(SDL_VIDEO_OPENGL 1) set(SDL_VIDEO_RENDER_OGL 1) endif() endif()` (lines ~890-905 at the pinned SHA) — a **compiled header probe**, not an unconditional `set()`. `SDL_OPENGL=ON` alone therefore proves nothing about whether the Linux build host actually has working OpenGL headers to compile against; **Round 2 must assert the derived `HAVE_OPENGL`/`SDL_VIDEO_RENDER_OGL` results, not the `SDL_OPENGL` option value (§A7)**, and must add the OpenGL development packages this probe needs to CI, which currently installs X11/Wayland/xkbcommon packages but no OpenGL headers at all (§A7) |

`SDL_GPU` (SDL3's unified next-generation GPU API) stays `OFF`/deferred
everywhere: M05's rendering need is a simple CPU-buffer-to-texture blit, not
a programmable GPU pipeline, and `SDL_GPU`'s own broader review (§1: "no GPU
needed for Phase A shell") is unaffected by this amendment.

This is a **policy decision for Round 2 to implement and verify**, not an
executed configure. §1's "M1 Verification Gate" pattern — declare, then read
every option back from the cache after `FetchContent_MakeAvailable` and
`FATAL_ERROR` on any mismatch, confirmed present in SDL's own CMakeLists so
a renamed/misspelled option cannot silently do nothing — extends to these
newly flipped options with one addition this fix round makes explicit: for
`SDL_OPENGL` (Linux), `SDL_RENDER_D3D11` (Windows), and the `SDL_METAL` +
`SDL_RENDER_METAL` pair (macOS, corrected in the M06 SDL_METAL fix round —
`SDL_METAL=ON` is required at runtime, not only `SDL_RENDER=ON`; see the
macOS row above), reading back the *option values* GraphScore itself
declared is not sufficient evidence that a working renderer resulted,
because each platform's renderer resolves through a compiled probe
(`HAVE_OPENGL`, `HAVE_D3D11_H`, `HAVE_FRAMEWORK_METAL`) that can fail
independently of the option value —
the general form of this correction is §A7's "derived vs. declared"
principle. `cmake/SDL3.cmake`'s existing verification loop already
generalizes over `GRAPHSCORE_SDL3_OPTIONS`, but Round 2 must add the probe
results (`HAVE_OPENGL`, `HAVE_D3D11_H`, `HAVE_FRAMEWORK_METAL`) to that
check, not only the option names, since the existing loop as designed reads
back exactly the variables it declared — which for each of these is the wrong
signal on its own.

The probe results are specifically the right signal to assert because they
are the only ones the checking code can actually read. `SDL_VIDEO_RENDER_OGL`,
`SDL_VIDEO_RENDER_D3D11`, and `SDL_VIDEO_RENDER_METAL` — the variables an
earlier draft of this paragraph named — are plain `set()` calls in SDL's own
directory scope at the pinned SHA, carrying neither `CACHE` nor
`PARENT_SCOPE`, and are therefore invisible to `cmake/SDL3.cmake` after
`FetchContent_MakeAvailable`. `HAVE_OPENGL`, `HAVE_D3D11_H`, and
`HAVE_FRAMEWORK_METAL` are written as **cache** entries by
`check_c_source_compiles`/`check_objc_source_compiles` and so survive into
the parent scope. Asserting an unreadable variable would silently pass on
every platform. Windows and Linux
presentation itself remains empirically **unverified** pending
physical-device testing, exactly as ADR 0004 already records for those two
platforms' windowing and accessibility gates ("documented mapping; not
implemented — deferred to physical hardware"); only macOS carries the
ADR 0004 §7 empirical precedent cited above.

### A6. Updated Status Overlay (2026-08-03)

This table restates the current status of every item this amendment
touches. It does not replace the original Summary Matrix above, which is
left as the as-accepted historical record.

| Category | Candidate | Status (2026-08-03) | Superseding section |
|----------|-----------|----------------------|----------------------|
| Vector renderer | ThorVG | **ACCEPTED** (production backend) | §A2 |
| Vector renderer fallback | Owned render-list/tessellation | Retained as documented regression path, no longer production | §A2, ADR 0003 §8 (2026-08-03 amendment), ADR 0004 §1 (2026-08-03 note) |
| Build tool | Meson | Pinned, Apache-2.0, POLICY-CLEARED as a build-tool dependency | §A1.1 |
| Build tool | Ninja | **Corrected, fix round**; floor further corrected, Round 1 review: no longer built from source; located via `find_program` with a `1.8.2` version floor (Meson's own documented minimum, not the pinned SHA's tagged version), Apache-2.0 pin retained purely as a license/provenance record | §A1.2 |
| Text shaping | HarfBuzz | PROVISIONAL (unchanged) — adapter blueprint now implementable, in its own `cmake/HarfBuzz.cmake` gated by `GRAPHSCORE_BUILD_WRITER` (corrected, fix round) | §A3 |
| Font rendering | FreeType | POLICY-CLEARED (unchanged) — adapter now specified as `cmake/FreeType.cmake` gated by `GRAPHSCORE_BUILD_WRITER` (corrected, fix round) | §A3 |
| SMuFL font | Bravura | **Default build dependency** (was spike-only, §9); offline-test coverage of `BRAVURA_FONT_SRC` is an outstanding Round 2 obligation (fix round) | §A4 |
| Text font | Noto Sans | Spike-only (unchanged, §10); production text font deferred past M05 | §A4 |
| Platform shell | SDL3 | PROVISIONAL (unchanged) — presentation-path decision made explicit (GPU-backed `SDL_Renderer` + streaming `SDL_Texture`, fix round); per-platform option decisions recorded for Round 2, including corrected Linux evidence | §A5 |
| Build-system wiring | `GRAPHSCORE_BUILD_WRITER` gating, `BUILD_TESTING` scoping, derived-vs-declared verification, Linux CI OpenGL packages | Round 2 obligations, added fix round | §A7 |

### A7. Round 2 Build-System Wiring Obligations (added, fix round, 2026-08-03)

None of the amendments above are executed CMake in this documentation-only
round; several of them depend on wiring decisions that do not belong to any
single dependency's own subsection. This section collects those
cross-cutting Round 2 obligations in one place so they are not lost inside
per-dependency prose.

**A7.1 `GRAPHSCORE_BUILD_WRITER` gate for every M05 rendering dependency.**
Nothing in §A1-§A5 as originally drafted stated where `include(ThorVG)`,
`include(HarfBuzz)`/`include(FreeType)` (§A3, corrected above), or the
Bravura `file(DOWNLOAD)` are invoked, or under what condition. Left
unstated, Round 2's naive implementation would fetch and build Meson,
locate/require Ninja, build ThorVG, and download Bravura for the
engine-integrator configuration (`-DGRAPHSCORE_BUILD_WRITER=OFF`), the
sanitizer presets (which already configure with
`-DGRAPHSCORE_BUILD_WRITER=OFF`, AGENTS.md), and the `runtime-only` CI job —
directly contradicting AGENTS.md's explicit contract: "Runtime-only build:
fetches no writer dependency at all." All four are writer-only:
`graphscore_rendering` (which needs FreeType, HarfBuzz, and ThorVG) is not
in the runtime closure AGENTS.md enumerates
(`graphscore_runtime_impl`/`graphscore_scheduler`/`graphscore_cooked_format`/
`graphscore_loader`/`graphscore_c_abi`/`graphscore_core`), and Bravura exists
only to be rendered by it. Round 2 must gate all four behind the existing
`if (GRAPHSCORE_BUILD_WRITER)` block in the root `CMakeLists.txt` (the same
block that already gates `include(SDL3)`), per §A3's corrected `include()`
ordering example. The `runtime-only` CI job's own assertion step ("Assert no
writer dependency was fetched", checking for `build/ci/_deps/sdl3-src`) is
the existing enforcement precedent Round 2 should extend to check for
`_deps/meson-src`, `_deps/thorvg-src`, and the Bravura destination path as
well, so a future regression of this gate fails CI rather than only this
ADR's own prose.

**A7.2 `BUILD_TESTING` must be a directory-scope variable, never a forced
cache entry.** §A1.2's original draft (before this fix round's larger
correction) specified `set(BUILD_TESTING OFF CACHE BOOL "" FORCE)` before
`add_subdirectory` to suppress a fetched dependency's own bundled test
suite. That specific instance is now moot (§A1.2 no longer builds Ninja from
source), but the underlying hazard is general to any *future* dependency
this repository adds that bundles its own `include(CTest)`/`if(BUILD_TESTING)`
gate, so it is recorded here rather than only inside the one subsection that
first surfaced it. The root `CMakeLists.txt` calls `include(CTest)`
(which itself runs `option(BUILD_TESTING "Build the testing tree." ON)` and
calls `enable_testing()` only when that resolves `ON`) **before**
`include(dependencies)` on every configure. Forcing the **cache** entry OFF
anywhere downstream of that point does not affect the current configure's
already-completed `enable_testing()` call, but it does corrupt the *next*
configure of the same build tree: `option()` does not overwrite an existing
cache entry, so a second configure's `include(CTest)` reads the previous
run's forced `OFF` value from `CMakeCache.txt`, skips `enable_testing()`
this time, and `ctest --preset debug` silently reports "No tests were
found" — a regression that depends on configure history, not on any file
changed between the two runs, making it exactly the kind of defect that
passes review once and breaks CI on the next unrelated re-configure.

The correct mechanism is a plain **directory-scope** `set(BUILD_TESTING
OFF)` (no `CACHE`, no `FORCE`) placed immediately before the
`FetchContent_MakeAvailable`/`add_subdirectory` call of the specific
dependency whose own tests must be suppressed, restored (`set(BUILD_TESTING
ON)`, or simply left to fall out of scope if the suppression is the last
statement before returning to the caller) immediately after. This works
because this project's `cmake_minimum_required(VERSION 3.25)` floor is
comfortably above policy `CMP0077`'s introduction — `CMP0077 NEW` (the
behavior in force at this floor) makes `option()` **not** override an
already-set normal (non-cache) variable of the same name, so the dependency's
own internal `option(BUILD_TESTING ...)` call sees the directory-scope `OFF`
and leaves it alone, without ever touching `CMakeCache.txt`. Because a plain
`set()` is scoped to the directory tree from the point it is set onward
(inherited into every `add_subdirectory` from there, but never written to
the persistent cache), it cannot leak into the next independent configure
run the way a `CACHE ... FORCE` entry does. Round 2 must scope any such
override narrowly — set immediately before, restored immediately after the
one `add_subdirectory`/`FetchContent_MakeAvailable` call it is protecting —
so it does not also suppress GraphScore's own `add_subdirectory(tests)` or
any other dependency's legitimate test configuration reached later in the
same configure pass.

**A7.3 "Derived, not declared" is the general form of a recurring
verification gap — now stated as one concrete rule, not only a heuristic.**
Four independent instances of the same defect family have now surfaced, the
third inside the fix for the first and the fourth inside the fix for the
second: §A2.3's ThorVG `static`/`default_library`
option (`meson-info/intro-buildoptions.json` would report the requested
value truthfully while the actual build artifact was the wrong type); §A5's
SDL3 `SDL_OPENGL`/`SDL_RENDER_D3D11` options (the option value GraphScore
declares can be `ON` while the compiled-probe-derived result
`HAVE_OPENGL`/`HAVE_D3D11_H` that actually determines whether a working
renderer exists resolves independently); and, found in the Round 1 review
of the fix that introduced the first instance, §A2.1/§A2.2's ThorVG
`libdir`/`includedir`/`namingscheme` — the original ThorVG fix pinned
`default_library` explicitly but still hardcoded `<install-dir>/lib/libthorvg-1.a`
against Meson's *default* `libdir`/`namingscheme` resolution rather than an
explicit setting, which is host-dependent (Debian multiarch, RHEL `lib64`)
and would have produced the identical class of silent artifact-location
failure the `static`/`default_library` fix was written to prevent. The
fourth surfaced in the delta review of the SDL3 fix: §A5's own correction
initially told Round 2 to assert `SDL_VIDEO_RENDER_OGL`,
`SDL_VIDEO_RENDER_D3D11`, and `SDL_RENDER_METAL` — but the first two are
plain directory-scope `set()` calls in SDL carrying neither `CACHE` nor
`PARENT_SCOPE` (unreadable from `cmake/SDL3.cmake`, so the assertion would
have passed vacuously on every platform), and the third is the *option*
rather than its probe result. The readable, correct signals are the
`check_*_source_compiles` cache entries `HAVE_OPENGL`, `HAVE_D3D11_H`, and
`HAVE_FRAMEWORK_METAL`. Asserting a derived value is only half the rule;
the other half is that the value must be **readable from the scope doing
the asserting**.

The rule, stated once rather than left as a pattern to notice by example:
**every path, filename, and directory an adapter hardcodes against a
third-party build system's default must be pinned by an explicit option
passed to that build system (e.g. ThorVG/Meson's `-Dlibdir`,
`-Dincludedir`, `-Ddefault_library`, `-Dnamingscheme`), never assumed to
equal the build system's current default merely because it does today.** A
default is a property of the tool's own version and the host it runs on,
not a property this ADR pins; only an explicitly passed option is. This
applies to every adapter in this repository that drives a third-party
build system as a separate process (currently ThorVG/Meson; a future
dependency added the same way inherits the same rule), not only to option
*values* the SDL3/ThorVG option-readback checks already cover — it extends
that same discipline to *paths derived from unstated defaults*, which is a
distinct failure mode a value-readback check does not catch by construction
(the JSON or cache entry reports the default faithfully; the defect is that
nothing pinned the default in the first place). Round 2 must audit every
hardcoded path in `cmake/ThorVG.cmake` against this rule before landing it,
not only the four instances already found and fixed here.

One acknowledged limit of the rule as phrased: it cannot apply to paths a
third-party build system provides no option to pin. Meson's
`<build-dir>/meson-info/intro-buildoptions.json` is the standing example —
its location is fixed by Meson's own introspection layout, with no
corresponding `-D` setting, so it is pinned by the Meson SHA alone. Where no
option exists, the fallback is an explicit existence check that fails loudly
(as §A2.3's artifact-existence check does), never a silent assumption.

**A7.4 Linux CI OpenGL packages, named exactly.** Both Linux CI jobs that
currently install SDL3's windowing build dependencies — the `test` job's
"Install Linux toolchain and SDL3 build dependencies" step
(`.github/workflows/ci.yml` lines 169-177) and the `cross-build` job's
identically named step (lines 252-260) — install X11, Wayland, and
`xkbcommon` development packages but no OpenGL development headers. Round 2
must add `libgl-dev` and `mesa-common-dev` (the standard Ubuntu/Debian
packages providing `GL/gl.h`/`GL/glext.h` and the system OpenGL loader) to
both jobs' `apt-get install` package lists, so `cmake/sdlchecks.cmake`'s
`CheckOpenGL` compiled probe (§A5) succeeds instead of silently leaving
`SDL_VIDEO_RENDER_OGL` unset while `SDL_OPENGL=ON` is still reported as
"resolved" by a check that only reads the option back.

**A7.5 ThorVG's `cross/` directory needs no action today.** The original
draft of §A2.1 listed ThorVG's `cross/` directory among the tree's top-level
contents without returning to it. At the pinned SHA it holds only
Android/iOS Meson cross-files. `.github/workflows/ci.yml`'s `cross-build`
job (lines 235-247) — despite its name — runs on **native**
`ubuntu-24.04-arm` and `windows-11-arm` GitHub-hosted runners, not
cross-compilation from an x86-64 host; Meson needs no cross-file for a
native build regardless of CPU architecture. No Meson cross-file is
required for any target this repository currently builds. This is recorded
here as the closing answer to a dangling reference, not a Round 2 action
item — revisit only if a future milestone adds genuine cross-compilation
(e.g. building Windows arm64 binaries from an x86-64 host).

**A7.6 AGENTS.md must be updated in the same change that implements this
amendment.** AGENTS.md's own header requires it to be updated "whenever a
later milestone changes a command, a target name, or a non-obvious
constraint... in the same change." Round 2's CMake bring-up changes several
things AGENTS.md documents: the Python build-host floor (3.7+, for Meson,
scoped to `cmake/ThorVG.cmake` per §A2.2's correction), the ThorVG
build-time (not configure-time) compilation model, and the Linux OpenGL
development packages (§A7.4). This documentation-only round does not modify
AGENTS.md itself — no command, target, or constraint changes yet, since no
CMake exists yet — but Round 2 must not treat that omission as license to
skip the AGENTS.md update when it lands the code these obligations describe.
