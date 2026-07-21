# SPDX-License-Identifier: Apache-2.0
#
# GRAPHSCORE_SANITIZER selects a sanitizer suite for the whole build (the
# CMake presets asan-ubsan/tsan set this). Applied project-wide, including to
# fetched dependencies: a sanitizer only gives correct results if every
# translation unit in the binary is instrumented.

set(GRAPHSCORE_SANITIZER "NONE" CACHE STRING "Sanitizer suite: NONE, ASAN_UBSAN, or TSAN")
set_property(CACHE GRAPHSCORE_SANITIZER PROPERTY STRINGS NONE ASAN_UBSAN TSAN)

set(GRAPHSCORE_SANITIZER_COMPILE_OPTIONS "")
set(GRAPHSCORE_SANITIZER_LINK_OPTIONS "")

if (GRAPHSCORE_SANITIZER STREQUAL "ASAN_UBSAN")
  set(GRAPHSCORE_SANITIZER_COMPILE_OPTIONS
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  set(GRAPHSCORE_SANITIZER_LINK_OPTIONS -fsanitize=address,undefined)
elseif (GRAPHSCORE_SANITIZER STREQUAL "TSAN")
  set(GRAPHSCORE_SANITIZER_COMPILE_OPTIONS
    -fsanitize=thread -fno-omit-frame-pointer)
  set(GRAPHSCORE_SANITIZER_LINK_OPTIONS -fsanitize=thread)
elseif (NOT GRAPHSCORE_SANITIZER STREQUAL "NONE")
  message(FATAL_ERROR "Unknown GRAPHSCORE_SANITIZER value: '${GRAPHSCORE_SANITIZER}' (expected NONE, ASAN_UBSAN, or TSAN)")
endif()

add_compile_options(${GRAPHSCORE_SANITIZER_COMPILE_OPTIONS})
add_link_options(${GRAPHSCORE_SANITIZER_LINK_OPTIONS})

# An instrumented shared library carries unresolved references to its
# sanitizer runtime (`__asan_*`, `__tsan_*`), which the *consuming* link is
# what supplies. So anything built outside this build tree that links
# graphscore_runtime — the out-of-tree consumer projects in tests/cmake —
# must be compiled and linked with the same sanitizer flags, or it fails to
# link. These two variables are the space-separated form those separately
# configured projects are handed.
list(JOIN GRAPHSCORE_SANITIZER_COMPILE_OPTIONS " "
  GRAPHSCORE_SANITIZER_COMPILE_FLAGS)
list(JOIN GRAPHSCORE_SANITIZER_LINK_OPTIONS " "
  GRAPHSCORE_SANITIZER_LINK_FLAGS)
