# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

# CMP0156 de-duplicates libraries based on linker capabilities, but only
# CMP0179 guarantees that the first static-library occurrence is retained.
# Enable them together so CMake 3.29-3.30 cannot change direct-link ordering.
set(GRAPHSCORE_HAS_ORDERED_STATIC_LIBRARY_DEDUPLICATION FALSE)
if (POLICY CMP0156 AND POLICY CMP0179)
  cmake_policy(SET CMP0156 NEW)
  cmake_policy(SET CMP0179 NEW)
  set(GRAPHSCORE_HAS_ORDERED_STATIC_LIBRARY_DEDUPLICATION TRUE)
elseif (POLICY CMP0156)
  cmake_policy(SET CMP0156 OLD)
endif()

project(GraphScore
  VERSION 0.1.0
  DESCRIPTION "Graph-based adaptive music notation authoring and runtime"
  LANGUAGES CXX C
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerGate)
# Before any target is created, and before the FetchContent dependencies are
# configured below, so both inherit the compiler launcher.
include(Ccache)
include(Warnings)
include(Sanitizers)
include(Lint)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_property(GRAPHSCORE_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT GRAPHSCORE_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(GRAPHSCORE_BUILD_TESTS "Build GraphScore GTest suites" ON)

# The writer's windowing backend (SDL3). OFF produces a runtime-only build
# that fetches no writer dependency at all — useful for engine integrators
# and for CI jobs that only exercise the runtime. Every target still exists
# and still satisfies the architecture audit; graphscore_writer_shell simply
# compiles without a backend.
option(GRAPHSCORE_BUILD_WRITER
  "Build the writer shell against its windowing backend (SDL3)" ON)

if (GRAPHSCORE_BUILD_TESTS)
  include(CTest)
  include(dependencies)
endif()

if (GRAPHSCORE_BUILD_WRITER)
  # HarfBuzz must precede FreeType: HarfBuzz's own CMakeLists.txt contains a
  # non-overridable guard that forces a link to FreeType if a `freetype`
  # target already exists at the point HarfBuzz is configured (ADR 0002 §4,
  # §A3). Do not reorder these two include() calls.
  include(HarfBuzz)
  include(FreeType)
  include(SDL3)
  # ThorVG and Bravura carry no ordering constraint relative to the three
  # include() calls above or to each other (ADR 0002 §A7.1).
  include(ThorVG)
  include(Bravura)
endif()

add_subdirectory(src)
add_subdirectory(apps)

# After src/, so every runtime-closure target exists to be exported.
include(RuntimePackage)

if (GRAPHSCORE_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Must come last: the audit dumps the effective target graph, so every
# target it constrains has to exist by this point (ADR 0003 §7).
include(ArchitectureAudit)
