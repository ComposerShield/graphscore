# SPDX-License-Identifier: Apache-2.0
#
# ThorVG build verification (ADR 0002 §A2.3). Invoked in CMake script mode
# (`cmake -P`) as an ExternalProject_Add_Step(ThorVGBuild verify_options
# DEPENDEES install ...) custom step, because ThorVG's own configure/build
# happen at build time (ADR 0002 §A1.3) rather than at CMake configure time
# where an equivalent check (e.g. SDL3's) can run directly.
#
# Two independent checks, per ADR 0002 §A2.3:
#
#   1. Declared-vs-actual option readback: every option in the table below
#      is cross-checked against Meson's own
#      <build-dir>/meson-info/intro-buildoptions.json, the same
#      "declare, then verify by reading results back" discipline as the
#      SDL3 M1 Verification Gate (ADR 0002 §1). A name absent from that
#      file would mean Meson never recognised it -- in practice `meson
#      setup` itself already fails configure hard on an unrecognised `-D`
#      option (verified empirically: "ERROR: Unknown option" with a nonzero
#      exit code), so reaching this step at all is already evidence every
#      declared name was recognised; the "found" check below still reports
#      that explicitly rather than assuming it.
#   2. Artifact-existence check: the static archive named in
#      cmake/ThorVG.cmake's BUILD_BYPRODUCTS, and the installed public
#      header at GRAPHSCORE_THORVG_HEADER, must actually exist on disk. The
#      option readback proves Meson's own option store reports the values
#      GraphScore requested; it does not by itself prove the build those
#      options were supposed to produce actually exists in the form expected
#      (ADR 0002 §A2.3). The header path in particular is pinned by no
#      Meson option (ADR 0002 §A7.3's acknowledged fallback case) -- this is
#      the loud existence check that stands in for a pinning option that
#      does not exist. cmake/ThorVG.cmake's own
#      file(MAKE_DIRECTORY "${install-dir}/include/thorvg-1") (needed so the
#      IMPORTED target's INTERFACE_INCLUDE_DIRECTORIES exists at generate
#      time) would make a bare directory-existence check pass vacuously even
#      if ThorVG's own install step never ran, so this checks the header
#      file itself, not merely its parent directory.
#
# Required variables (passed via -D from the ExternalProject_Add_Step
# COMMAND in cmake/ThorVG.cmake):
#   GRAPHSCORE_THORVG_BUILD_DIR    ThorVG's own Meson build directory.
#   GRAPHSCORE_THORVG_ARTIFACT     Expected path to the installed static
#                                  archive.
#   GRAPHSCORE_THORVG_HEADER       Expected path to the installed public
#                                  header (thorvg-1/thorvg.h).
#   GRAPHSCORE_THORVG_EVIDENCE_FILE
#                                  Where to write the full evidence report.

cmake_minimum_required(VERSION 3.25)

if (NOT DEFINED GRAPHSCORE_THORVG_BUILD_DIR OR
    NOT DEFINED GRAPHSCORE_THORVG_ARTIFACT OR
    NOT DEFINED GRAPHSCORE_THORVG_HEADER OR
    NOT DEFINED GRAPHSCORE_THORVG_EVIDENCE_FILE)
  message(FATAL_ERROR
    "thorvg_verify_options.cmake requires GRAPHSCORE_THORVG_BUILD_DIR, "
    "GRAPHSCORE_THORVG_ARTIFACT, GRAPHSCORE_THORVG_HEADER, and "
    "GRAPHSCORE_THORVG_EVIDENCE_FILE")
endif()

# ADR 0002 §A2.2's option table. type is one of: bool, string, array. array
# values are the comma-joined element list Meson's own introspection reports
# ("" for an empty array).
set(GRAPHSCORE_THORVG_DECLARED_OPTIONS
  "engines|array|cpu"
  "partial|bool|true"
  "loaders|array|"
  "savers|array|"
  "threads|bool|true"
  "simd|bool|false"
  "bindings|array|"
  "tools|array|"
  "tests|bool|false"
  "log|bool|false"
  "default_library|string|static"
  "static|bool|true"
  "libdir|string|lib"
  "includedir|string|include"
  "file|bool|true"
  "extra|array|"
  "werror|bool|false"
)

# `namingscheme` is passed to `meson setup` (cmake/ThorVG.cmake) and is
# genuinely a Meson builtin option -- `mesonbuild/options.py`'s
# BUILTIN_CORE_OPTIONS table lists it, `meson setup` accepts
# `-Dnamingscheme=classic` without error, and its own "User defined
# options" configure-log summary echoes it back. It cannot, however, be
# cross-checked against intro-buildoptions.json the way every other option
# above is: at the pinned Meson SHA, `mesonbuild/options.py`'s separate
# `_BUILTIN_NAMES` set (consulted by `OptionStore.is_builtin_option`, which
# `mintro.py`'s `list_buildoptions` uses to decide what to include) omits
# `namingscheme` even though `BUILTIN_CORE_OPTIONS` includes it -- confirmed
# empirically: `namingscheme` never appears in intro-buildoptions.json's
# output at this pin, while sibling core builtins like `layout` and `unity`
# do. This is the acknowledged limit ADR 0002 §A7.3 names: "it cannot apply
# to paths a third-party build system provides no option to pin... Where no
# option exists, the fallback is an explicit existence check that fails
# loudly." The artifact-existence check below is exactly that fallback for
# `namingscheme`: had it not resolved to 'classic', the installed archive
# would be named `thorvg-1.lib` (Windows) rather than `libthorvg-1.a`, and
# the check for the exact expected path would already fail.
#
# That artifact check discriminates only on Windows, though: on macOS and
# Linux, both `classic` and `platform` naming schemes produce the identical
# `libthorvg-1.a` path (the `.lib`/`.a`-suffix distinction `namingscheme`
# actually controls is a Windows-only naming-convention choice; POSIX static
# archives are named identically either way), so a `namingscheme` value that
# silently resolved to something other than `classic` would pass the
# artifact check vacuously on those platforms. The stronger guarantee is
# what `meson setup` itself does: it hard-fails on any unrecognised `-D`
# option -- verified empirically at this pin, `meson setup
# -Dbogusoption=1 ...` exits nonzero with `ERROR: Unknown option:
# "bogusoption"` before any build file is even written -- so a future pin
# that renames or removes `namingscheme` fails loudly at `meson setup`
# itself, on every platform, rather than silently at this narrower
# Windows-only artifact check.

set(intro_file "${GRAPHSCORE_THORVG_BUILD_DIR}/meson-info/intro-buildoptions.json")
if (NOT EXISTS "${intro_file}")
  message(FATAL_ERROR
    "ThorVG build verification (ADR 0002 §A2.3): '${intro_file}' does not "
    "exist. meson setup either failed or wrote its introspection data "
    "somewhere else.")
endif()

file(READ "${intro_file}" intro_json)
string(JSON option_count LENGTH "${intro_json}")
math(EXPR last_option_index "${option_count} - 1")

set(mismatches "")
set(evidence
"ThorVG option evidence (ADR 0002 §A2.2, §A2.3 verification)

Build dir: ${GRAPHSCORE_THORVG_BUILD_DIR}

")

foreach (declared_entry IN LISTS GRAPHSCORE_THORVG_DECLARED_OPTIONS)
  string(REPLACE "|" ";" declared_parts "${declared_entry}")
  list(GET declared_parts 0 opt_name)
  list(GET declared_parts 1 opt_type)
  list(LENGTH declared_parts declared_parts_len)
  if (declared_parts_len GREATER 2)
    list(GET declared_parts 2 opt_value)
  else()
    set(opt_value "")
  endif()

  set(found FALSE)
  set(actual "<not found>")

  foreach (i RANGE ${last_option_index})
    string(JSON entry_name GET "${intro_json}" ${i} "name")
    if (NOT entry_name STREQUAL opt_name)
      continue()
    endif()
    set(found TRUE)

    if (opt_type STREQUAL "array")
      string(JSON arr_len LENGTH "${intro_json}" ${i} "value")
      set(elems "")
      if (arr_len GREATER 0)
        math(EXPR arr_last_index "${arr_len} - 1")
        foreach (j RANGE ${arr_last_index})
          string(JSON elem GET "${intro_json}" ${i} "value" ${j})
          list(APPEND elems "${elem}")
        endforeach()
      endif()
      list(JOIN elems "," actual)
    else()
      string(JSON actual GET "${intro_json}" ${i} "value")
      if (opt_type STREQUAL "bool")
        if (actual)
          set(actual "true")
        else()
          set(actual "false")
        endif()
      endif()
    endif()
    break()
  endforeach()

  string(APPEND evidence
    "${opt_name}: declared=${opt_value} actual=${actual} found=${found}\n")

  if (NOT found)
    list(APPEND mismatches
      "${opt_name}: not present in intro-buildoptions.json -- Meson did not recognise this option")
  elseif (NOT actual STREQUAL opt_value)
    list(APPEND mismatches
      "${opt_name}: ADR 0002 §A2.2 declares '${opt_value}', resolved to '${actual}'")
  endif()
endforeach()

string(APPEND evidence "\nArtifact: ${GRAPHSCORE_THORVG_ARTIFACT}\n")
if (EXISTS "${GRAPHSCORE_THORVG_ARTIFACT}")
  string(APPEND evidence "Artifact exists: YES\n")
else()
  string(APPEND evidence "Artifact exists: NO\n")
  list(APPEND mismatches
    "artifact: expected static archive at '${GRAPHSCORE_THORVG_ARTIFACT}' does not exist after install")
endif()

string(APPEND evidence "\nHeader: ${GRAPHSCORE_THORVG_HEADER}\n")
if (EXISTS "${GRAPHSCORE_THORVG_HEADER}")
  string(APPEND evidence "Header exists: YES\n")
else()
  string(APPEND evidence "Header exists: NO\n")
  list(APPEND mismatches
    "header: expected installed public header at '${GRAPHSCORE_THORVG_HEADER}' does not exist after install -- 'includedir' is pinned by -Dincludedir, but the thorvg-1/ subdirectory beneath it comes from ThorVG's own install_headers(subdir: 'thorvg-' + vmaj), which has no Meson option to pin (ADR 0002 §A7.3)")
endif()

file(WRITE "${GRAPHSCORE_THORVG_EVIDENCE_FILE}" "${evidence}")

if (mismatches)
  list(JOIN mismatches "\n  - " mismatch_text)
  message(FATAL_ERROR
    "ThorVG build verification failed (ADR 0002 §A2.3):\n\n"
    "  - ${mismatch_text}\n\n"
    "Full evidence: ${GRAPHSCORE_THORVG_EVIDENCE_FILE}")
endif()

message(STATUS
  "ThorVG: all reviewed options verified, artifact present at "
  "${GRAPHSCORE_THORVG_ARTIFACT}, header present at "
  "${GRAPHSCORE_THORVG_HEADER} "
  "(evidence: ${GRAPHSCORE_THORVG_EVIDENCE_FILE})")
