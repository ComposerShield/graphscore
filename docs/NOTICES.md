# GraphScore Third-Party Notices

This file records notices and licensing information for all third-party
dependencies that may be included in the default build of GraphScore.
It satisfies the attribution and notice obligations of each dependency
and is the authoritative license inventory.  All pins are immutable
40-character lowercase hex commit SHAs.  License-file links are
SHA-pinned to the exact source tree.

Full license texts are committed under `docs/licenses/` named by
dependency, fetched from each recorded SHA.  This file is the
source-repository license inventory, kept for every dependency this
repository has evaluated — including ones that are excluded, deferred, or
not linked into any shipped artifact.  A binary distribution satisfies the
same review's obligations by shipping the narrower, per-artifact notice set
below, not this file itself: reproducing the full inventory inside every
shipped artifact would carry entries (e.g. miniaudio, PortAudio, RtMidi,
Corrosion, GoogleTest) that artifact never links.

## Shipped artifact layout

`cmake --install` installs two independent, artifact-scoped notice sets,
neither of which is this file:

- `share/licenses/GraphScore/` — GraphScore's own top-level `LICENSE` and
  `NOTICE` (Apache-2.0). Installed by every build regardless of
  `GRAPHSCORE_BUILD_WRITER` (`cmake/RuntimePackage.cmake`); this is what a
  runtime-only consumer of the exported `GraphScoreRuntime` CMake package
  receives.
- `share/licenses/GraphScoreWriter/` — the license text of every
  third-party dependency `graphscore_writer_app` actually links (ThorVG,
  HarfBuzz, SDL3, and FreeType's own two license files) plus Bravura's OFL
  text, and `NOTICE` — GraphScore's own aggregated attribution statement
  (`docs/NOTICE-writer.txt`, installed under that name), which discharges
  FreeType FTL §2's binary-distribution disclaimer obligation (see §5
  below). Installed only when `GRAPHSCORE_BUILD_WRITER=ON`
  (`apps/CMakeLists.txt`); a runtime-only install ships none of it, matching
  that none of these dependencies are fetched in that configuration
  (ADR 0002 §A7.1).
- `share/graphscore/fonts/` — the Bravura SMuFL font, installed as
  `Bravura.otf` under that fixed name regardless of its acquisition path,
  and its own `Bravura-OFL.txt`. Also writer-only.

`tests/cmake/run_consumer_test.cmake` asserts the exact file set at each
destination, under both `GRAPHSCORE_BUILD_WRITER` values.

## Status Definitions

| Status | Meaning |
|--------|---------|
| **POLICY-CLEARED** | License, transitive dependency, patent, and redistribution review complete from authoritative source documents. No empirical build or platform verification has been performed. |
| **PROVISIONAL** | Policy-cleared, but a build-system adapter, platform spike, or unresolved blocker prevents final acceptance. |
| **EXCLUDED** | Explicitly excluded from the default build. Reason and blocker recorded. License file committed for reference; obligations are not currently active but are noted for when the dependency is re-evaluated. |
| **DEFERRED** | Evaluation belongs to a later milestone phase. |

Each dependency records only the enabled transitive closure that would
be active in the GraphScore default build.  Optional paths disabled by
CMake configuration are documented as excluded.

---

## 1. SDL3 — Windowing, Input, GPU Abstraction, Audio Device

| Field | Detail |
|-------|--------|
| **Status** | **PROVISIONAL** — license-cleared (zlib) but the comprehensive CMake option set (ADR 0002 §1) is an UNEXECUTED adapter specification for M1, not an applied configuration. Requires configure-cache evidence report in M1 verifying every option resolves correctly, including explicit verification that `SDL_PIPEWIRE=OFF` takes effect on Linux (it defaults ON without an `SDL_AUDIO` dependency guard at the pinned SHA). |
| **Repository** | https://github.com/libsdl-org/SDL |
| **Pinned SHA** | `08b9c55393be5cb08fbec12ca431470faba3c8c9` |
| **License** | zlib License |
| **License file at SHA** | https://raw.githubusercontent.com/libsdl-org/SDL/08b9c55393be5cb08fbec12ca431470faba3c8c9/LICENSE.txt |
| **Committed license** | `docs/licenses/SDL3-LICENSE.txt` |
| **SPDX** | Zlib |
| **Patent grant** | None (zlib license contains no patent clause) |
| **Required notices** | None mandatory. Acknowledgment in product documentation is appreciated but not required. The zlib license notice itself must be retained in source distributions. |
| **Specified closure (UNEXECUTED — M1 adapter required)** | None vendored. All retained backends (Cocoa, Wayland, X11) are platform OS services / system SDKs. SDL_DIRECTX, SDL_KMSDRM, and all audio subsystems disabled (see ADR 0002 §1). HIDAPI disabled (BSD 3-clause inventoried but no consuming subsystem enabled). All unreviewed optional integrations explicitly OFF: PipeWire, ALSA, PulseAudio, JACK, sndio, OSS, Fribidi, libthai, D-Bus, IBus, liburing, libudev, Raspberry Pi video, Rockchip video, Vivante video, Wayland libdecor. X11 extensions audited and explicitly decided per ADR 0002 §1. SDL_OPENGLES, SDL_DUMMYVIDEO, SDL_OFFSCREEN, SDL_SYSTEM_ICONV, SDL_DLOPEN_NOTES, and SDL_XINPUT all explicitly OFF. SDL3 `src/hidapi/` directory at this SHA is tri-licensed (GPLv3 / BSD 3-clause / original HIDAPI); GraphScore selects BSD 3-clause when evaluating. `src/hidapi/LICENSE-bsd.txt`, `LICENSE-gpl3.txt`, and `LICENSE-orig.txt` are present in the SDL3 source tree at the pinned SHA. No `src/3rdparty/` directory exists at this SHA. Full option set and citation of default sources documented in ADR 0002 §1. Residual risk: `SDL_PIPEWIRE` is `set_option(${UNIX_SYS})` with no `SDL_AUDIO` dependency guard (line ~480 of CMakeLists.txt at pinned SHA) — defaults ON on Linux regardless of `SDL_AUDIO=OFF`; the specification forces it OFF but this must be confirmed via M1 configure-cache. |
| **Distribution** | Redistributable in source and binary form provided the zlib license notice is retained (`docs/licenses/SDL3-LICENSE.txt`). |
| **Presentation path (2026-08-03, corrected in fix round)** | GraphScore presents via GPU-backed SDL renderers: ThorVG's CPU-rasterized buffer is uploaded to a streaming `SDL_Texture` and presented through `SDL_CreateRenderer`, backed by Metal (macOS), D3D11 (Windows), and OpenGL (Linux). `SDL_RENDER` flips `ON` for M05 on every platform; `SDL_DIRECTX` (Windows) and `SDL_OPENGL` (Linux) also flip `ON` from the `OFF` values recorded in the closure above, superseding those two specific entries — the rest of the specified closure row above (PipeWire/ALSA/PulseAudio/JACK/etc., X11 extensions, HIDAPI) is unaffected. Per-platform graphics-API flags needed to obtain a renderer backend under `SDL_RENDER=ON`, and the ADR evidence for each, are recorded in ADR 0002 §A5. This does not change the PROVISIONAL status above; it is a Round 2 implementation item against that section. |

---

## 2. AccessKit — Accessibility Bridge

| Field | Detail |
|-------|--------|
| **Status** | EXCLUDED from default build (PROVISIONAL — pending Phase C spike) |
| **Repository** | https://github.com/AccessKit/accesskit-c |
| **Pinned SHA** | `826d672661f9453c8b269ab3946dbcbae6300555` (release 0.22.3) |
| **License** | MIT OR Apache-2.0 (user's choice) |
| **License file at SHA (Apache)** | https://raw.githubusercontent.com/AccessKit/accesskit-c/826d672661f9453c8b269ab3946dbcbae6300555/LICENSE-APACHE |
| **License file at SHA (MIT)** | https://raw.githubusercontent.com/AccessKit/accesskit-c/826d672661f9453c8b269ab3946dbcbae6300555/LICENSE-MIT |
| **License file at SHA (Chromium)** | https://raw.githubusercontent.com/AccessKit/accesskit-c/826d672661f9453c8b269ab3946dbcbae6300555/LICENSE.chromium |
| **Committed licenses** | `docs/licenses/AccessKit-LICENSE-MIT.txt`, `docs/licenses/AccessKit-LICENSE-APACHE.txt`, `docs/licenses/AccessKit-LICENSE-chromium.txt` |
| **Patent grant** | Via Apache-2.0 option (if selected) |
| **Required notices** | Copyright The AccessKit contributors. Chromium-derived code carries a BSD-style notice — see `docs/licenses/AccessKit-LICENSE-chromium.txt`. Not currently active (dependency excluded). |

**Corrosion and mutable fetch**:

The accesskit-c CMakeLists.txt at the pinned SHA (line 26) fetches
Corrosion at `GIT_TAG v0.6.1` — a mutable git tag. The actual
immutable SHA at this tag is `1499b14e4906a2890f5cee1547c8848db261753d`
(verified via GitHub API at `refs/tags/v0.6.1`). Corrosion is MIT-licensed
(`docs/licenses/Corrosion-LICENSE.txt`;
license source at pinned SHA:
https://raw.githubusercontent.com/corrosion-rs/corrosion/1499b14e4906a2890f5cee1547c8848db261753d/LICENSE).
AccessKit remains excluded until
the mutable fetch is patched, the Rust toolchain requirement is
resolved, and a formal Cargo.lock offline audit (`cargo vendor` +
`cargo-deny`) is completed.

**Transitive closure — Rust crates (from Cargo.lock)**:

All direct and platform-specific AccessKit crates are MIT OR
Apache-2.0. Initial review of the 80+ Cargo.lock transitives identifies
no GPL/LGPL Rust crates. Formal audit incomplete.

**Meson build files**: LGPL-2.1 — applies only to build-system files,
not the library binary.

**Blocker for default build**: Rust toolchain requirement, mutable
Corrosion fetch, and incomplete Cargo.lock audit. Fallback is
platform-native accessibility APIs.

---

## 3. ThorVG — Backend-Neutral Vector Renderer

| Field | Detail |
|-------|--------|
| **Status** | **ACCEPTED** — production rasterization backend for `graphscore_rendering` (ADR 0002 §A2, 2026-08-03 amendment). All three former acceptance blockers below are retired; the CMake adapter (`cmake/ThorVG.cmake`) and its empirical validation are implemented in a later M05 round against the blueprint in that amendment. |
| **Repository** | https://github.com/thorvg/thorvg |
| **Pinned SHA** | `6d5933c9d1aca94635c6ad8129f3530ae554d423` (2026-07-18) |
| **License** | MIT |
| **Committed license** | `docs/licenses/ThorVG-LICENSE.txt` |
| **SPDX** | MIT |
| **Patent grant** | None (MIT license contains no patent clause) |
| **Required notices** | Copyright (c) 2020-2026 ThorVG Project. MIT notice must appear in all copies. |
| **Build system** | Meson only at the pinned SHA. No CMakeLists.txt provided. Driven via a Meson-driven `ExternalProject_Add`, consumed as an `IMPORTED` static library — see ADR 0002 §A2.1 for the integration mechanism and the rejected GraphScore-authored-CMake alternative. |
| **Enabled feature set** | `default_library=static` (the option that actually selects the produced artifact type — `libthorvg-1.a`, not `libthorvg-1.dylib`/`.so`/`.dll`), `libdir=lib`, `includedir=include`, `namingscheme=classic` (all three pin Meson's own build-system defaults explicitly, rather than relying on them, so the installed archive path/filename is host-independent — Round 1 review correction), `engines=cpu`, `loaders=`, `savers=`, `bindings=`, `tools=`, `tests=false`, `log=false`, `static=true` (kept only to record intent; read by zero live code once every loader is disabled — `default_library` above, not this option, controls whether the archive is static), `threads=true`, `simd=false`, `partial=true`, `file=true`, `extra=` (ADR 0002 §A2.2, corrected in the fix round and Round 1 review). No optional loader (FreeType/libpng/libjpeg-turbo/libwebp, or ThorVG's vendored `jerryscript` Lottie-expression engine) is enabled. |
| **Former acceptance blockers (retired 2026-08-03)** | (1) Meson and Ninja toolchain — pinned and licensed in ADR 0002 §A1 (both Apache-2.0). (2) `cmake/ThorVG.cmake` adapter — blueprint specified in ADR 0002 §A2.1–§A2.3; implementation is a later M05 round. (3) Empirical validation of enabled features, binary size, and platform compatibility — remains a build-gate requirement for that same round. |
| **Distribution** | MIT terms apply to the ThorVG library itself. |
| **Fallback** | Owned toolkit-neutral vector render-list/tessellation layer, retained as the documented regression path if a future ThorVG pin regresses the build gate (ADR 0002 §3b, ADR 0003 §8). |

---

## 4. HarfBuzz — Text Shaping

| Field | Detail |
|-------|--------|
| **Status** | **PROVISIONAL** — license-cleared (Old MIT) but the `cmake/HarfBuzz.cmake` adapter and its isolation-order invariant (ADR 0002 §4) are UNEXECUTED M1 requirements. The forced-FreeType `if(TARGET freetype)` guard in the upstream CMakeLists.txt at the pinned SHA must be suppressed via adapter ordering and verified with `get_target_property` introspection. |
| **Repository** | https://github.com/harfbuzz/harfbuzz |
| **Pinned SHA** | `af192b7e0f49a9965220ba3f18473e3f8e28b8b9` |
| **License** | Old MIT (MIT with no-endorsement clause) |
| **License file at SHA** | https://raw.githubusercontent.com/harfbuzz/harfbuzz/af192b7e0f49a9965220ba3f18473e3f8e28b8b9/COPYING |
| **Committed license** | `docs/licenses/HarfBuzz-COPYING.txt` |
| **SPDX** | MIT |

**Required notices — verbatim from COPYING**:

The following copyright notice and the two disclaimer paragraphs must
appear in all copies of this software (the full text is committed at
`docs/licenses/HarfBuzz-COPYING.txt`):

```
Copyright © 2010-2022  Google, Inc.
Copyright © 2015-2020  Ebrahim Byagowi
Copyright © 2019,2020  Facebook, Inc.
Copyright © 2012,2015  Mozilla Foundation
Copyright © 2011  Codethink Limited
Copyright © 2008,2010  Nokia Corporation and/or its subsidiary(-ies)
Copyright © 2009  Keith Stribley
Copyright © 2011  Martin Hosken and SIL International
Copyright © 2007  Chris Wilson
Copyright © 2005,2006,2020,2021,2022,2023  Behdad Esfahbod
Copyright © 2004,2007,2008,2009,2010,2013,2021,2022,2023  Red Hat, Inc.
Copyright © 1998-2005  David Turner and Werner Lemberg
Copyright © 2016  Igalia S.L.
Copyright © 2022  Matthias Clasen
Copyright © 2018,2021  Khaled Hosny
Copyright © 2018,2019,2020  Adobe, Inc
Copyright © 2013-2015  Alexei Podtelezhnikov

Permission is hereby granted, without written agreement and without
license or royalty fees, to use, copy, modify, and distribute this
software and its documentation for any purpose, provided that the
above copyright notice and the following two paragraphs appear in
all copies of this software.

IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
```

**Constrained CMake options** (verified at the pinned SHA CMakeLists.txt):

| Option | Value | Rationale |
|--------|-------|-----------|
| `HB_HAVE_FREETYPE` | `OFF` | FreeType's HarfBuzz integration is also OFF — avoid circular dependency. HarfBuzz can be consumed by FreeType separately when FreeType is selected. |
| `HB_HAVE_CORETEXT` | `OFF` | Platform-specific shapers disabled for deterministic cross-platform build. Verified: `option(HB_HAVE_CORETEXT ... ON)` exists at `APPLE` guard in CMakeLists.txt at pinned SHA. |
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

| **Patent grant** | None (MIT-style) |
| **Transitive closure** | Core library only. No optional deps enabled. FreeType closure: HarfBuzz CMakeLists.txt at the pinned SHA contains a non-overridable `if(TARGET freetype)` guard that silently flips `HB_HAVE_FREETYPE=ON` and links the FreeType target. The `cmake/HarfBuzz.cmake` adapter (UNEXECUTED M1 requirement) must enforce isolation by configuring HarfBuzz before any `freetype` target exists, then verifying via `get_target_property` that no FreeType link exists. See ADR 0002 §4 for the full enforceable invariant and M1 verification gate. |
| **Distribution** | Must include `docs/licenses/HarfBuzz-COPYING.txt` in all copies. |

---

## 5. FreeType — Font Loading and Glyph Rasterization

| Field | Detail |
|-------|--------|
| **Status** | POLICY-CLEARED |
| **Repository** | https://github.com/freetype/freetype |
| **Pinned SHA** | `f01dec5e676847267834b881b25f6e8c79581163` |
| **License** | FTL (FreeType License). Dual-licensed: FTL or GPLv2 (user's choice). GraphScore selects FTL. |
| **License file at SHA** | https://raw.githubusercontent.com/freetype/freetype/f01dec5e676847267834b881b25f6e8c79581163/docs/FTL.TXT |
| **Committed license** | `docs/licenses/FreeType-FTL.TXT` |
| **SPDX** | FTL |

**Required binary-distribution disclaimer — verbatim from FTL §2**:

> Redistribution in binary form must provide a disclaimer that states
> that the software is based in part of the work of the FreeType Team,
> in the distribution documentation.

The following text must appear in documentation accompanying any
binary distribution of GraphScore that includes FreeType:

```
This software is based in part of the work of the FreeType Team.
```

Additionally, the preferred credit form from the FTL preamble:

```
Portions of this software are copyright © <year> The FreeType
Project (https://freetype.org).  All rights reserved.
```

(Replace `<year>` with the FreeType version actually used.)

**Patent grant** | FreeType Patents Grant (https://freetype.org/patents.html): FreeType-related patents held by FreeType developers are freely licensed for use with FreeType. No general patent grant from all contributors.

**Constrained CMake options — deterministic closure** (verified at pinned SHA CMakeLists.txt):

| Option | Value | Rationale |
|--------|-------|-----------|
| `FT_DISABLE_ZLIB` | `ON` | Use internal zlib; no system dependency |
| `FT_DISABLE_BZIP2` | `ON` | No BZip2 compressed font support |
| `FT_DISABLE_PNG` | `ON` | No PNG-embedded OpenType bitmap support |
| `FT_DISABLE_HARFBUZZ` | `ON` | HarfBuzz auto-hinting disabled; avoids circular dependency (HarfBuzz may consume FreeType separately) |
| `FT_DISABLE_BROTLI` | `ON` | No WOFF2 compressed font support |
| `FT_DISABLE_HVF` | `ON` | HVF is Apple-only; disable for portability |
| All `FT_REQUIRE_*` | `OFF` | No hard requirements on system libraries |

This produces a fully self-contained FreeType build with zero optional
system library dependencies. HarfBuzz integration is off in both
directions — if HarfBuzz is separately selected, it may consume FreeType
through its own CMake configuration. M1 can loosen constraints on a
per-platform basis with documented rationale.

**Enabled transitive: FreeType-bundled zlib 1.3.1**:

Because `FT_DISABLE_ZLIB=ON`, FreeType compiles its bundled zlib 1.3.1
from `src/gzip/zlib.h`. This is the standard zlib implementation under
the zlib license, included here as an enabled transitive component of
the FreeType dependency at the same pinned SHA.
`docs/licenses/FreeType-zlib-license.txt` was extracted from the header
file (the license block only; the complete header is 97,499 bytes).
Source URL at pinned SHA:

https://raw.githubusercontent.com/freetype/freetype/f01dec5e676847267834b881b25f6e8c79581163/src/gzip/zlib.h

| Field | Detail |
|-------|--------|
| License | zlib License |
| Copyright | (C) 1995-2024 Jean-loup Gailly and Mark Adler |
| Notice | Acknowledgment in product documentation is appreciated but not required. The zlib license notice itself must be retained in source distributions. |
| Distribution | Redistributable in source form with the notice retained. |

| **Transitive closure** | Core library + bundled zlib 1.3.1 (zlib license, version confirmed at pinned SHA). All optional system deps disabled. HarfBuzz integration disabled (`FT_DISABLE_HARFBUZZ=ON`). HarfBuzz is configured in isolation first (see HarfBuzz §4 transitive closure); both directions of the circular dependency are severed. No external deps active. |
| **Distribution** | Must include `docs/licenses/FreeType-FTL.TXT`, `docs/licenses/FreeType-zlib-license.txt`, and the binary-distribution disclaimer above. FTL §2 draws two distinct obligations: the source-distribution bullet is discharged by retaining `FreeType-FTL.TXT` unaltered (above); the *binary*-distribution bullet requires an affirmative disclaimer statement authored by the redistributor — retaining the license file alone does not satisfy it. In the shipped writer artifact, `docs/NOTICE-writer.txt` is that statement: it is installed as `NOTICE` under `share/licenses/GraphScoreWriter/` (`apps/CMakeLists.txt`), carrying the exact disclaimer and preferred-credit text above with `<year>` resolved to `2026` (the pinned SHA's FreeType 2.14.3 headers carry `Copyright (C) 1996-2026`). |

---

## 6. miniaudio — Audio Device (Primary)

| Field | Detail |
|-------|--------|
| **Status** | **PROVISIONAL** — license-cleared (Unlicense, no mandatory attribution) with zero vendored dependencies, but the CMake adapter (`cmake/miniaudio.cmake`), warning-isolation translation unit, and `MA_ENABLE_ONLY_SPECIFIC_BACKENDS` per-platform backend selection are UNEXECUTED M1 requirements. No empirical build or audio-device test has been performed. Full device validation deferred to Phase C. |
| **Repository** | https://github.com/mackron/miniaudio |
| **Pinned SHA** | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` (v0.11.25) |
| **License** | Public Domain (Unlicense) OR MIT-0. GraphScore selects Unlicense. |
| **License file at SHA** | https://raw.githubusercontent.com/mackron/miniaudio/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d/LICENSE |
| **Committed license** | `docs/licenses/miniaudio-LICENSE.txt` |
| **SPDX** | Unlicense OR MIT-0 |
| **Patent grant** | None |
| **Required notices** | No mandatory attribution. |
| **Transitive closure** | None vendored. All audio backends (CoreAudio, WASAPI, DirectSound, WinMM, ALSA, PulseAudio, JACK) are platform OS APIs / system-installed libraries. Wayland/X11 is N/A: audio-device libraries operate below the windowing system. |
| **Distribution** | Unrestricted. |

---

## 7. PortAudio — Audio Device (Fallback)

| Field | Detail |
|-------|--------|
| **Status** | PROVISIONAL fallback (not in default closure) |
| **Repository** | https://github.com/PortAudio/portaudio |
| **Pinned SHA** | `f88b5841575b43bfa024a6861635b69d7eb98d6d` |
| **License** | MIT |
| **License file at SHA** | https://raw.githubusercontent.com/PortAudio/portaudio/f88b5841575b43bfa024a6861635b69d7eb98d6d/LICENSE.txt |
| **Committed license** | `docs/licenses/PortAudio-LICENSE.txt` |
| **SPDX** | MIT |
| **Patent grant** | None |
| **Required notices** | Copyright (c) 1999-2006 Ross Bencina and Phil Burk. MIT notice must be retained. Obligations not currently active (PROVISIONAL fallback). |
| **Transitive closure** | ASIO SDK **excluded** (proprietary Steinberg license, `PA_USE_ASIO=OFF`). All retained audio backends (CoreAudio on macOS; MME, DirectSound, WASAPI, WDM-KS on Windows; ALSA on Linux) are platform OS APIs / system-installed libraries. `PA_USE_WDMKS_DEVICE_INFO=ON` on Windows for device information enumeration. PulseAudio and JACK are OFF in the default Linux specification (future opt-in behind explicit GraphScore options). JACK is explicitly OFF on macOS to prevent accidental detection via Homebrew. All options explicitly decided per ADR 0002 §6b. Wayland/X11 N/A. |
| **Distribution** | MIT terms. Obligations not currently active. |

---

## 8. RtMidi — External MIDI Port I/O

| Field | Detail |
|-------|--------|
| **Status** | PROVISIONAL (external device I/O only; not in default closure) |
| **Repository** | https://github.com/thestk/rtmidi |
| **Pinned SHA** | `a3233c22949342f6697681e2cf2403e27fcf0c9e` |
| **License** | MIT |
| **License file at SHA** | https://raw.githubusercontent.com/thestk/rtmidi/a3233c22949342f6697681e2cf2403e27fcf0c9e/LICENSE |
| **Committed license** | `docs/licenses/RtMidi-LICENSE.txt` |
| **SPDX** | MIT |
| **Patent grant** | None |
| **Required notices** | Copyright (c) 2003-2023 Gary P. Scavone. MIT notice must be retained. Obligations not currently active (PROVISIONAL). |
| **Transitive closure** | None vendored. All MIDI backends (CoreMIDI, WinMM, ALSA sequencer, JACK) are platform OS APIs / system-installed libraries. `RTMIDI_API_ALSA` must be explicitly set `ON` on Linux (the pinned default `${ALSA}` does not auto-detect). `RTMIDI_API_JACK` must be explicitly `OFF` on macOS/Windows (the pinned default finds JACK on any OS). All API flags explicitly decided per ADR 0002 §7a. Wayland/X11 N/A: MIDI libraries operate below the windowing system. |
| **Distribution** | MIT terms. Obligations not currently active. |

---

## 9. VST3 SDK — Plugin Host

| Field | Detail |
|-------|--------|
| **Status** | DEFERRED to M0 Phase D |
| **Repository** | https://github.com/steinbergmedia/vst3sdk |
| **SHA** | To be pinned in M0 Phase D spike |
| **License** | MIT (SDK code). VST3 license agreement applies for plugin distribution. |

---

## 10. MIDI Message Encoding — Owned Code

| Field | Detail |
|-------|--------|
| **Status** | POLICY-CLEARED (GraphScore-owned code) |
| **License** | Apache-2.0 (same as GraphScore) |

---

## 11. Bravura — SMuFL Music Font

| Field | Detail |
|-------|--------|
| **Status** | **Default build dependency** (OFL 1.1), promoted from spike-only in ADR 0002 §A4 (2026-08-03 amendment). Bravura is the shipped SMuFL music font for Milestone 05 notation rendering. |
| **Repository** | https://github.com/steinbergmedia/bravura |
| **Pinned SHA** | `02e8ed29a29115df35007d1178cebaeee26c20e1` |
| **License** | SIL Open Font License 1.1 |
| **License file at SHA** | https://raw.githubusercontent.com/steinbergmedia/bravura/02e8ed29a29115df35007d1178cebaeee26c20e1/LICENSE.txt |
| **Committed license** | `docs/licenses/Bravura-OFL.txt` |
| **SPDX** | OFL-1.1 |
| **Patent grant** | None (OFL 1.1; copyright and trademark only) |
| **Required notices** | Copyright © 2019, Steinberg Media Technologies GmbH, with Reserved Font Name "Bravura". The OFL 1.1 notice must be included. Condition 3 of the OFL restricts use of the Reserved Font Name "Bravura" in modified versions without written permission; GraphScore does not modify the font. The OFL preamble's "not sold by themselves" condition permits bundling the unmodified font inside the GraphScore Writer distribution but forbids selling `Bravura.otf` standalone — see ADR 0002 §A4 for the full compliance statement. |
| **Used file** | `redist/otf/Bravura.otf` at pinned SHA |
| **Artifact SHA-256** | `dca2d90c88437a701b1c2e71fa54e76f9fa41d7deee935d74dc871ea66ecfdd2` |
| **Build tool integration** | `file(DOWNLOAD ... EXPECTED_HASH SHA256=dca2d90c88437a701b1c2e71fa54e76f9fa41d7deee935d74dc871ea66ecfdd2)` from the pinned raw URL, following the ADR 0004 §6 Noto Sans acquisition pattern exactly (ADR 0002 §A4): a `BRAVURA_FONT_SRC` `FILEPATH` cache-variable override for offline builds, SHA-256 verified at configure time on every acquisition path with `FATAL_ERROR` on mismatch, and no system-font fallback. |
| **Distribution** | Redistributable in source and binary form under OFL 1.1. Must retain the copyright notice and license text. |

---

## 12. Noto Sans — Reproducible Latin Text Font (Spike)

| Field | Detail |
|-------|--------|
| **Status** | POLICY-CLEARED — spike-only reproducible text font (OFL 1.1). Pinned at an immutable SHA for deterministic HarfBuzz text shaping and glyph rasterization in self-tests. No system-font fallback is permitted. |
| **Repository** | https://github.com/notofonts/noto-fonts |
| **Pinned SHA** | `ffebf8c1ee449e544955a7e813c54f9b73848eac` |
| **License** | SIL Open Font License 1.1 |
| **License file at SHA** | https://raw.githubusercontent.com/notofonts/noto-fonts/ffebf8c1ee449e544955a7e813c54f9b73848eac/LICENSE |
| **Committed license** | `docs/licenses/NotoSans-OFL.txt` |
| **SPDX** | OFL-1.1 |
| **Patent grant** | None (OFL 1.1; copyright and trademark only) |
| **Required notices** | Copyright 2018 The Noto Project Authors (github.com/googlei18n/noto-fonts). The OFL 1.1 notice must be included. "Noto" is a trademark of Google LLC. |
| **Used file** | `archive/hinted/NotoSans/NotoSans-Regular.ttf` at pinned SHA |
| **Distribution** | Redistributable in source and binary form under OFL 1.1. Must retain the copyright notice and license text. |

---

## 13. GoogleTest — Production Unit Test Framework

| Field | Detail |
|-------|--------|
| **Status** | **POLICY-CLEARED.** Promoted in Milestone 01 (ADR 0002 §11, ADR 0003 §2.3) from the earlier M0 spike-only note to the production test framework used by every GraphScore-owned target's test suite. |
| **Repository** | https://github.com/google/googletest |
| **Pinned SHA** | `6910c9d9165801d8827d628cb72eb7ea9dd538c5` (1.16.0) |
| **License** | BSD 3-Clause |
| **License file at SHA** | https://raw.githubusercontent.com/google/googletest/6910c9d9165801d8827d628cb72eb7ea9dd538c5/LICENSE |
| **Committed license** | `docs/licenses/GoogleTest-BSD-3-Clause.txt` |
| **Patent grant** | None stated in the BSD 3-Clause license. |
| **Required notices** | Copyright 2008, Google Inc. Retain the BSD 3-Clause copyright, conditions, and disclaimer wherever a compiled test binary is distributed. Test executables are development/CI artifacts only and are never included in a shipped writer or runtime binary distribution, so this obligation does not extend to `0.1.0` release archives. |
| **Transitive closure** | None: `BUILD_GMOCK=OFF`, `INSTALL_GTEST=OFF`. Every `graphscore_<component>_test` target links only `GTest::gtest` and `GTest::gtest_main`. |

---

## 14. Meson — ThorVG Build Toolchain

| Field | Detail |
|-------|--------|
| **Status** | POLICY-CLEARED as a build-tool dependency (ADR 0002 §A1.1, 2026-08-03 amendment). Never linked into, `#include`d by, or shipped inside any GraphScore or ThorVG binary artifact — it drives ThorVG's own build only. |
| **Repository** | https://github.com/mesonbuild/meson |
| **Pinned SHA** | `ff84a1ab2699385f67eea990260a20beb2b46c98` (release `1.11.2`) |
| **License** | Apache License 2.0 |
| **License file at SHA** | https://raw.githubusercontent.com/mesonbuild/meson/ff84a1ab2699385f67eea990260a20beb2b46c98/COPYING |
| **Committed license** | `docs/licenses/Meson-COPYING.txt` |
| **SPDX** | Apache-2.0 |
| **Patent grant** | Yes — Apache-2.0 §3 |
| **Required notices** | Standard Apache-2.0 NOTICE/attribution retention; no additional obligation. |
| **Minimum Python** | `>= 3.7.0` at the pinned SHA. Satisfied by this repository's Python (3.9.6 at review time); not a blocker. |
| **Build tool integration** | `FetchContent_MakeAvailable` (corrected, fix round — not the deprecated single-argument `FetchContent_Populate` form the original draft specified; Meson has no `CMakeLists.txt` anywhere in its tree, so `MakeAvailable` safely populates the source without ever attempting `add_subdirectory`, per CMake's own `FetchContent.cmake` logic); invoked directly as `${Python3_EXECUTABLE} ${meson_SOURCE_DIR}/meson.py`. No `pip install`, no PyPI access. `FETCHCONTENT_SOURCE_DIR_MESON` for offline builds; covered automatically by `graphscore_offline_dependencies`'s generic dependency discovery. See ADR 0002 §A1 for the full acquisition and timing design. |
| **Distribution** | Apache-2.0 terms. Build-tool only; no distribution obligation attaches to any shipped GraphScore artifact. |

---

## 15. Ninja — ThorVG Build Toolchain

| Field | Detail |
|-------|--------|
| **Status** | POLICY-CLEARED as a build-tool dependency, license/provenance record only (ADR 0002 §A1.2, corrected in the 2026-08-03 fix round). Not fetched or built from source: located via `find_program` with a version floor. Never linked into, `#include`d by, or shipped inside any GraphScore or ThorVG binary artifact — it drives ThorVG's own build only. |
| **Repository** | https://github.com/ninja-build/ninja |
| **Pinned SHA (license/provenance record, not built; not the source of the version floor)** | `3441b633c2fe2c494e958780ba0f4227b1327634` (release `v1.13.2`) |
| **License** | Apache License 2.0 |
| **License file at SHA** | https://raw.githubusercontent.com/ninja-build/ninja/3441b633c2fe2c494e958780ba0f4227b1327634/COPYING |
| **Committed license** | `docs/licenses/Ninja-COPYING.txt` |
| **SPDX** | Apache-2.0 |
| **Patent grant** | Yes — Apache-2.0 §3 |
| **Required notices** | Standard Apache-2.0 NOTICE/attribution retention; no additional obligation. |
| **Build tool integration** | **Corrected, fix round; floor further corrected, Round 1 review**: `find_program(GRAPHSCORE_NINJA_EXECUTABLE NAMES ninja ninja-build REQUIRED)` plus a `1.8.2` version-floor check against `${GRAPHSCORE_NINJA_EXECUTABLE} --version` — `1.8.2` is Meson's own documented minimum (`ninja_required_version` in its generated build files), not the pinned SHA's tagged `1.13.2`, which is unsatisfiable by this repository's own Ubuntu 24.04 and Windows CI images. No `FetchContent`, no source build, no `ninja` CMake target. Every GraphScore CMake preset already requires a working `ninja` on the build host as its generator (`CMakePresets.json`), and CI already installs it independently for that reason, so building a second copy from pinned source added no value and reintroduced the class of hazard ADR 0002 §A7.2 documents generally: Ninja's own `CMakeLists.txt` gates `ninja_test` behind `include(CTest); if(BUILD_TESTING)`, and inside that block declares its own independent `FetchContent_Declare(googletest URL .../googletest-1.16.0.tar.gz ...)` under the same `googletest` FetchContent name this repository's own `cmake/dependencies.cmake` already declares (§13) — a name collision, not merely a redundant fetch. See ADR 0002 §A1.2 for the full withdrawal rationale. No `FETCHCONTENT_SOURCE_DIR_NINJA` exists and none is needed: `find_program` never touches the network. |
| **Distribution** | Apache-2.0 terms. Build-tool only; no distribution obligation attaches to any shipped GraphScore artifact. |

---

## Standard Library and Compiler Runtime

GraphScore links against the platform C++ standard library (libc++ on
macOS/Clang, MSVC STL on Windows, libstdc++ on Linux).  These are
system components provided by the platform toolchain and are not
vendored.

---

## Summary

| # | Dependency | Status | SHA | License |
|---|------------|--------|-----|---------|
| 1 | SDL3 | PROVISIONAL | `08b9c55393be5cb08fbec12ca431470faba3c8c9` | Zlib |
| 2 | accesskit-c | EXCLUDED | `826d672661f9453c8b269ab3946dbcbae6300555` | MIT OR Apache-2.0 |
| 3 | ThorVG | **ACCEPTED** (production) | `6d5933c9d1aca94635c6ad8129f3530ae554d423` | MIT |
| 4 | HarfBuzz | PROVISIONAL | `af192b7e0f49a9965220ba3f18473e3f8e28b8b9` | Old MIT |
| 5 | FreeType | POLICY-CLEARED | `f01dec5e676847267834b881b25f6e8c79581163` | FTL |
| 6 | miniaudio | PROVISIONAL | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | Unlicense OR MIT-0 |
| 7 | PortAudio | PROVISIONAL fallback | `f88b5841575b43bfa024a6861635b69d7eb98d6d` | MIT |
| 8 | RtMidi | PROVISIONAL | `a3233c22949342f6697681e2cf2403e27fcf0c9e` | MIT |
| 9 | VST3 SDK | DEFERRED | To be pinned | MIT |
| 10 | MIDI encoding | Owned code | N/A | Apache-2.0 |
| 11 | Bravura | **POLICY-CLEARED (default build dependency)** | `02e8ed29a29115df35007d1178cebaeee26c20e1` | OFL-1.1 |
| 12 | Noto Sans | POLICY-CLEARED (spike-only text font) | `ffebf8c1ee449e544955a7e813c54f9b73848eac` | OFL-1.1 |
| 13 | GoogleTest | POLICY-CLEARED (production test framework) | `6910c9d9165801d8827d628cb72eb7ea9dd538c5` | BSD 3-Clause |
| 14 | Meson | POLICY-CLEARED (build tool) | `ff84a1ab2699385f67eea990260a20beb2b46c98` | Apache-2.0 |
| 15 | Ninja | POLICY-CLEARED (build tool) | `3441b633c2fe2c494e958780ba0f4227b1327634` | Apache-2.0 |
