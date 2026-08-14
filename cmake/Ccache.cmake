# SPDX-License-Identifier: Apache-2.0
#
# Compiler cache (ccache) wiring.
#
# ---------------------------------------------------------------------------
# Why this is a module rather than a flag on each command
# ---------------------------------------------------------------------------
#
# Every configure path in this repository benefits: the presets, the scratch
# tree `graphscore_offline_dependencies` configures, and any tree a developer
# configures by hand. Auto-detecting here means no command in AGENTS.md has
# to carry a `-DCMAKE_CXX_COMPILER_LAUNCHER=` argument, and a machine without
# ccache installed keeps working unchanged.
#
# The launcher is set as a directory-scope variable at the top level, so it is
# inherited by every target created afterwards — including the FetchContent
# third-party subprojects (SDL3, FreeType, HarfBuzz), which are the largest
# single cost of a from-scratch writer build. ThorVG is the exception: it is
# driven through its own Meson/Ninja build (cmake/ThorVG.cmake), not as a
# CMake subproject, so this variable never reaches it. Meson performs its own
# ccache detection from the PATH, which is the only mechanism available there.
#
# ---------------------------------------------------------------------------
# CMake wiring
# ---------------------------------------------------------------------------

option(GRAPHSCORE_USE_CCACHE
  "Route C/C++ compiles through ccache when it is installed" ON)

if (NOT GRAPHSCORE_USE_CCACHE)
  return()
endif()

# HINTS covers the Homebrew prefixes: a Git hook can inherit a PATH that does
# not include them (Git invokes hooks with /bin/sh, and a GUI Git client
# passes down the login environment rather than an interactive shell's), and
# the hooks configure a build tree exactly like any other caller.
find_program(GRAPHSCORE_CCACHE_EXECUTABLE
  NAMES ccache
  HINTS /opt/homebrew/bin /usr/local/bin)

if (NOT GRAPHSCORE_CCACHE_EXECUTABLE)
  message(STATUS
    "ccache: not found, compiling without a compiler cache "
    "(install: brew install ccache | apt install ccache)")
  return()
endif()

# Honour an explicit -DCMAKE_<LANG>_COMPILER_LAUNCHER= on the command line:
# a caller who named a launcher outranks this module's default.
#
# Only the Makefile and Ninja generators act on these variables; the Xcode and
# Visual Studio generators ignore them silently. Every preset in
# CMakePresets.json uses Ninja.
foreach (graphscore_ccache_lang IN ITEMS C CXX)
  if (NOT CMAKE_${graphscore_ccache_lang}_COMPILER_LAUNCHER)
    set(CMAKE_${graphscore_ccache_lang}_COMPILER_LAUNCHER
      "${GRAPHSCORE_CCACHE_EXECUTABLE}")
  endif()
endforeach()

message(STATUS "ccache: ${GRAPHSCORE_CCACHE_EXECUTABLE}")
