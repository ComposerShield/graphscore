# SPDX-License-Identifier: Apache-2.0
#
# GRAPHSCORE_SANITIZER selects a sanitizer suite for the whole build (the
# CMake presets asan-ubsan/tsan set this). Applied project-wide, including to
# fetched dependencies: a sanitizer only gives correct results if every
# translation unit in the binary is instrumented.

set(GRAPHSCORE_SANITIZER "NONE" CACHE STRING "Sanitizer suite: NONE, ASAN_UBSAN, or TSAN")
set_property(CACHE GRAPHSCORE_SANITIZER PROPERTY STRINGS NONE ASAN_UBSAN TSAN)

if (GRAPHSCORE_SANITIZER STREQUAL "ASAN_UBSAN")
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
elseif (GRAPHSCORE_SANITIZER STREQUAL "TSAN")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif (NOT GRAPHSCORE_SANITIZER STREQUAL "NONE")
  message(FATAL_ERROR "Unknown GRAPHSCORE_SANITIZER value: '${GRAPHSCORE_SANITIZER}' (expected NONE, ASAN_UBSAN, or TSAN)")
endif()
