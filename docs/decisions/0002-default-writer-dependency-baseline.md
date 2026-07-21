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
| `SDL_METAL` | `OFF` | Deferred |
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

##### M1 Verification Gate

A `cmake/SDL3.cmake` adapter must be implemented in M1. The M1
deliverable must include:
- A complete configure-cache file proving every option in the reviewed
  tables above resolves to the declared value.
- Explicit decisions and cache evidence for every remaining unaudited
  default listed in §Remaining Unaudited Defaults.
- Explicit verification that `SDL_PIPEWIRE=OFF` takes effect on Linux
  (the option defaults ON and has no `SDL_AUDIO` guard).
SDL3 remains PROVISIONAL until this full audit is completed.

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
