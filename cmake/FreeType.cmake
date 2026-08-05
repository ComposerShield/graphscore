# SPDX-License-Identifier: Apache-2.0
#
# FreeType dependency adapter (ADR 0002 §5, §A3). FreeType is PRIVATE to
# graphscore_rendering: no FreeType type appears in a public header
# (ADR 0003 §2.2).
#
# Writer-only: include()d from the root CMakeLists.txt inside the same
# `if (GRAPHSCORE_BUILD_WRITER)` block that gates SDL3, so
# `-DGRAPHSCORE_BUILD_WRITER=OFF` fetches nothing here (ADR 0002 §A7.1).
#
# ORDERING INVARIANT (ADR 0002 §4, §A3) -- must be include()d strictly after
# cmake/HarfBuzz.cmake. HarfBuzz's own forced-link guard fires only when a
# `freetype` CMake target already exists at the point HarfBuzz configures;
# HarfBuzz.cmake asserts that guard did not fire, which is only true if
# FreeType has not been configured yet. FT_DISABLE_HARFBUZZ below severs the
# dependency in the other direction; FreeType's own CMakeLists.txt has no
# forced-link guard analogous to HarfBuzz's, so this direction needs no
# equivalent introspection check (ADR 0002 §4).
#
# ADR 0002 §5 records that FreeType needs no M1-style *adapter* gate of the
# kind HarfBuzz's forced-link risk or ThorVG's separate build-tool invocation
# demand: it supports add_subdirectory directly and its CMake integration is a
# plain option set. The declared-vs-actual readback below is nonetheless
# applied here for the same reason it is applied to SDL3 and HarfBuzz -- a
# forced cache value that upstream renames or overwrites becomes a silent
# no-op, and several of these options gate optional system dependencies. Note
# FreeType rewrites its own FT_REQUIRE_* entries from BOOL to INTERNAL, which
# is exactly the kind of upstream overwrite a readback exists to observe.

include(FetchContent)

set(GRAPHSCORE_FREETYPE_GIT_TAG "f01dec5e676847267834b881b25f6e8c79581163")

# ADR 0002 §5's constrained option table: a self-contained FreeType with
# zero optional system dependencies and no HarfBuzz integration. Same
# "NAME=VALUE list, apply, then read back against upstream" discipline as
# cmake/SDL3.cmake's GRAPHSCORE_SDL3_OPTIONS loop -- a future pin that
# renames one of these (FreeType's own CMakeLists.txt rewrites the
# FT_REQUIRE_* entries from BOOL to INTERNAL cache type at configure time,
# which proves upstream does actively consume them today) would otherwise
# leave the forced cache entry silently inert.
set(GRAPHSCORE_FREETYPE_OPTIONS
  FT_DISABLE_ZLIB=ON
  FT_DISABLE_BZIP2=ON
  FT_DISABLE_PNG=ON
  FT_DISABLE_HARFBUZZ=ON
  FT_DISABLE_BROTLI=ON
  FT_DISABLE_HVF=ON
  FT_REQUIRE_ZLIB=OFF
  FT_REQUIRE_BZIP2=OFF
  FT_REQUIRE_PNG=OFF
  FT_REQUIRE_HARFBUZZ=OFF
  FT_REQUIRE_BROTLI=OFF
)

foreach (gs_ft_option IN LISTS GRAPHSCORE_FREETYPE_OPTIONS)
  if (NOT gs_ft_option MATCHES "^([A-Za-z0-9_]+)=(.+)$")
    message(FATAL_ERROR "Malformed FreeType option entry: '${gs_ft_option}'")
  endif()
  set(${CMAKE_MATCH_1} ${CMAKE_MATCH_2} CACHE BOOL "" FORCE)
endforeach()
unset(gs_ft_option)

FetchContent_Declare(
  FreeType
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG ${GRAPHSCORE_FREETYPE_GIT_TAG}
  GIT_SHALLOW FALSE
  # FreeType's own warnings are not GraphScore build failures (ADR 0001,
  # "Warning isolation").
  SYSTEM
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(FreeType)

# Declared-vs-actual readback plus an upstream name-presence cross-check,
# lifted from cmake/SDL3.cmake's M1 Verification Gate. Value comparison
# alone is not enough: because this adapter creates each cache entry
# itself, a name FreeType no longer recognises (a typo, or an option
# renamed since this pin was reviewed) would be created, read back
# unchanged, and "verified", while having no effect on the build.
file(READ "${freetype_SOURCE_DIR}/CMakeLists.txt" gs_ft_upstream_cmakelists)

set(gs_ft_mismatches "")
set(gs_ft_evidence
"FreeType option evidence (ADR 0002 §5)
Pinned SHA: ${GRAPHSCORE_FREETYPE_GIT_TAG}

")

foreach (gs_ft_option IN LISTS GRAPHSCORE_FREETYPE_OPTIONS)
  string(REGEX MATCH "^([A-Za-z0-9_]+)=(.+)$" _ "${gs_ft_option}")
  set(gs_name "${CMAKE_MATCH_1}")
  set(gs_declared "${CMAKE_MATCH_2}")
  set(gs_actual "${${gs_name}}")

  if (gs_actual)
    set(gs_actual_bool ON)
  else()
    set(gs_actual_bool OFF)
  endif()

  if (NOT gs_ft_upstream_cmakelists MATCHES "[^A-Za-z0-9_]${gs_name}[^A-Za-z0-9_]")
    list(APPEND gs_ft_mismatches
      "${gs_name}: not referenced anywhere in FreeType's CMakeLists at this pin, so setting it does nothing")
    string(APPEND gs_ft_evidence "${gs_name}: UNRECOGNISED BY UPSTREAM\n")
    continue()
  endif()

  string(APPEND gs_ft_evidence
    "${gs_name}: declared=${gs_declared} actual=${gs_actual_bool}\n")

  if (NOT gs_actual_bool STREQUAL gs_declared)
    list(APPEND gs_ft_mismatches
      "${gs_name}: ADR 0002 §5 declares ${gs_declared}, resolved to ${gs_actual_bool}")
  endif()
endforeach()
unset(gs_ft_option)

file(WRITE "${CMAKE_BINARY_DIR}/freetype_option_evidence.txt" "${gs_ft_evidence}")

if (gs_ft_mismatches)
  list(JOIN gs_ft_mismatches "\n  - " gs_ft_mismatch_text)
  message(FATAL_ERROR
    "FreeType option verification failed (ADR 0002 §5):\n\n"
    "  - ${gs_ft_mismatch_text}\n\n"
    "An option resolving to a value other than the reviewed one means the "
    "shipped FreeType build differs from the one whose option surface was "
    "audited. Full evidence: ${CMAKE_BINARY_DIR}/freetype_option_evidence.txt")
endif()

message(STATUS
  "FreeType: ${GRAPHSCORE_FREETYPE_GIT_TAG}, all reviewed options verified, "
  "HarfBuzz integration disabled (evidence: "
  "${CMAKE_BINARY_DIR}/freetype_option_evidence.txt)")
