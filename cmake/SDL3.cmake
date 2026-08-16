# SPDX-License-Identifier: Apache-2.0
#
# SDL3 dependency adapter (ADR 0002 §1). SDL3 provides the writer's native
# window creation, input event collection, clipboard, drag-and-drop, and
# high-DPI handling. It is PRIVATE to graphscore_writer_shell: no SDL type
# appears in any GraphScore public header (ADR 0003 §2.2).
#
# ADR 0002 §1 reviewed and fixed the value of every SDL3 option that affects
# the licence surface or pulls in an unreviewed system dependency. This file
# is the executable form of that table. The options are set as FORCEd cache
# entries before FetchContent_MakeAvailable so that SDL's own set_option /
# dep_option calls see a value already present and leave it alone.
#
# ADR 0002 §1 "M1 Verification Gate" additionally requires evidence that each
# declared option actually resolved to its declared value. That verification
# runs below, after MakeAvailable, and fails configure on any mismatch — so
# the gate is enforced on every configure rather than by a snapshot taken
# once.
#
# ADR 0002 §A5 extends the reviewed option set: GraphScore presents ThorVG's
# CPU-rasterized output through SDL_CreateRenderer plus a streaming
# SDL_Texture, backed by a GPU renderer per platform (Metal on macOS, D3D11
# on Windows, OpenGL on Linux). SDL_RENDER=ON is common to all three
# platforms; SDL_DIRECTX (Windows) and SDL_OPENGL (Linux) are decided per
# platform below. Everything §A5 leaves OFF stays OFF.
#
# §A5/§A7.3 additionally require asserting the *derived* result of each
# newly enabled graphics API, not only the option value GraphScore declares:
# SDL_OPENGL/SDL_DIRECTX/SDL_RENDER each resolve to a working renderer only
# through a compiled probe (HAVE_OPENGL, HAVE_D3D11_H, HAVE_FRAMEWORK_METAL)
# that can fail independently of the option. That assertion runs in a
# separate block below, after the option-readback gate.

include(FetchContent)

set(GRAPHSCORE_SDL3_GIT_TAG "08b9c55393be5cb08fbec12ca431470faba3c8c9")

# ---------------------------------------------------------------------------
# The reviewed option set (ADR 0002 §1)
# ---------------------------------------------------------------------------
#
# Each entry is "NAME=VALUE". Options that ADR 0002 marks platform-specific
# are appended per platform below; everything here applies everywhere.

set(GRAPHSCORE_SDL3_OPTIONS
  # Library type: GraphScore links SDL statically into the writer executable.
  SDL_SHARED=OFF
  SDL_STATIC=ON
  SDL_TEST_LIBRARY=OFF
  SDL_TESTS=OFF
  SDL_INSTALL=OFF

  # Core subsystems. Only video is required for the writer shell; audio is
  # miniaudio's job (ADR 0002 §6a). SDL_GPU (the unified next-gen GPU API)
  # stays deferred (ADR 0002 §A5: "a simple CPU-buffer-to-texture blit, not a
  # programmable GPU pipeline"); SDL_RENDER=ON is common to every platform
  # (ADR 0002 §A5) — the specific renderer backend is decided per platform
  # below.
  SDL_VIDEO=ON
  SDL_AUDIO=OFF
  SDL_GPU=OFF
  SDL_RENDER=ON
  SDL_CAMERA=OFF
  SDL_JOYSTICK=OFF
  SDL_HAPTIC=OFF
  SDL_POWER=OFF
  SDL_SENSOR=OFF
  SDL_DIALOG=OFF
  SDL_TRAY=OFF
  SDL_NOTIFICATION=OFF

  # HIDAPI is vendored in the SDL tree and tri-licensed. GraphScore selects
  # BSD 3-clause, but no enabled subsystem consumes it, so it stays off and
  # out of the shipped binary entirely.
  SDL_HIDAPI=OFF
  SDL_HIDAPI_LIBUSB=OFF

  # Graphics APIs still deferred everywhere (ADR 0002 §A5: SDL_VULKAN and
  # SDL_OPENGLES are unaffected by the presentation-path decision).
  # SDL_METAL is deliberately absent from this common list: it is decided per
  # platform below — ON on macOS (required for the Metal render driver at
  # runtime), OFF elsewhere. See the APPLE block and its comment.
  SDL_VULKAN=OFF
  SDL_OPENGLES=OFF
  SDL_OPENVR=OFF

  # Video drivers that are not real windowing paths.
  SDL_DUMMYVIDEO=OFF
  SDL_OFFSCREEN=OFF

  # Determinism: these default differently per platform, so they are pinned.
  SDL_SYSTEM_ICONV=OFF
  SDL_DLOPEN_NOTES=OFF
  SDL_XINPUT=OFF
)

if (APPLE)
  list(APPEND GRAPHSCORE_SDL3_OPTIONS
    SDL_COCOA=ON
    # ADR 0002 §A5 (empirically corrected): SDL_RENDER=ON alone compiles the
    # Metal *render driver* (SDL_RENDER_METAL auto-enabled by the
    # "SDL_RENDER;APPLE" dep_option guard), but that driver cannot create a
    # renderer at runtime without SDL_METAL=ON as well. SDL_METAL gates
    # SDL_VIDEO_METAL (SDL CMakeLists ~2865), which is the only thing that
    # wires the Cocoa video driver's Metal_CreateView/Metal_GetLayer function
    # pointers (SDL_cocoavideo.m, `#ifdef SDL_VIDEO_METAL`); the Metal render
    # driver obtains its CAMetalLayer through SDL_Metal_CreateView, which
    # returns SDL_Unsupported() ("That operation is not supported") when that
    # pointer is NULL. With SDL_METAL=OFF the Metal render driver therefore
    # compiles but fails at runtime on a Metal-capable Mac. SDL_METAL's own
    # dep_option default is already ON on APPLE; this entry states it
    # explicitly so the readback gate below verifies it like every other
    # reviewed option.
    SDL_METAL=ON
    SDL_OPENGL=OFF
    SDL_DIRECTX=OFF
  )
endif()

if (WIN32)
  list(APPEND GRAPHSCORE_SDL3_OPTIONS
    # ADR 0002 §A5: SDL_DIRECTX=ON is required for the D3D11 render driver.
    # SDL_RENDER_D3D11 is declared explicitly (its own dep_option already
    # defaults ON once SDL_RENDER;SDL_DIRECTX are satisfied, but this
    # project decides every reviewed option explicitly rather than relying
    # on an upstream default that could flip); SDL_RENDER_D3D (legacy D3D9)
    # and SDL_RENDER_D3D12 are explicitly forced OFF so only
    # SDL_RENDER_D3D11 is active.
    SDL_OPENGL=OFF
    SDL_DIRECTX=ON
    SDL_RENDER_D3D=OFF
    SDL_RENDER_D3D11=ON
    SDL_RENDER_D3D12=OFF
  )
endif()

if (UNIX AND NOT APPLE)
  list(APPEND GRAPHSCORE_SDL3_OPTIONS
    # ADR 0002 §A5: SDL_OPENGL=ON is the only path to a working SDL_Renderer
    # on Linux; verified below via the derived HAVE_OPENGL probe, not merely
    # this option's own value.
    SDL_OPENGL=ON
    SDL_DIRECTX=OFF

    # Windowing backends.
    SDL_X11=ON
    SDL_WAYLAND=ON
    SDL_KMSDRM=OFF

    SDL_X11_SHARED=ON
    SDL_WAYLAND_SHARED=ON

    # X11 extensions required for windowing and input.
    SDL_X11_XCURSOR=ON
    SDL_X11_XDBE=ON
    SDL_X11_XINPUT=ON
    SDL_X11_XFIXES=ON
    SDL_X11_XRANDR=ON

    # X11 extensions not required; each defaults ON.
    SDL_X11_XSCRNSAVER=OFF
    SDL_X11_XSHAPE=OFF
    SDL_X11_XSYNC=OFF
    SDL_X11_XTEST=OFF

    # Linux audio backends. SDL_AUDIO is already OFF, but SDL_PIPEWIRE is set
    # by a bare set_option(${UNIX_SYS}) with no SDL_AUDIO guard and would
    # otherwise default ON — the residual risk ADR 0002 §1 records. The
    # verification pass below is what proves the OFF took effect.
    SDL_ALSA=OFF
    SDL_PULSEAUDIO=OFF
    SDL_JACK=OFF
    SDL_SNDIO=OFF
    SDL_OSS=OFF
    SDL_PIPEWIRE=OFF

    # Unreviewed system integration, all default ON on Linux.
    SDL_FRIBIDI=OFF
    SDL_LIBTHAI=OFF
    SDL_DBUS=OFF
    SDL_IBUS=OFF
    SDL_LIBURING=OFF
    SDL_LIBUDEV=OFF
    SDL_WAYLAND_LIBDECOR=OFF

    # ARM-only drivers that auto-enable on Linux arm64/arm32.
    SDL_RPI=OFF
    SDL_ROCKCHIP=OFF
    SDL_VIVANTE=OFF
  )
endif()

# ---------------------------------------------------------------------------
# Apply, fetch, verify
# ---------------------------------------------------------------------------

foreach (gs_sdl_option IN LISTS GRAPHSCORE_SDL3_OPTIONS)
  if (NOT gs_sdl_option MATCHES "^([A-Za-z0-9_]+)=(.+)$")
    message(FATAL_ERROR "Malformed SDL3 option entry: '${gs_sdl_option}'")
  endif()
  set(${CMAKE_MATCH_1} ${CMAKE_MATCH_2} CACHE BOOL "" FORCE)
endforeach()
unset(gs_sdl_option)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG ${GRAPHSCORE_SDL3_GIT_TAG}
  GIT_SHALLOW FALSE
  # SDL's own warnings are not GraphScore build failures (ADR 0001,
  # "Warning isolation").
  SYSTEM
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(SDL3)

# The M1 verification gate. SDL's dep_option() can silently override a value
# whose guard condition is unmet, so every declared option is read back from
# the cache after MakeAvailable and compared against what ADR 0002 declared.
#
# Value comparison alone is not enough. Because this adapter creates each
# cache entry itself, a name that SDL does not recognise — a typo, or an
# option renamed since the pin was reviewed — would be created, read back
# unchanged, and "verified", while having no effect on the build. So each
# declared option is also required to appear in SDL's own CMakeLists.
#
file(READ "${sdl3_SOURCE_DIR}/CMakeLists.txt" gs_sdl_upstream_cmakelists)

set(gs_sdl_mismatches "")
set(gs_sdl_evidence
"SDL3 option evidence (ADR 0002 §1, M1 Verification Gate)
Pinned SHA: ${GRAPHSCORE_SDL3_GIT_TAG}
Platform:   ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}

")

foreach (gs_sdl_option IN LISTS GRAPHSCORE_SDL3_OPTIONS)
  string(REGEX MATCH "^([A-Za-z0-9_]+)=(.+)$" _ "${gs_sdl_option}")
  set(gs_name "${CMAKE_MATCH_1}")
  set(gs_declared "${CMAKE_MATCH_2}")
  set(gs_actual "${${gs_name}}")

  # Normalise CMake's boolean spellings before comparing.
  if (gs_actual)
    set(gs_actual_bool ON)
  else()
    set(gs_actual_bool OFF)
  endif()

  if (NOT gs_sdl_upstream_cmakelists MATCHES "[^A-Za-z0-9_]${gs_name}[^A-Za-z0-9_]")
    list(APPEND gs_sdl_mismatches
      "${gs_name}: not referenced anywhere in SDL's CMakeLists at this pin, so setting it does nothing")
    string(APPEND gs_sdl_evidence "${gs_name}: UNRECOGNISED BY UPSTREAM\n")
    continue()
  endif()

  string(APPEND gs_sdl_evidence
    "${gs_name}: declared=${gs_declared} actual=${gs_actual_bool}\n")

  if (NOT gs_actual_bool STREQUAL gs_declared)
    list(APPEND gs_sdl_mismatches
      "${gs_name}: ADR 0002 §1 declares ${gs_declared}, resolved to ${gs_actual_bool}")
  endif()
endforeach()
unset(gs_sdl_option)

file(WRITE "${CMAKE_BINARY_DIR}/sdl3_option_evidence.txt" "${gs_sdl_evidence}")

if (gs_sdl_mismatches)
  list(JOIN gs_sdl_mismatches "\n  - " gs_sdl_mismatch_text)
  message(FATAL_ERROR
    "SDL3 option verification failed (ADR 0002 §1, M1 Verification Gate):\n\n"
    "  - ${gs_sdl_mismatch_text}\n\n"
    "An option resolving to a value other than the reviewed one means the "
    "shipped SDL3 build differs from the one whose licence surface and "
    "system dependencies were audited. Full evidence: "
    "${CMAKE_BINARY_DIR}/sdl3_option_evidence.txt")
endif()

# ---------------------------------------------------------------------------
# Derived-result assertions (ADR 0002 §A5, §A7.3)
# ---------------------------------------------------------------------------
#
# The readback above proves the *option* GraphScore declared resolved to the
# value GraphScore requested. For SDL_OPENGL (Linux), SDL_DIRECTX (Windows),
# and SDL_RENDER (macOS, via the auto-enabled SDL_RENDER_METAL) that is not
# evidence a working renderer actually resulted: each is gated behind a
# compiled probe in SDL's own CMakeLists.txt / cmake/sdlchecks.cmake that can
# fail independently of the option value —
# HAVE_OPENGL (cmake/sdlchecks.cmake's CheckOpenGL macro, a
# check_c_source_compiles against <GL/gl.h>/<GL/glext.h>), HAVE_D3D11_H
# (CMakeLists.txt ~2299-2310, consumed at ~2446-2449), and
# HAVE_FRAMEWORK_METAL (CMakeLists.txt 2844-2877, a check_objc_source_compiles
# that also rejects an unsupported architecture).
#
# These three — not SDL_VIDEO_RENDER_OGL/_D3D11/_METAL — are the readable
# signal: SDL_VIDEO_RENDER_OGL and its siblings are plain directory-scope
# set() calls with neither CACHE nor PARENT_SCOPE, invisible here after
# FetchContent_MakeAvailable, so asserting them would pass vacuously on every
# platform (ADR 0002 §A7.3). HAVE_OPENGL/HAVE_D3D11_H/HAVE_FRAMEWORK_METAL
# are written as CACHE entries by check_c_source_compiles/
# check_objc_source_compiles and so survive into this scope.
#
# On macOS the readable chain for the Metal render driver is the readback of
# SDL_METAL=ON (declared above) AND this HAVE_FRAMEWORK_METAL probe: together
# they are SDL's own `SDL_METAL AND HAVE_FRAMEWORK_METAL` condition for
# SDL_VIDEO_METAL, without which the compiled Metal render driver fails at
# runtime (see the APPLE block above). SDL_VIDEO_METAL itself is a plain
# directory-scope set() and is not readable here, exactly like
# SDL_VIDEO_RENDER_METAL.
set(gs_sdl_derived_mismatches "")
set(gs_sdl_derived_evidence
"\nDerived-result assertions (ADR 0002 §A5, §A7.3)\n")

if (UNIX AND NOT APPLE)
  string(APPEND gs_sdl_derived_evidence "HAVE_OPENGL: ${HAVE_OPENGL}\n")
  if (NOT HAVE_OPENGL)
    list(APPEND gs_sdl_derived_mismatches
      "HAVE_OPENGL is not set: SDL's CheckOpenGL compiled probe against "
      "<GL/gl.h>/<GL/glext.h> failed, so SDL_OPENGL=ON did not produce a "
      "working OpenGL render driver")
  endif()
endif()

if (WIN32)
  string(APPEND gs_sdl_derived_evidence "HAVE_D3D11_H: ${HAVE_D3D11_H}\n")
  if (NOT HAVE_D3D11_H)
    list(APPEND gs_sdl_derived_mismatches
      "HAVE_D3D11_H is not set: the Windows SDK D3D11 header probe failed, "
      "so SDL_DIRECTX=ON did not produce a working D3D11 render driver")
  endif()
endif()

if (APPLE)
  string(APPEND gs_sdl_derived_evidence
    "HAVE_FRAMEWORK_METAL: ${HAVE_FRAMEWORK_METAL}\n")
  if (NOT HAVE_FRAMEWORK_METAL)
    list(APPEND gs_sdl_derived_mismatches
      "HAVE_FRAMEWORK_METAL is not set: the Metal framework compiled probe "
      "failed, so SDL_RENDER=ON did not auto-enable a working Metal render "
      "driver on this host")
  endif()
endif()

file(APPEND "${CMAKE_BINARY_DIR}/sdl3_option_evidence.txt"
  "${gs_sdl_derived_evidence}")

if (gs_sdl_derived_mismatches)
  list(JOIN gs_sdl_derived_mismatches "\n  - " gs_sdl_derived_mismatch_text)
  message(FATAL_ERROR
    "SDL3 derived-result verification failed (ADR 0002 §A5, §A7.3):\n\n"
    "  - ${gs_sdl_derived_mismatch_text}\n\n"
    "The declared option resolved to the requested value, but the compiled "
    "probe that determines whether a working renderer actually resulted did "
    "not succeed. Full evidence: ${CMAKE_BINARY_DIR}/sdl3_option_evidence.txt")
endif()

# ---------------------------------------------------------------------------
# Upstream defect workaround, macOS, pinned SHA only
# ---------------------------------------------------------------------------
#
# At ${GRAPHSCORE_SDL3_GIT_TAG}, SDL's Cocoa video driver glob compiles
# src/video/cocoa/SDL_cocoanotification.m unconditionally, and SDL_cocoamouse.m
# references GameController's GCMouse unconditionally — but the matching
# `-framework` link dependencies are added only when SDL_NOTIFICATION and
# SDL_JOYSTICK are ON (CMakeLists.txt around lines 2683, 2741). With the ADR
# 0002 §1 option set both are OFF, so the objects are in libSDL3.a with no
# framework to resolve against and the writer fails to link.
#
# Adding the frameworks here rather than changing the option set is the
# narrower fix: it changes nothing about which SDL code is compiled or which
# licences are in play — the objects are already in the archive — it only
# lets the linker resolve what SDL chose to build. Turning the subsystems ON
# instead would enable code paths ADR 0002 §1 deliberately excluded.
#
# These are OS frameworks, which ADR 0003 §2.2 treats as system-provided
# services rather than vendored dependencies.
#
# Revisit when the SDL3 pin moves: if upstream has made those sources
# conditional, delete this block.
if (APPLE)
  target_link_libraries(SDL3-static PRIVATE
    "-framework UserNotifications"
    "-framework Security"
    "-framework GameController"
  )
endif()

message(STATUS
  "SDL3: ${GRAPHSCORE_SDL3_GIT_TAG}, all reviewed options and derived "
  "renderer probes verified (evidence: "
  "${CMAKE_BINARY_DIR}/sdl3_option_evidence.txt)")
