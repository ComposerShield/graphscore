# SPDX-License-Identifier: Apache-2.0
#
# Fails configure with an actionable message on any compiler other than
# Clang, AppleClang, or clang-cl (whose CMAKE_CXX_COMPILER_ID is also
# "Clang"). Requires C++23 project-wide.

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  message(FATAL_ERROR
    "GraphScore requires Clang, AppleClang, or clang-cl. Detected CXX "
    "compiler: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER}).\n"
    "Use one of the checked-in CMake presets (see CMakePresets.json), or "
    "configure explicitly with -DCMAKE_C_COMPILER=clang "
    "-DCMAKE_CXX_COMPILER=clang++ (clang-cl on Windows).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
