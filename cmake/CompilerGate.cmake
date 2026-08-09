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

# Minimum compiler versions. GraphScore builds with the platform-native
# toolchain, so there are two floors rather than one pinned version: Apple's
# compiler versions are its own line and do not track upstream LLVM releases
# (there is no Apple Clang 18).
#
# The floors are set by observed C++23 feature support, not preference. Apple
# Clang 15 and 16 both reject P1091R3 (lambda capture of a structured
# binding); Apple Clang 17 is the first that accepts it. See AGENTS.md,
# "Toolchain policy".
if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  set(graphscore_minimum_compiler "17.0")
  set(graphscore_minimum_hint "Xcode 16.3 or newer")
else()
  set(graphscore_minimum_compiler "18.0")
  set(graphscore_minimum_hint "LLVM/Clang 18 or newer")
endif()

if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS graphscore_minimum_compiler)
  message(FATAL_ERROR
    "GraphScore requires ${CMAKE_CXX_COMPILER_ID} "
    "${graphscore_minimum_compiler} or newer (${graphscore_minimum_hint}). "
    "Detected ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} "
    "(${CMAKE_CXX_COMPILER}).\n"
    "Older releases miss C++23 features this codebase uses, so the failure "
    "would otherwise surface as an unrelated-looking template error deep in "
    "the build. See AGENTS.md, \"Toolchain policy\".")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
