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
  # miniaudio's job (ADR 0002 §6a) and the GPU/render paths are Phase C.
  SDL_VIDEO=ON
  SDL_AUDIO=OFF
  SDL_GPU=OFF
  SDL_RENDER=OFF
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

  # Graphics APIs: deferred to Phase C.
  SDL_VULKAN=OFF
  SDL_OPENGL=OFF
  SDL_OPENGLES=OFF
  SDL_METAL=OFF
  SDL_OPENVR=OFF

  # Video drivers that are not real windowing paths.
  SDL_DUMMYVIDEO=OFF
  SDL_OFFSCREEN=OFF

  # Determinism: these default differently per platform, so they are pinned.
  SDL_SYSTEM_ICONV=OFF
  SDL_DLOPEN_NOTES=OFF
  SDL_XINPUT=OFF
  SDL_DIRECTX=OFF
)

if (APPLE)
  list(APPEND GRAPHSCORE_SDL3_OPTIONS SDL_COCOA=ON)
endif()

if (UNIX AND NOT APPLE)
  list(APPEND GRAPHSCORE_SDL3_OPTIONS
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
  "SDL3: ${GRAPHSCORE_SDL3_GIT_TAG}, all reviewed options verified "
  "(evidence: ${CMAKE_BINARY_DIR}/sdl3_option_evidence.txt)")
