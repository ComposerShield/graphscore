# SPDX-License-Identifier: Apache-2.0
#
# Script-mode driver behind the `lint` target (see cmake/Lint.cmake). Run
# with `cmake -P`; it never sees the configured project, only the variables
# cmake/Lint.cmake passes on the command line.

cmake_minimum_required(VERSION 3.25)

if (NOT GRAPHSCORE_SOURCE_DIR)
  message(FATAL_ERROR "GRAPHSCORE_SOURCE_DIR must be set")
endif()

set(missing_tools "")

if (NOT GRAPHSCORE_CPPLINT OR GRAPHSCORE_CPPLINT MATCHES "NOTFOUND")
  list(APPEND missing_tools "cpplint (pip install cpplint)")
endif()

if (NOT GRAPHSCORE_CLANG_FORMAT OR GRAPHSCORE_CLANG_FORMAT MATCHES "NOTFOUND")
  list(APPEND missing_tools "clang-format (LLVM, or Xcode Command Line Tools)")
endif()

if (missing_tools)
  list(JOIN missing_tools "\n  - " missing_list)
  message(FATAL_ERROR
    "The `lint` target requires tools that were not found when this build "
    "tree was configured:\n  - ${missing_list}\n"
    "Install them, then re-run `cmake --preset <preset>` so the lint target "
    "picks them up.")
endif()

# Collect GraphScore-owned sources. Only the directories listed by
# cmake/Lint.cmake are walked, so FetchContent dependency trees under build/
# are never linted (ADR 0001, "Warning isolation").
set(lint_files "")
foreach (dir IN LISTS GRAPHSCORE_LINT_DIRECTORIES)
  if (NOT IS_DIRECTORY "${GRAPHSCORE_SOURCE_DIR}/${dir}")
    continue()
  endif()
  file(GLOB_RECURSE dir_files
    LIST_DIRECTORIES false
    RELATIVE "${GRAPHSCORE_SOURCE_DIR}"
    "${GRAPHSCORE_SOURCE_DIR}/${dir}/*.cpp"
    "${GRAPHSCORE_SOURCE_DIR}/${dir}/*.hpp"
    "${GRAPHSCORE_SOURCE_DIR}/${dir}/*.c"
    "${GRAPHSCORE_SOURCE_DIR}/${dir}/*.h"
  )
  list(APPEND lint_files ${dir_files})
endforeach()

list(FILTER lint_files EXCLUDE REGEX "(^|/)build/")
list(SORT lint_files)

if (NOT lint_files)
  message(FATAL_ERROR
    "No GraphScore sources found to lint under: "
    "${GRAPHSCORE_LINT_DIRECTORIES}. This is always a bug in the lint "
    "wiring, never a clean tree.")
endif()

list(LENGTH lint_files lint_file_count)
message(STATUS "Linting ${lint_file_count} GraphScore-owned source files")

set(failed FALSE)

# --- cpplint --------------------------------------------------------------
#
# Invoked from the repository root so CPPLINT.cfg is the config that applies.

execute_process(
  COMMAND "${GRAPHSCORE_CPPLINT}" --quiet ${lint_files}
  WORKING_DIRECTORY "${GRAPHSCORE_SOURCE_DIR}"
  RESULT_VARIABLE cpplint_result
)

if (NOT cpplint_result EQUAL 0)
  message(SEND_ERROR "cpplint reported violations (exit ${cpplint_result}).")
  set(failed TRUE)
else()
  message(STATUS "cpplint: clean")
endif()

# --- clang-format verification -------------------------------------------
#
# --dry-run -Werror reports, but does not apply, formatting differences.
# `clang-format -i <file>` is the fix.

execute_process(
  COMMAND "${GRAPHSCORE_CLANG_FORMAT}" --dry-run -Werror ${lint_files}
  WORKING_DIRECTORY "${GRAPHSCORE_SOURCE_DIR}"
  RESULT_VARIABLE format_result
)

if (NOT format_result EQUAL 0)
  message(SEND_ERROR
    "clang-format reported unformatted files (exit ${format_result}). Fix "
    "with: ${GRAPHSCORE_CLANG_FORMAT} -i <file>")
  set(failed TRUE)
else()
  message(STATUS "clang-format: clean")
endif()

if (failed)
  message(FATAL_ERROR "lint failed")
endif()
