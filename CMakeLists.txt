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

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_property(GRAPHSCORE_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT GRAPHSCORE_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(GRAPHSCORE_BUILD_TESTS "Build GraphScore GTest suites" ON)

if (GRAPHSCORE_BUILD_TESTS)
  include(CTest)
  include(dependencies)
endif()

add_subdirectory(src)
add_subdirectory(apps)

if (GRAPHSCORE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
