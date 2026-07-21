# SPDX-License-Identifier: Apache-2.0
#
# GraphScore M0 Phase C — Rendering & Accessibility Spike
# STANDALONE build — not part of the production CMake DAG.
#
# Pins from ADR 0002 (all verified reachable at authoritative upstream repos):
#   SDL3:     08b9c55393be5cb08fbec12ca431470faba3c8c9  (zlib, 2026-07-18)
#   HarfBuzz: af192b7e0f49a9965220ba3f18473e3f8e28b8b9  (Old MIT)
#   FreeType:  f01dec5e676847267834b881b25f6e8c79581163  (FTL)
#   Bravura:   02e8ed29a29115df35007d1178cebaeee26c20e1  (OFL 1.1)
#   Noto Sans: ffebf8c1ee449e544955a7e813c54f9b73848eac  (OFL 1.1)

cmake_minimum_required(VERSION 3.20)
project(m0_rendering_accessibility
    VERSION 0.1.0
    DESCRIPTION "GraphScore M0 Phase C rendering/accessibility spike"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# ---- FetchContent boilerplate ----
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Support source overrides and populated caches
# export FETCHCONTENT_SOURCE_DIR_SDL3=/path/to/sdl3
# export FETCHCONTENT_SOURCE_DIR_FREETYPE=/path/to/freetype
if(NOT DEFINED FETCHCONTENT_FULLY_DISCONNECTED)
    set(FETCHCONTENT_FULLY_DISCONNECTED OFF)
endif()

# ──────────────────────────────────────────────────────────────────
# Fully-Disconnected Configure Transcript
#
# The following command was executed on macOS 24.6.0 arm64 with
# CMake 4.4.0, Ninja 1.13.2, AppleClang 17.0.0.  All FetchContent
# sources were pre-populated via a previous online build; their
# `_deps/<name>-src/` directories were reused as override paths.
#
#   cmake -B build -G Ninja \
#     -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
#     -DFETCHCONTENT_SOURCE_DIR_SDL3=$(pwd)/build/_deps/sdl3-src \
#     -DFETCHCONTENT_SOURCE_DIR_FREETYPE=$(pwd)/build/_deps/freetype-src \
#     -DFETCHCONTENT_SOURCE_DIR_HARFBUZZ=$(pwd)/build/_deps/harfbuzz-src \
#     -DFETCHCONTENT_SOURCE_DIR_BRAVURA=$(pwd)/build/_deps/bravura-src
#
#   Noto Sans is downloaded as a single file via file(DOWNLOAD); for offline
#   builds, pre-place NotoSans-Regular.ttf at the NOTO_SANS_DST path.
#
#   cmake --build build
#
#   ctest --test-dir build --output-on-failure
#
# This is the *exact* command-attributed transcript.  When
# FETCHCONTENT_FULLY_DISCONNECTED=ON, CMake skips all network
# resolution; every FetchContent_Declare'd dependency must have a
# corresponding FETCHCONTENT_SOURCE_DIR_<NAME> cache variable.
# The HarfBuzz-FreeType isolation invariant (ADR 0002 §4) is
# verified at configure time regardless of disconnected mode.
#
# Repro: populate _deps/ with a prior online build, then run the
# exact cmake command above.  If _deps/ is not present, run the
# online configure first to populate them.
# ──────────────────────────────────────────────────────────────────

# ----
# 1. HarfBuzz FIRST (before any freetype CMake target exists)
#    This guarantees the upstream if(TARGET freetype) guard does NOT fire.
# ----
set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
set(HB_HAVE_CORETEXT OFF CACHE BOOL "" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
set(HB_HAVE_CAIRO OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GRAPHITE2 OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
set(HB_HAVE_UNISCRIBE OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GDI OFF CACHE BOOL "" FORCE)
set(HB_HAVE_DIRECTWRITE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG af192b7e0f49a9965220ba3f18473e3f8e28b8b9
)
FetchContent_MakeAvailable(harfbuzz)

# Verify no FreeType linkage on HarfBuzz — this is the ADR 0002 §4 invariant.
get_target_property(_hb_link_libs harfbuzz LINK_LIBRARIES)
if(_hb_link_libs)
    if("freetype" IN_LIST _hb_link_libs)
        message(FATAL_ERROR
            "HarfBuzz is linked to FreeType — isolation violated! "
            "HB_HAVE_FREETYPE=OFF should prevent this. "
            "The upstream if(TARGET freetype) guard must not fire "
            "when HarfBuzz is configured before any freetype target exists.")
    endif()
endif()
message(STATUS "[spike] HarfBuzz isolation verified: no freetype in LINK_LIBRARIES")

# ----
# 2. FreeType (after HarfBuzz — no circular dependency)
# ----
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF ON CACHE BOOL "" FORCE)
set(FT_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_BZIP2 OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_PNG OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_BROTLI OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG f01dec5e676847267834b881b25f6e8c79581163
)
FetchContent_MakeAvailable(freetype)

# ----
# 3. SDL3 — windowing and input
#
#   Explicit overrides below keep the spike portable and minimal.
#   In addition, the following options are auto-detected by SDL and are
#   intentionally left at their defaults (reviewed per-platform):
#
#     Option            Top-of-tree macOS arm64   Rationale
#     ───────────────── ────────────────────────  ─────────────────────────
#     SDL_METAL         ON                        Required by Cocoa video
#                                                 driver (macOS windowing)
#                                                 — spike-only, not needed
#                                                 on non-Apple platforms.
#     SDL_RENDER_METAL   ON (SDL_RENDER+APPLE)     Required by Cocoa window-
#                                                 surface presentation path
#                                                 — SDL_GetWindowSurface on
#                                                 macOS uses the render API
#                                                 (Metal texture framebuffer)
#                                                 because Cocoa has no software
#                                                 CreateWindowFramebuffer. The
#                                                 Metal render driver must be
#                                                 compiled in for window-surface
#                                                 presentation to work. This is
#                                                 a spike-only reviewed closure
#                                                 choice; non-Apple platforms
#                                                 using X11/Wayland do not need
#                                                 SDL_RENDER for window-surface.
#     SDL_ASSEMBLY      ON                        Platform-optimised blit/
#                                                 conversion routines.
#                                                 No behavioural change.
#     SDL_ARMNEON       ON                        ARM SIMD (Apple Silicon).
#                                                 No behavioural change.
#     SDL_ARMSVE2       ON                        SVE2 extension (M4+).
#                                                 No behavioural change.
#     SDL_NOTIFICATION  ON                        macOS User Notifications
#                                                 framework. Harmless for
#                                                 spike windowing; unused.
#
#   All five (SDL_METAL, SDL_RENDER_METAL, SDL_ASSEMBLY, SDL_ARMNEON,
#   SDL_ARMSVE2, SDL_NOTIFICATION) are platform-autodetected defaults;
#   none are required for correctness beyond SDL_RENDER_METAL, which is
#   explicitly enabled via SDL_RENDER=ON below.  Disabling them would add
#   gratuitous cmake cache variables without changing spike behaviour.
#   They are therefore left at upstream autodetected values for this spike.
#
#   SDL_RENDER is set ON because the macOS Cocoa video driver relies on
#   the render API (Metal texture framebuffer) for SDL_GetWindowSurface.
#   Without SDL_RENDER, window-surface presentation fails with "Window
#   framebuffer support not available" on macOS.  On platforms that have a
#   native software CreateWindowFramebuffer (X11, Wayland, Windows),
#   SDL_RENDER is not required for window-surface — but it is harmless.
#   Enabling it adds approximately 6 render drivers (software + Metal on
#   macOS) to the static library; all are needed for the Cocoa path.
# ----
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_DUMMYVIDEO OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_SYSTEM_ICONV OFF CACHE BOOL "" FORCE)
set(SDL_DLOPEN_NOTES OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(SDL_XINPUT OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG 08b9c55393be5cb08fbec12ca431470faba3c8c9
)
FetchContent_MakeAvailable(SDL3)

# ----
# 4. Bravura SMuFL font — fetch repository, extract OTF at configure time
#    To override source: export FETCHCONTENT_SOURCE_DIR_BRAVURA=/path/to/bravura
#
#    Bravura is a data-only dependency (no CMake build at source root).
#    FetchContent_MakeAvailable will download the repository but skip
#    add_subdirectory since there is no top-level CMakeLists.txt.
#    This is the supported immutable population path (no CMP0169 warning).
# ----
set(BRAVURA_FONT_DIR "${CMAKE_BINARY_DIR}/bravura_font" CACHE PATH
    "Directory where Bravura.otf is placed")
set(BRAVURA_SHA "02e8ed29a29115df35007d1178cebaeee26c20e1")

FetchContent_Declare(
    bravura
    GIT_REPOSITORY https://github.com/steinbergmedia/bravura.git
    GIT_TAG ${BRAVURA_SHA}
)
FetchContent_MakeAvailable(bravura)

# Copy Bravura.otf to the build directory for runtime access.
# SMuFL glyphs use the Bravura music face; text shaping uses
# Noto Sans exclusively.
# Face-correct rasterization is enforced: never shape with one FT_Face and
# rasterize GIDs through another.
set(BRAVURA_OTF_SRC "${bravura_SOURCE_DIR}/redist/otf/Bravura.otf")
set(BRAVURA_OTF_DST "${BRAVURA_FONT_DIR}/Bravura.otf")

if(EXISTS "${BRAVURA_OTF_SRC}")
    file(MAKE_DIRECTORY "${BRAVURA_FONT_DIR}")
    file(COPY "${BRAVURA_OTF_SRC}" DESTINATION "${BRAVURA_FONT_DIR}")
    message(STATUS "[spike] Bravura OTF copied: ${BRAVURA_OTF_DST}")
else()
    message(WARNING "[spike] Bravura source not found at ${BRAVURA_OTF_SRC}")
endif()

# ----
# 5. Noto Sans Latin text font — pinned reproducible Latin-capable font for
#    deterministic HarfBuzz text shaping and glyph rasterization.
#    Source: notofonts/noto-fonts, immutable SHA ffebf8c1....
#    License: SIL Open Font License 1.1 (docs/licenses/NotoSans-OFL.txt)
#    Used file: archive/hinted/NotoSans/NotoSans-Regular.ttf at pinned SHA
#    Artifact SHA-256: d78a4640e19e06c128e2041d480d5ddfd8db4fdecb3d582ca12b26aef1548bf9
#
#    This is the canonical reproducible text font for all self-tests.
#    No system-font fallback (Helvetica, CWD, etc.) is permitted.
#    Tests that require a text face must use this exact pinned artifact.
#
#    Downloaded as a single immutable artifact via file(DOWNLOAD) from the
#    exact SHA-pinned raw URL.  NOTO_SANS_SRC can be overridden via cache
#    variable.  Every acquisition path (download, override, pre-existing
#    destination) is verified against the pinned SHA-256 at configure time.
# ----
set(NOTO_FONT_DIR "${CMAKE_BINARY_DIR}/noto_font" CACHE PATH
    "Directory where NotoSans-Regular.ttf is placed")
set(NOTO_SHA "ffebf8c1ee449e544955a7e813c54f9b73848eac")
set(NOTO_EXPECTED_SHA256 "d78a4640e19e06c128e2041d480d5ddfd8db4fdecb3d582ca12b26aef1548bf9")

set(NOTO_SANS_SRC "${CMAKE_BINARY_DIR}/_downloads/NotoSans-Regular.ttf" CACHE FILEPATH
    "Path to NotoSans-Regular.ttf (will be downloaded if absent)")
set(NOTO_SANS_DST "${NOTO_FONT_DIR}/NotoSans-Regular.ttf")

set(NOTO_URL
    "https://raw.githubusercontent.com/notofonts/noto-fonts/${NOTO_SHA}/archive/hinted/NotoSans/NotoSans-Regular.ttf")

if(NOT EXISTS "${NOTO_SANS_DST}")
    if(NOT EXISTS "${NOTO_SANS_SRC}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_downloads")
        message(STATUS "[spike] Downloading NotoSans-Regular.ttf from pinned SHA ${NOTO_SHA}...")
        file(DOWNLOAD "${NOTO_URL}" "${NOTO_SANS_SRC}"
             STATUS _noto_dl_status
             TLS_VERIFY ON
             EXPECTED_HASH SHA256=${NOTO_EXPECTED_SHA256})
        list(GET _noto_dl_status 0 _noto_dl_code)
        if(NOT _noto_dl_code EQUAL 0)
            list(GET _noto_dl_status 1 _noto_dl_msg)
            message(FATAL_ERROR "[spike] Noto Sans download failed: ${_noto_dl_msg}")
        endif()
    endif()
    if(EXISTS "${NOTO_SANS_SRC}")
        file(MAKE_DIRECTORY "${NOTO_FONT_DIR}")
        file(COPY "${NOTO_SANS_SRC}" DESTINATION "${NOTO_FONT_DIR}")
        message(STATUS "[spike] Noto Sans OTF copied: ${NOTO_SANS_DST}")
    else()
        message(FATAL_ERROR "[spike] Noto Sans source not found at ${NOTO_SANS_SRC}")
    endif()
endif()

# ── Noto artifact integrity gate ───────────────────────────────────────
# Every acquisition path must match the pinned SHA-256 exactly.
# This covers: pre-existing destination, cache override, and fresh download.
if(EXISTS "${NOTO_SANS_DST}")
    file(SHA256 "${NOTO_SANS_DST}" _noto_actual_sha256)
    if(NOT _noto_actual_sha256 STREQUAL "${NOTO_EXPECTED_SHA256}")
        message(FATAL_ERROR
            "[spike] Noto Sans artifact integrity FAILED:\n"
            "  expected SHA256: ${NOTO_EXPECTED_SHA256}\n"
            "  actual   SHA256: ${_noto_actual_sha256}\n"
            "  file: ${NOTO_SANS_DST}\n"
            "  Remove the file to re-download, or override NOTO_SANS_SRC "
            "with a valid artifact.")
    endif()
    message(STATUS "[spike] Noto Sans artifact integrity verified (SHA-256)")
else()
    message(FATAL_ERROR "[spike] Noto Sans destination not found at ${NOTO_SANS_DST}")
endif()

# ----
# 6. Spike executable — GraphScore-owned code with strict warnings
# ----
set(SPIKE_SOURCES
    src/main.cpp
    src/app.cpp
    src/rasterizer.cpp
    src/render_list.cpp
    src/font_manager.cpp
    src/notation_renderer.cpp
    src/graph_renderer.cpp
    src/hit_test.cpp
    src/input_logger.cpp
    src/native_handle.cpp
    src/self_test.cpp
    src/semantic_tree.cpp
    src/accessibility_report.cpp
    src/viewport_event_handler.cpp
)

# Add macOS-specific Objective-C++ source
if(APPLE)
    list(APPEND SPIKE_SOURCES src/accessibility_bridge_mac.mm)
else()
    # No-op bridge is inline in the header; no extra source needed
endif()

add_executable(m0_spike ${SPIKE_SOURCES})
set_target_properties(m0_spike PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Third-party includes — GraphScore spike source only
target_include_directories(m0_spike PRIVATE
    src/
)
target_link_libraries(m0_spike PRIVATE
    harfbuzz
    freetype
    SDL3::SDL3-static
)

# Platform frameworks for SDL3 on macOS
if(APPLE)
    target_link_libraries(m0_spike PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework GameController"
        "-framework CoreHaptics"
        "-framework ForceFeedback"
        "-framework UniformTypeIdentifiers"
        "-framework Carbon"
        "-framework AppKit"
    )
endif()

# ----
# 7. Warning configuration — strict for GraphScore spike code
# ----
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(m0_spike PRIVATE
        -Weverything
        -Werror

        # Documented suppression list for the spike
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-padded
        -Wno-weak-vtables
        -Wno-exit-time-destructors
        -Wno-global-constructors
        -Wno-ctad-maybe-unsupported
        -Wno-switch-enum
        -Wno-covered-switch-default
        -Wno-float-equal
        -Wno-double-promotion
        -Wno-implicit-int-conversion
        -Wno-sign-conversion
        -Wno-shorten-64-to-32
        -Wno-unused-macros
        -Wno-reserved-macro-identifier
        -Wno-date-time
        -Wno-zero-as-null-pointer-constant
        -Wno-unused-parameter
        -Wno-poison-system-directories
        -Wno-documentation-unknown-command
        -Wno-deprecated-copy-with-user-provided-dtor
        -Wno-switch-default
        -Wno-unused-private-field
        -Wno-unused-variable
        -Wno-missing-field-initializers
        -Wno-missing-variable-declarations
        -Wno-direct-ivar-access
        -Wno-objc-interface-ivars
        -Wno-objc-missing-property-synthesis
        -Wno-deprecated-implementations
        -Wno-nullable-to-nonnull-conversion
        -Wno-objc-messaging-id
        -Wno-strict-selector-match
        -Wno-old-style-cast
        -Wno-deprecated-declarations
    )

    # Ensure third-party headers have warnings suppressed via SYSTEM
    target_include_directories(m0_spike SYSTEM PRIVATE
        ${harfbuzz_SOURCE_DIR}/src
        ${freetype_SOURCE_DIR}/include
        ${sdl3_SOURCE_DIR}/include
        ${sdl3_BINARY_DIR}/include-revision
    )
endif()

# ----
# 8. Testing — CTest integration
#
#    Standard headless-safe suite (ctest must exit zero):
#      spike_self_test           deterministic self-tests, no window required
#      spike_offscreen           render to PPM, print pixel hash
#      spike_offscreen_invalid_output  expected PPM creation failure
#      spike_native_handle       probe Cocoa/Win32/X11 handle (hidden window)
#      spike_accessibility_report  semantic tree + macOS bridge (hidden window)
#      spike_smoke_headless_negative  expected headless failure (WILL_FAIL)
#
#    Display-attached suite (opt-in, requires physical display):
#      SPIKE_ENABLE_DISPLAY_TESTS=ON  enables spike_smoke (real window rendering)
#
#    Commands:
#      Standard headless-safe:  cmake -B build -G Ninja && cmake --build build &&
#                               ctest --test-dir build --output-on-failure
#      Display-attached:        Add -DSPIKE_ENABLE_DISPLAY_TESTS=ON to cmake line
# ----
enable_testing()

option(SPIKE_ENABLE_DISPLAY_TESTS "Enable display-attached interactive smoke test (requires physical display)" OFF)

add_test(NAME spike_self_test
    COMMAND m0_spike --self-test
        --bravura-font "${BRAVURA_OTF_DST}"
        --text-font "${NOTO_SANS_DST}"
)

add_test(NAME spike_offscreen
    COMMAND m0_spike --offscreen
        --offscreen-output "${CMAKE_BINARY_DIR}/spike_output.ppm"
        --bravura-font "${BRAVURA_OTF_DST}"
        --text-font "${NOTO_SANS_DST}"
)

# The build directory is always a directory, so it is an inherently invalid
# PPM output path without relying on permission or root-user behavior.
add_test(NAME spike_offscreen_invalid_output
    COMMAND m0_spike --offscreen
        --offscreen-output "${CMAKE_BINARY_DIR}"
        --bravura-font "${BRAVURA_OTF_DST}"
        --text-font "${NOTO_SANS_DST}"
)
set_tests_properties(spike_offscreen_invalid_output PROPERTIES
    WILL_FAIL TRUE
)

add_test(NAME spike_native_handle
    COMMAND m0_spike --native-handle
        --native-handle-report "${CMAKE_BINARY_DIR}/spike_native_handle.txt"
)

add_test(NAME spike_accessibility_report
    COMMAND m0_spike
        --accessibility-report "${CMAKE_BINARY_DIR}/spike_accessibility_report.txt"
        --bravura-font "${BRAVURA_OTF_DST}"
        --text-font "${NOTO_SANS_DST}"
)

# Display smoke test — opt-in via SPIKE_ENABLE_DISPLAY_TESTS=ON.
# Requires a physical display. Default OFF so standard headless CTest
# always exits zero.
if(SPIKE_ENABLE_DISPLAY_TESTS)
    add_test(NAME spike_smoke
        COMMAND m0_spike --smoke --quit-after-ms 500
            --bravura-font "${BRAVURA_OTF_DST}"
            --text-font "${NOTO_SANS_DST}"
    )
    message(STATUS "[spike] Display tests ENABLED — spike_smoke registered and required to pass")
else()
    message(STATUS "[spike] Display tests DISABLED — smoke headless-safe; use -DSPIKE_ENABLE_DISPLAY_TESTS=ON on physical Mac")
endif()

# Negative-path smoke test: forces an invalid SDL video driver via
# SDL_VIDEO_DRIVER env so smoke deterministically exits nonzero even on
# a display machine.  WILL_FAIL documents this as expected behaviour.
add_test(NAME spike_smoke_headless_negative
    COMMAND m0_spike --smoke --quit-after-ms 500
        --bravura-font "${BRAVURA_OTF_DST}"
        --text-font "${NOTO_SANS_DST}"
)
set_tests_properties(spike_smoke_headless_negative PROPERTIES
    WILL_FAIL TRUE
    ENVIRONMENT "SDL_VIDEO_DRIVER=__headless_negative_force_fail__"
)

# ----
# 9. Sanitizer configurations (opt-in)
#    TSan is N/A — this spike contains no concurrency primitives.
# ----
option(SPIKE_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(SPIKE_ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        message(STATUS "[spike] Enabling ASan+UBSan")
        target_compile_options(m0_spike PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        target_link_options(m0_spike PRIVATE
            -fsanitize=address,undefined
        )
    else()
        message(WARNING "[spike] SPIKE_ENABLE_ASAN requires Clang/AppleClang")
    endif()
endif()

# ----
# 10. Install — minimal for evidence capture
# ----
install(TARGETS m0_spike DESTINATION bin)

message(STATUS "[spike] Build configured")
message(STATUS "[spike]   Bravura font: ${BRAVURA_OTF_DST}")
message(STATUS "[spike]   Noto Sans font: ${NOTO_SANS_DST}")
message(STATUS "[spike]   Display tests: ${SPIKE_ENABLE_DISPLAY_TESTS}")
message(STATUS "[spike]   ASan: ${SPIKE_ENABLE_ASAN}")
