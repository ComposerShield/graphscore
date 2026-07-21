# SPDX-License-Identifier: Apache-2.0
#
# The `lint` target: cpplint plus a clang-format verification pass over every
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
find_program(GRAPHSCORE_CLANG_FORMAT_EXECUTABLE
  NAMES clang-format
  # Apple's Command Line Tools ship clang-format but do not put it on PATH.
  HINTS /Library/Developer/CommandLineTools/usr/bin
)

set(GRAPHSCORE_LINT_DIRECTORIES src include apps tools tests)

add_custom_target(lint
  COMMAND ${CMAKE_COMMAND}
    -D GRAPHSCORE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -D GRAPHSCORE_CPPLINT=${GRAPHSCORE_CPPLINT_EXECUTABLE}
    -D GRAPHSCORE_CLANG_FORMAT=${GRAPHSCORE_CLANG_FORMAT_EXECUTABLE}
    -D "GRAPHSCORE_LINT_DIRECTORIES=${GRAPHSCORE_LINT_DIRECTORIES}"
    -P ${CMAKE_SOURCE_DIR}/cmake/run_lint.cmake
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running cpplint and clang-format verification"
  VERBATIM
  USES_TERMINAL
)
