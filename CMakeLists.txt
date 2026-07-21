# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(GraphScore
  VERSION 0.1.0
  DESCRIPTION "Graph-based adaptive music notation authoring and runtime"
  LANGUAGES CXX C
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerGate)
include(Warnings)
include(Sanitizers)
include(ConstCorrectness)
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
  include(SDL3)
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
