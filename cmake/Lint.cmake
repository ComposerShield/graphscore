# SPDX-License-Identifier: Apache-2.0
#
# The `lint` target: cpplint plus a clang-format 18 verification pass over every
# GraphScore-owned source file.
#
#   cmake --build --preset debug --target lint
#
# The tracked .githooks/pre-commit hook runs the same two tools over the
# staged files only, using the same CPPLINT.cfg and .clang-format. CI runs
# them over the whole tree independently of any local hook, so CI stays
# authoritative even when a contributor has not installed the hook.
#
# Both tools are optional at configure time: a developer without them still
# gets a working build, and `lint` fails with an actionable install message
# rather than silently passing.

find_program(GRAPHSCORE_CPPLINT_EXECUTABLE NAMES cpplint)
# Preserve an explicitly supplied or previously cached executable. The lint
# driver validates its reported major, so a stale cache (including Apple
# clang-format 17) fails with reconfigure guidance rather than being used.
if (NOT DEFINED GRAPHSCORE_CLANG_FORMAT_EXECUTABLE OR
    GRAPHSCORE_CLANG_FORMAT_EXECUTABLE MATCHES "NOTFOUND")
  unset(_graphscore_clang_format CACHE)
  find_program(_graphscore_clang_format NAMES clang-format-18)

  if (NOT _graphscore_clang_format)
    find_program(_graphscore_clang_format NAMES clang-format
      HINTS /opt/homebrew/opt/llvm@18/bin
      NO_DEFAULT_PATH
    )
  endif()
  if (NOT _graphscore_clang_format)
    find_program(_graphscore_clang_format NAMES clang-format
      HINTS /usr/local/opt/llvm@18/bin
      NO_DEFAULT_PATH
    )
  endif()
  if (NOT _graphscore_clang_format)
    unset(_graphscore_unversioned_clang_format CACHE)
    find_program(_graphscore_unversioned_clang_format NAMES clang-format)
    if (_graphscore_unversioned_clang_format)
      execute_process(
        COMMAND "${_graphscore_unversioned_clang_format}" --version
        RESULT_VARIABLE _graphscore_unversioned_result
        OUTPUT_VARIABLE _graphscore_unversioned_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if (_graphscore_unversioned_result EQUAL 0 AND
          _graphscore_unversioned_version MATCHES
            "clang-format version 18(\\.|[ \r\n]|$)")
        set(_graphscore_clang_format
          "${_graphscore_unversioned_clang_format}")
      endif()
    endif()
  endif()

  set(GRAPHSCORE_CLANG_FORMAT_EXECUTABLE "${_graphscore_clang_format}"
    CACHE FILEPATH "clang-format 18 executable")
endif()

set(GRAPHSCORE_LINT_DIRECTORIES src include apps tools tests)

add_custom_target(lint
  COMMAND ${CMAKE_COMMAND}
    -D GRAPHSCORE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -D GRAPHSCORE_CPPLINT=${GRAPHSCORE_CPPLINT_EXECUTABLE}
    -D GRAPHSCORE_CLANG_FORMAT=${GRAPHSCORE_CLANG_FORMAT_EXECUTABLE}
    -D "GRAPHSCORE_LINT_DIRECTORIES=${GRAPHSCORE_LINT_DIRECTORIES}"
    -P ${CMAKE_SOURCE_DIR}/cmake/run_lint.cmake
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running cpplint and clang-format 18 verification"
  VERBATIM
  USES_TERMINAL
)
