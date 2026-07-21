# SPDX-License-Identifier: Apache-2.0
#
# Source-control metadata test: CLAUDE.md must be a symbolic link to
# AGENTS.md in the Git index, not a copy of it.
#
# Both entry points must always expose identical guidance. A copy would drift
# the moment either file was edited, and the drift would be invisible in
# review — the two files would simply disagree. Git stores a symlink as mode
# 120000 with the link target as its blob content, so the index itself
# records the intent and this test can check it without trusting the working
# tree.
#
# On a checkout made with core.symlinks=false (Windows without Developer
# Mode), the working-tree file is a regular file containing the target path.
# The index mode is still 120000, so the authoritative check below holds
# everywhere; only the working-tree resolution check is conditional.

cmake_minimum_required(VERSION 3.25)

if (NOT GRAPHSCORE_SOURCE_DIR)
  message(FATAL_ERROR "GRAPHSCORE_SOURCE_DIR must be set")
endif()

find_program(GIT_EXECUTABLE NAMES git)
if (NOT GIT_EXECUTABLE)
  message(FATAL_ERROR
    "git was not found. This test reads Git index metadata and cannot be "
    "satisfied any other way.")
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" ls-files --stage -- CLAUDE.md
  WORKING_DIRECTORY "${GRAPHSCORE_SOURCE_DIR}"
  OUTPUT_VARIABLE index_entry
  ERROR_VARIABLE git_error
  RESULT_VARIABLE git_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (NOT git_result EQUAL 0)
  message(FATAL_ERROR "git ls-files failed: ${git_error}")
endif()

if (index_entry STREQUAL "")
  message(FATAL_ERROR
    "CLAUDE.md is not tracked by Git. It must be tracked as a symbolic link "
    "to AGENTS.md so that both entry points expose identical guidance.")
endif()

# Format: <mode> <object> <stage>\t<path>
if (NOT index_entry MATCHES "^([0-7]+) ([0-9a-f]+) ")
  message(FATAL_ERROR "Unrecognised git ls-files output: '${index_entry}'")
endif()

set(index_mode "${CMAKE_MATCH_1}")
set(index_object "${CMAKE_MATCH_2}")

if (NOT index_mode STREQUAL "120000")
  message(FATAL_ERROR
    "CLAUDE.md is recorded in the Git index with mode ${index_mode}, not "
    "120000 (symbolic link). It has been replaced by a regular copied file. "
    "Restore it with:\n"
    "  git rm --cached CLAUDE.md && rm -f CLAUDE.md\n"
    "  ln -s AGENTS.md CLAUDE.md && git add CLAUDE.md\n"
    "See docs/plan/01-toolchain-ci.md, 'Repository structure'.")
endif()

# The blob content of a symlink entry is its target path.
execute_process(
  COMMAND "${GIT_EXECUTABLE}" cat-file blob "${index_object}"
  WORKING_DIRECTORY "${GRAPHSCORE_SOURCE_DIR}"
  OUTPUT_VARIABLE link_target
  RESULT_VARIABLE cat_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (NOT cat_result EQUAL 0)
  message(FATAL_ERROR "git cat-file failed for CLAUDE.md")
endif()

if (NOT link_target STREQUAL "AGENTS.md")
  message(FATAL_ERROR
    "CLAUDE.md is a symbolic link to '${link_target}', not to 'AGENTS.md'. "
    "The link must be relative and must target the root AGENTS.md.")
endif()

message(STATUS "CLAUDE.md: tracked as a symlink to AGENTS.md (mode 120000)")

# Working-tree resolution. Skipped where the platform or Git configuration
# cannot materialise symlinks; the index check above already holds there.
if (IS_SYMLINK "${GRAPHSCORE_SOURCE_DIR}/CLAUDE.md")
  file(READ "${GRAPHSCORE_SOURCE_DIR}/CLAUDE.md" claude_contents)
  file(READ "${GRAPHSCORE_SOURCE_DIR}/AGENTS.md" agents_contents)

  if (NOT claude_contents STREQUAL agents_contents)
    message(FATAL_ERROR
      "CLAUDE.md is a symlink but resolves to content that differs from "
      "AGENTS.md. The link target is broken.")
  endif()

  message(STATUS "CLAUDE.md: resolves to AGENTS.md in the working tree")
else()
  message(STATUS
    "CLAUDE.md: working tree has no symlink support (core.symlinks=false); "
    "index metadata checked only")
endif()
