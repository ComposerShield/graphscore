# SPDX-License-Identifier: Apache-2.0
#
# HarfBuzz dependency adapter (ADR 0002 §4, §A3). HarfBuzz is PRIVATE to
# graphscore_rendering: no HarfBuzz type appears in a public header
# (ADR 0003 §2.2).
#
# Writer-only: include()d from the root CMakeLists.txt inside the same
# `if (GRAPHSCORE_BUILD_WRITER)` block that gates SDL3, so
# `-DGRAPHSCORE_BUILD_WRITER=OFF` fetches nothing here (ADR 0002 §A7.1).
#
# ORDERING INVARIANT (ADR 0002 §4, §A3) -- do not move this include() below
# cmake/FreeType.cmake. HarfBuzz's own CMakeLists.txt contains a
# non-overridable guard:
#
#   if (TARGET freetype)
#     set (HB_HAVE_FREETYPE ON)
#     ...
#   endif ()
#   ...
#   if (HB_HAVE_FREETYPE AND TARGET freetype)
#     target_link_libraries(harfbuzz freetype)
#   endif ()
#
# which fires regardless of the HB_HAVE_FREETYPE option value below whenever
# a `freetype` CMake target already exists at the point HarfBuzz is
# configured. Configuring HarfBuzz here, before cmake/FreeType.cmake ever
# runs, means no such target can exist yet; the assertion below proves this
# rather than merely relying on include() order holding.

include(FetchContent)

set(GRAPHSCORE_HARFBUZZ_GIT_TAG "af192b7e0f49a9965220ba3f18473e3f8e28b8b9")

# ADR 0002 §4's constrained option table. Several of these control
# licensing-relevant integrations (HB_HAVE_GLIB/HB_HAVE_CAIRO/
# HB_HAVE_GRAPHITE2 are all LGPL-licensed; HB_HAVE_ICU is a separate
# unreviewed dependency) -- the same "NAME=VALUE list, apply, then read back
# against upstream" discipline as cmake/SDL3.cmake's GRAPHSCORE_SDL3_OPTIONS
# loop applies here so a future pin rename cannot silently turn one of these
# into a no-op.
set(GRAPHSCORE_HARFBUZZ_OPTIONS
  HB_HAVE_FREETYPE=OFF
  HB_HAVE_CORETEXT=OFF
  HB_BUILD_SUBSET=OFF
  HB_BUILD_RASTER=OFF
  HB_BUILD_VECTOR=OFF
  HB_BUILD_GPU=OFF
  HB_BUILD_UTILS=OFF
  HB_HAVE_GLIB=OFF
  HB_HAVE_ICU=OFF
  HB_HAVE_CAIRO=OFF
  HB_HAVE_GRAPHITE2=OFF
  HB_HAVE_GOBJECT=OFF
  HB_HAVE_UNISCRIBE=OFF
  HB_HAVE_GDI=OFF
  HB_HAVE_DIRECTWRITE=OFF
)

foreach (gs_hb_option IN LISTS GRAPHSCORE_HARFBUZZ_OPTIONS)
  if (NOT gs_hb_option MATCHES "^([A-Za-z0-9_]+)=(.+)$")
    message(FATAL_ERROR "Malformed HarfBuzz option entry: '${gs_hb_option}'")
  endif()
  set(${CMAKE_MATCH_1} ${CMAKE_MATCH_2} CACHE BOOL "" FORCE)
endforeach()
unset(gs_hb_option)

FetchContent_Declare(
  HarfBuzz
  GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
  GIT_TAG ${GRAPHSCORE_HARFBUZZ_GIT_TAG}
  GIT_SHALLOW FALSE
  # HarfBuzz's own warnings are not GraphScore build failures (ADR 0001,
  # "Warning isolation").
  SYSTEM
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(HarfBuzz)

# Declared-vs-actual readback plus an upstream name-presence cross-check,
# lifted from cmake/SDL3.cmake's M1 Verification Gate. Value comparison
# alone is not enough: because this adapter creates each cache entry
# itself, a name HarfBuzz no longer recognises (a typo, or an option
# renamed since this pin was reviewed) would be created, read back
# unchanged, and "verified", while having no effect on the build -- and for
# the licensing-relevant options above, that silent no-op is exactly the
# failure mode that matters.
file(READ "${harfbuzz_SOURCE_DIR}/CMakeLists.txt" gs_hb_upstream_cmakelists)

set(gs_hb_mismatches "")
set(gs_hb_evidence
"HarfBuzz option evidence (ADR 0002 §4)
Pinned SHA: ${GRAPHSCORE_HARFBUZZ_GIT_TAG}

")

foreach (gs_hb_option IN LISTS GRAPHSCORE_HARFBUZZ_OPTIONS)
  string(REGEX MATCH "^([A-Za-z0-9_]+)=(.+)$" _ "${gs_hb_option}")
  set(gs_name "${CMAKE_MATCH_1}")
  set(gs_declared "${CMAKE_MATCH_2}")
  set(gs_actual "${${gs_name}}")

  if (gs_actual)
    set(gs_actual_bool ON)
  else()
    set(gs_actual_bool OFF)
  endif()

  if (NOT gs_hb_upstream_cmakelists MATCHES "[^A-Za-z0-9_]${gs_name}[^A-Za-z0-9_]")
    list(APPEND gs_hb_mismatches
      "${gs_name}: not referenced anywhere in HarfBuzz's CMakeLists at this pin, so setting it does nothing")
    string(APPEND gs_hb_evidence "${gs_name}: UNRECOGNISED BY UPSTREAM\n")
    continue()
  endif()

  string(APPEND gs_hb_evidence
    "${gs_name}: declared=${gs_declared} actual=${gs_actual_bool}\n")

  if (NOT gs_actual_bool STREQUAL gs_declared)
    list(APPEND gs_hb_mismatches
      "${gs_name}: ADR 0002 §4 declares ${gs_declared}, resolved to ${gs_actual_bool}")
  endif()
endforeach()
unset(gs_hb_option)

file(WRITE "${CMAKE_BINARY_DIR}/harfbuzz_option_evidence.txt" "${gs_hb_evidence}")

if (gs_hb_mismatches)
  list(JOIN gs_hb_mismatches "\n  - " gs_hb_mismatch_text)
  message(FATAL_ERROR
    "HarfBuzz option verification failed (ADR 0002 §4):\n\n"
    "  - ${gs_hb_mismatch_text}\n\n"
    "An option resolving to a value other than the reviewed one means the "
    "shipped HarfBuzz build differs from the one whose licence surface was "
    "audited. Full evidence: ${CMAKE_BINARY_DIR}/harfbuzz_option_evidence.txt")
endif()

message(STATUS
  "HarfBuzz: ${GRAPHSCORE_HARFBUZZ_GIT_TAG}, all reviewed options verified "
  "(evidence: ${CMAKE_BINARY_DIR}/harfbuzz_option_evidence.txt)")

# The enforceable invariant itself (ADR 0002 §4, restated as an executable
# requirement in §A3): after HarfBuzz configures, no `freetype` target may
# appear in its LINK_LIBRARIES.
get_target_property(GRAPHSCORE_HARFBUZZ_LINK_LIBRARIES harfbuzz LINK_LIBRARIES)

set(GRAPHSCORE_HARFBUZZ_ISOLATION_OK TRUE)
set(GRAPHSCORE_HARFBUZZ_FREETYPE_PRESENT "NO")
if ("freetype" IN_LIST GRAPHSCORE_HARFBUZZ_LINK_LIBRARIES)
  set(GRAPHSCORE_HARFBUZZ_ISOLATION_OK FALSE)
  set(GRAPHSCORE_HARFBUZZ_FREETYPE_PRESENT "YES")
endif()

file(WRITE "${CMAKE_BINARY_DIR}/harfbuzz_isolation_evidence.txt"
"HarfBuzz/FreeType isolation evidence (ADR 0002 §4, M1 Verification Gate)
Pinned SHA:      ${GRAPHSCORE_HARFBUZZ_GIT_TAG}
harfbuzz target LINK_LIBRARIES: ${GRAPHSCORE_HARFBUZZ_LINK_LIBRARIES}
freetype target present in that list: ${GRAPHSCORE_HARFBUZZ_FREETYPE_PRESENT}
")

if (NOT GRAPHSCORE_HARFBUZZ_ISOLATION_OK)
  message(FATAL_ERROR
    "HarfBuzz unexpectedly linked to FreeType (ADR 0002 §4). This means a "
    "'freetype' CMake target already existed when cmake/HarfBuzz.cmake ran. "
    "Check that the root CMakeLists.txt include(HarfBuzz) call still "
    "precedes include(FreeType) -- see the ordering invariant comment at "
    "the top of this file. Full evidence: "
    "${CMAKE_BINARY_DIR}/harfbuzz_isolation_evidence.txt")
endif()

message(STATUS
  "HarfBuzz: ${GRAPHSCORE_HARFBUZZ_GIT_TAG}, isolation from FreeType "
  "verified (evidence: ${CMAKE_BINARY_DIR}/harfbuzz_isolation_evidence.txt)")
