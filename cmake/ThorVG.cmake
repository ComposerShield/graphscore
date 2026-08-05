# SPDX-License-Identifier: Apache-2.0
#
# ThorVG dependency adapter (ADR 0002 §A1, §A2; ADR 0003 §2.2/§8). ThorVG has
# no CMakeLists.txt at the pinned SHA -- its own build system is Meson,
# driven through Ninja (ADR 0002 §A1). This file:
#
#   1. Locates a Python 3.7+ interpreter (Meson's own minimum) and a host
#      Ninja >= 1.8.2 (Meson's own documented floor), both scoped to this
#      adapter (ADR 0002 §A2.2's correction: cmake/ArchitectureAudit.cmake's
#      existing, deliberately optional Python check must not be touched).
#   2. Fetches Meson's own source via FetchContent (never itself built --
#      pure Python, invoked directly) and ThorVG's own source the same way.
#   3. Drives ThorVG's `meson setup` / `ninja` / `ninja install` cycle as an
#      ExternalProject_Add build-graph step -- i.e. at build time, not this
#      configure pass (ADR 0002 §A1.3) -- then verifies the result
#      (cmake/thorvg_verify_options.cmake).
#   4. Exposes the installed static archive as a normal IMPORTED target.
#
# PRIVATE to graphscore_rendering: no ThorVG type may appear in a public
# header (ADR 0003 §2.2).
#
# Writer-only: include()d from the root CMakeLists.txt inside the same
# `if (GRAPHSCORE_BUILD_WRITER)` block that gates SDL3/FreeType/HarfBuzz, so
# `-DGRAPHSCORE_BUILD_WRITER=OFF` fetches and builds nothing here
# (ADR 0002 §A7.1).

include(FetchContent)
include(ExternalProject)

set(GRAPHSCORE_MESON_GIT_TAG "ff84a1ab2699385f67eea990260a20beb2b46c98")
set(GRAPHSCORE_THORVG_GIT_TAG "6d5933c9d1aca94635c6ad8129f3530ae554d423")

# ---------------------------------------------------------------------------
# Build-host tooling (ADR 0002 §A1, §A2.2)
# ---------------------------------------------------------------------------

# Meson's own build is driven entirely by a Python interpreter. Scoped here
# rather than raising cmake/ArchitectureAudit.cmake's existing bare
# find_package(Python3 COMPONENTS Interpreter QUIET) to a 3.7 REQUIRED floor
# (ADR 0002 §A2.2's correction): that file is include()d last, after ThorVG
# has already needed Python, and its own Python usage is deliberately
# optional (a missing interpreter there only disables four audit scripts
# with a warning).
find_package(Python3 3.7 COMPONENTS Interpreter REQUIRED)

# Meson's own documented Ninja floor (mesonbuild/backend/ninjabackend.py at
# the Meson pin: `ninja_required_version = 1.8.2`) -- not the version tagged
# at Ninja's own reviewed provenance SHA (ADR 0002 §A1.2, which is a
# license/provenance record only; GraphScore never builds Ninja).
find_program(GRAPHSCORE_NINJA_EXECUTABLE NAMES ninja ninja-build REQUIRED)

execute_process(
  COMMAND "${GRAPHSCORE_NINJA_EXECUTABLE}" --version
  OUTPUT_VARIABLE GRAPHSCORE_NINJA_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE GRAPHSCORE_NINJA_VERSION_RESULT
)
if (NOT GRAPHSCORE_NINJA_VERSION_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Could not run '${GRAPHSCORE_NINJA_EXECUTABLE} --version' "
    "(ADR 0002 §A1.2).")
endif()
if (GRAPHSCORE_NINJA_VERSION VERSION_LESS "1.8.2")
  message(FATAL_ERROR
    "Ninja ${GRAPHSCORE_NINJA_VERSION} at '${GRAPHSCORE_NINJA_EXECUTABLE}' "
    "is older than Meson's own documented minimum 1.8.2 (ADR 0002 §A1.2). "
    "Install a newer ninja/ninja-build.")
endif()

# ---------------------------------------------------------------------------
# Source acquisition (ADR 0002 §A1.1, §A2.1 step 1)
# ---------------------------------------------------------------------------

FetchContent_Declare(
  Meson
  GIT_REPOSITORY https://github.com/mesonbuild/meson.git
  GIT_TAG ${GRAPHSCORE_MESON_GIT_TAG}
  GIT_SHALLOW FALSE
  SYSTEM
)
FetchContent_MakeAvailable(Meson)

FetchContent_Declare(
  ThorVG
  GIT_REPOSITORY https://github.com/thorvg/thorvg.git
  GIT_TAG ${GRAPHSCORE_THORVG_GIT_TAG}
  GIT_SHALLOW FALSE
  SYSTEM
)
FetchContent_MakeAvailable(ThorVG)

# ---------------------------------------------------------------------------
# Option set (ADR 0002 §A2.2) and per-platform passthrough (ADR 0002 §A2.1
# step 2)
# ---------------------------------------------------------------------------

set(GRAPHSCORE_THORVG_BUILD_DIR "${CMAKE_BINARY_DIR}/thorvg-build")
set(GRAPHSCORE_THORVG_INSTALL_DIR "${CMAKE_BINARY_DIR}/thorvg-install")
set(GRAPHSCORE_THORVG_ARTIFACT
  "${GRAPHSCORE_THORVG_INSTALL_DIR}/lib/libthorvg-1.a")

# -Dincludedir=include (below) pins the top-level include/ directory, but the
# thorvg-1/ subdirectory under it comes from ThorVG's own
# install_headers(subdir: 'thorvg-' + vmaj) with no Meson option to pin it
# (ADR 0002 §A7.3's acknowledged fallback case: "it cannot apply to paths a
# third-party build system provides no option to pin... the fallback is an
# explicit existence check that fails loudly"). thorvg_verify_options.cmake
# checks this path exists after install.
set(GRAPHSCORE_THORVG_HEADER
  "${GRAPHSCORE_THORVG_INSTALL_DIR}/include/thorvg-1/thorvg.h")

# A separate Meson build does not inherit CMAKE_BUILD_TYPE the way a native
# CMake subproject does, so it is mapped explicitly (ADR 0002 §A2.2).
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(GRAPHSCORE_THORVG_BUILDTYPE "debug")
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  set(GRAPHSCORE_THORVG_BUILDTYPE "debugoptimized")
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
  set(GRAPHSCORE_THORVG_BUILDTYPE "release")
elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  set(GRAPHSCORE_THORVG_BUILDTYPE "minsize")
else()
  message(FATAL_ERROR
    "Unrecognised CMAKE_BUILD_TYPE '${CMAKE_BUILD_TYPE}' for the ThorVG "
    "adapter (ADR 0002 §A2.2): no Meson --buildtype mapping is defined for "
    "it, so it would previously have silently mapped to 'debug' regardless "
    "of what was actually requested. Recognised values: Debug, Release, "
    "RelWithDebInfo, MinSizeRel -- every preset in CMakePresets.json sets "
    "one of these. Configure with one of GraphScore's own presets, or "
    "extend this mapping if a new CMAKE_BUILD_TYPE is intentionally added.")
endif()

set(GRAPHSCORE_THORVG_MESON_OPTIONS
  "-Dengines=cpu"
  "-Dpartial=true"
  "-Dloaders="
  "-Dsavers="
  "-Dthreads=true"
  "-Dsimd=false"
  "-Dbindings="
  "-Dtools="
  "-Dtests=false"
  "-Dlog=false"
  "-Ddefault_library=static"
  "-Dstatic=true"
  "-Dlibdir=lib"
  "-Dincludedir=include"
  "-Dnamingscheme=classic"
  "-Dfile=true"
  "-Dextra="
  "-Dwerror=false"
)

# Sanitizer presets instrument GraphScore's own translation units; an
# uninstrumented third-party static archive linked into an otherwise
# instrumented binary is the standard source of TSan false positives (ADR
# 0002 §A2.1 step 2). GRAPHSCORE_SANITIZER is set by cmake/Sanitizers.cmake.
if (GRAPHSCORE_SANITIZER STREQUAL "ASAN_UBSAN" OR
    GRAPHSCORE_SANITIZER STREQUAL "TSAN")
  list(APPEND GRAPHSCORE_THORVG_MESON_OPTIONS "-Db_sanitize=none")
endif()

# Windows CRT selection must match the rest of GraphScore's own build, or
# ThorVG's separately built archive uses a different CRT than the writer
# executable that links it (ADR 0002 §A2.1 step 2). Only literal (non
# generator-expression) CMAKE_MSVC_RUNTIME_LIBRARY values are translated;
# this project's presets do not set the variable today. CMake's documented
# default spelling is MultiThreaded$<$<CONFIG:Debug>:Debug>DLL -- lowercased,
# that string matches both "dll" and "debug", which would silently resolve
# to -Db_vscrt=mdd (the debug CRT) regardless of the actual configuration a
# genex would select. Guard against a genex value explicitly rather than
# letting it fall through to a wrong-but-plausible translation.
if (WIN32 AND CMAKE_MSVC_RUNTIME_LIBRARY)
  if (CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "\\$<")
    message(FATAL_ERROR
      "CMAKE_MSVC_RUNTIME_LIBRARY is a generator expression "
      "('${CMAKE_MSVC_RUNTIME_LIBRARY}'): the ThorVG adapter's -Db_vscrt "
      "translation below only handles a literal value, and lowercasing a "
      "genex like the documented default "
      "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL would match both 'dll' and "
      "'debug' and silently select the debug CRT regardless of the actual "
      "build configuration. Set CMAKE_MSVC_RUNTIME_LIBRARY to a literal "
      "value, or extend this adapter to resolve the genex per-configuration "
      "before translating it.")
  endif()
  string(TOLOWER "${CMAKE_MSVC_RUNTIME_LIBRARY}" GRAPHSCORE_MSVC_CRT_LOWER)
  if (GRAPHSCORE_MSVC_CRT_LOWER MATCHES "dll")
    if (GRAPHSCORE_MSVC_CRT_LOWER MATCHES "debug")
      set(GRAPHSCORE_THORVG_VSCRT "mdd")
    else()
      set(GRAPHSCORE_THORVG_VSCRT "md")
    endif()
  else()
    if (GRAPHSCORE_MSVC_CRT_LOWER MATCHES "debug")
      set(GRAPHSCORE_THORVG_VSCRT "mtd")
    else()
      set(GRAPHSCORE_THORVG_VSCRT "mt")
    endif()
  endif()
  list(APPEND GRAPHSCORE_THORVG_MESON_OPTIONS
    "-Db_vscrt=${GRAPHSCORE_THORVG_VSCRT}")
endif()

# macOS deployment target / SDK / architecture must match the rest of
# GraphScore's own build for the same ABI-compatibility reason (ADR 0002
# §A2.1 step 2). This project's presets do not set these variables today, so
# this passthrough is exercised only if a future preset or invocation does.
#
# Multi-arch (CMAKE_OSX_ARCHITECTURES with two or more values, e.g. a
# "arm64;x86_64" universal-binary configuration) is NOT supported here:
# passing "-arch arm64 -arch x86_64" through -Dc_args/-Dcpp_args is not how
# Meson performs universal builds (there is no single-invocation multi-slice
# compile the way Xcode's own build system does it) and would either fail
# outright or silently produce a single-architecture archive depending on
# how the underlying compiler driver resolves conflicting -arch flags. Fail
# loudly rather than appear to support what is not actually supported.
if (APPLE)
  list(LENGTH CMAKE_OSX_ARCHITECTURES GRAPHSCORE_OSX_ARCH_COUNT)
  if (GRAPHSCORE_OSX_ARCH_COUNT GREATER 1)
    message(FATAL_ERROR
      "CMAKE_OSX_ARCHITECTURES ('${CMAKE_OSX_ARCHITECTURES}') names more "
      "than one architecture. The ThorVG adapter's -Dc_args/-Dcpp_args "
      "passthrough does not support multi-architecture (universal binary) "
      "builds -- Meson has no equivalent to Xcode's single-invocation "
      "multi-slice compile. Configure a single-architecture build, or "
      "extend cmake/ThorVG.cmake with a real Meson cross-file-based "
      "universal-binary strategy before using this option.")
  endif()
endif()

set(GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS "")
if (APPLE)
  if (CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS
      "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
  if (CMAKE_OSX_SYSROOT)
    list(APPEND GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS
      "-isysroot" "${CMAKE_OSX_SYSROOT}")
  endif()
  foreach (GRAPHSCORE_OSX_ARCH IN LISTS CMAKE_OSX_ARCHITECTURES)
    list(APPEND GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS "-arch" "${GRAPHSCORE_OSX_ARCH}")
  endforeach()
endif()
if (GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS)
  # Meson listifies -Dc_args/-Dcpp_args via shlex (split_args), so each
  # element must be individually quoted before joining -- otherwise a value
  # containing a space (e.g. a CMAKE_OSX_SYSROOT path with a space in it)
  # would silently split into two broken arguments instead of the one
  # argument it actually is.
  set(GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS_QUOTED "")
  foreach (GRAPHSCORE_ARG IN LISTS GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS)
    list(APPEND GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS_QUOTED
      "\"${GRAPHSCORE_ARG}\"")
  endforeach()
  list(JOIN GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS_QUOTED " "
    GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS_JOINED)
  list(APPEND GRAPHSCORE_THORVG_MESON_OPTIONS
    "-Dc_args=${GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS_JOINED}"
    "-Dcpp_args=${GRAPHSCORE_THORVG_EXTRA_COMPILE_ARGS_JOINED}")
endif()

# ---------------------------------------------------------------------------
# ExternalProject_Add: meson setup / ninja / ninja install as build-graph
# steps (ADR 0002 §A1.3, §A2.1 step 2)
# ---------------------------------------------------------------------------

# meson.py is Meson's own entrypoint (ADR 0002 §A1.1); a future Meson pin
# that moves or renames it would otherwise fail only at build time, deep
# inside ExternalProject's CONFIGURE_COMMAND, with a bare Python "can't open
# file" error and no indication of which pin or path is at fault. Fail at
# configure time instead, with a diagnostic that names the path checked.
if (NOT EXISTS "${meson_SOURCE_DIR}/meson.py")
  message(FATAL_ERROR
    "Meson's entrypoint was not found at "
    "'${meson_SOURCE_DIR}/meson.py' (ADR 0002 §A1.1, pinned commit "
    "${GRAPHSCORE_MESON_GIT_TAG}). Either the fetch failed, or a future "
    "Meson pin has moved/renamed its own entrypoint -- update the path this "
    "adapter invokes to match.")
endif()

ExternalProject_Add(ThorVGBuild
  DOWNLOAD_COMMAND ""
  SOURCE_DIR "${thorvg_SOURCE_DIR}"
  BINARY_DIR "${GRAPHSCORE_THORVG_BUILD_DIR}"
  INSTALL_DIR "${GRAPHSCORE_THORVG_INSTALL_DIR}"
  CONFIGURE_COMMAND
    "${CMAKE_COMMAND}" -E env
      "CC=${CMAKE_C_COMPILER}"
      "CXX=${CMAKE_CXX_COMPILER}"
    "${Python3_EXECUTABLE}" "${meson_SOURCE_DIR}/meson.py" setup
      "${GRAPHSCORE_THORVG_BUILD_DIR}" "${thorvg_SOURCE_DIR}"
      --backend=ninja
      --wrap-mode=nodownload
      --buildtype=${GRAPHSCORE_THORVG_BUILDTYPE}
      --prefix=${GRAPHSCORE_THORVG_INSTALL_DIR}
      ${GRAPHSCORE_THORVG_MESON_OPTIONS}
  BUILD_COMMAND "${GRAPHSCORE_NINJA_EXECUTABLE}" -C "${GRAPHSCORE_THORVG_BUILD_DIR}"
  INSTALL_COMMAND "${GRAPHSCORE_NINJA_EXECUTABLE}" -C "${GRAPHSCORE_THORVG_BUILD_DIR}" install
  BUILD_BYPRODUCTS "${GRAPHSCORE_THORVG_ARTIFACT}"
  USES_TERMINAL_CONFIGURE TRUE
  USES_TERMINAL_BUILD TRUE
  USES_TERMINAL_INSTALL TRUE
)

# ADR 0002 §A2.3's build verification design: declared-vs-actual option
# readback plus an artifact-existence check, run as a build-graph step
# because ThorVG's own configure/build happen at build time (§A1.3).
set(GRAPHSCORE_THORVG_EVIDENCE_FILE "${CMAKE_BINARY_DIR}/thorvg_option_evidence.txt")

ExternalProject_Add_Step(ThorVGBuild verify_options
  DEPENDEES install
  COMMAND "${CMAKE_COMMAND}"
    -D "GRAPHSCORE_THORVG_BUILD_DIR=${GRAPHSCORE_THORVG_BUILD_DIR}"
    -D "GRAPHSCORE_THORVG_ARTIFACT=${GRAPHSCORE_THORVG_ARTIFACT}"
    -D "GRAPHSCORE_THORVG_HEADER=${GRAPHSCORE_THORVG_HEADER}"
    -D "GRAPHSCORE_THORVG_EVIDENCE_FILE=${GRAPHSCORE_THORVG_EVIDENCE_FILE}"
    -P "${CMAKE_SOURCE_DIR}/cmake/thorvg_verify_options.cmake"
  USES_TERMINAL TRUE
)

# Exposes ThorVGBuild-verify_options as an ordinary target so `thorvg` can
# depend on the verification having actually completed -- not merely on
# `install`, which is all ThorVGBuild's own completion stamp waits for by
# default.
ExternalProject_Add_StepTargets(ThorVGBuild verify_options)

# ---------------------------------------------------------------------------
# IMPORTED target (ADR 0002 §A2.1 step 3, ADR 0003 §2.2)
# ---------------------------------------------------------------------------

add_library(thorvg STATIC IMPORTED GLOBAL)

set_target_properties(thorvg PROPERTIES
  IMPORTED_LOCATION "${GRAPHSCORE_THORVG_ARTIFACT}")

# CMake requires an INTERFACE_INCLUDE_DIRECTORIES entry to exist at generate
# time even though ThorVG's own install step (which actually populates it)
# runs later, at build time (ADR 0002 §A1.3) -- the standard ExternalProject
# + IMPORTED-target pattern.
file(MAKE_DIRECTORY "${GRAPHSCORE_THORVG_INSTALL_DIR}/include/thorvg-1")

# inc/thorvg.h declares every TVG_API symbol dllimport by default on Windows
# unless the consumer also defines TVG_STATIC (ADR 0002 §A2.1 step 3);
# ThorVG's own Meson build supplies this automatically only for consumers
# reached through Meson's own dependency resolution, which this hand-rolled
# IMPORTED target is not.
target_compile_definitions(thorvg INTERFACE TVG_STATIC)

# ThorVG's inc/meson.build installs its one public header into a
# version-qualified subdirectory (thorvg-1/thorvg.h), not directly under
# include/ (ADR 0002 §A2.1 step 3). Marked SYSTEM so ThorVG's own header
# content is exempt from GraphScore's -Werror flag set.
target_include_directories(thorvg SYSTEM INTERFACE
  "${GRAPHSCORE_THORVG_INSTALL_DIR}/include/thorvg-1")

add_dependencies(thorvg ThorVGBuild ThorVGBuild-verify_options)

message(STATUS
  "ThorVG: ${GRAPHSCORE_THORVG_GIT_TAG}, Meson ${GRAPHSCORE_MESON_GIT_TAG}, "
  "Ninja ${GRAPHSCORE_NINJA_VERSION} at ${GRAPHSCORE_NINJA_EXECUTABLE}")
