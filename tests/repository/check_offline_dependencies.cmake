# SPDX-License-Identifier: Apache-2.0
#
# Dependency override / offline-cache configure test.
#
# Air-gapped and reproducible builds depend on two FetchContent facilities
# working together:
#
#   FETCHCONTENT_SOURCE_DIR_<NAME>   point one dependency at a local checkout
#   FETCHCONTENT_FULLY_DISCONNECTED  perform no downloads or updates at all
#
# This test configures a scratch build tree with both set, using the
# dependency sources the main build tree already downloaded as the local
# checkout. If FetchContent were still reaching the network — or if a
# dependency were declared in a way that ignores the override — configuring
# with FETCHCONTENT_FULLY_DISCONNECTED=ON would fail.
#
# It is a configure-only test: building the dependencies again would add
# minutes without testing anything the main build does not already cover.

cmake_minimum_required(VERSION 3.25)

foreach (required
    GRAPHSCORE_SOURCE_DIR GRAPHSCORE_BINARY_DIR GRAPHSCORE_SCRATCH_DIR
    GRAPHSCORE_GENERATOR)
  if (NOT ${required})
    message(FATAL_ERROR "${required} must be set")
  endif()
endforeach()

# Discover every dependency the main build tree has already fetched, rather
# than naming them here. A milestone that adds a dependency then gets offline
# coverage for it automatically; one that names dependencies by hand would
# quietly stop covering the new one.
file(GLOB dependency_source_dirs
  LIST_DIRECTORIES true
  "${GRAPHSCORE_BINARY_DIR}/_deps/*-src")

set(overrides "")
set(dependency_names "")

foreach (source_dir IN LISTS dependency_source_dirs)
  if (NOT IS_DIRECTORY "${source_dir}")
    continue()
  endif()

  get_filename_component(dir_name "${source_dir}" NAME)
  string(REGEX REPLACE "-src$" "" dependency "${dir_name}")

  # FetchContent uppercases the dependency name for the override variable.
  string(TOUPPER "${dependency}" dependency_upper)

  list(APPEND dependency_names "${dependency}")
  list(APPEND overrides
    "-DFETCHCONTENT_SOURCE_DIR_${dependency_upper}=${source_dir}")
endforeach()

if (NOT dependency_names)
  message(FATAL_ERROR
    "The main build tree at '${GRAPHSCORE_BINARY_DIR}' has fetched no "
    "dependencies, so this test would prove nothing. Configure and build "
    "the main tree first.")
endif()

list(JOIN dependency_names ", " dependency_list)
message(STATUS "Offline configure: overriding ${dependency_list}")

file(REMOVE_RECURSE "${GRAPHSCORE_SCRATCH_DIR}")
file(MAKE_DIRECTORY "${GRAPHSCORE_SCRATCH_DIR}")

execute_process(
  COMMAND ${CMAKE_COMMAND}
    -S "${GRAPHSCORE_SOURCE_DIR}"
    -B "${GRAPHSCORE_SCRATCH_DIR}"
    -G "${GRAPHSCORE_GENERATOR}"
    "-DCMAKE_BUILD_TYPE=Debug"
    "-DCMAKE_C_COMPILER=${GRAPHSCORE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${GRAPHSCORE_CXX_COMPILER}"
    # The overrides under test, one per fetched dependency.
    ${overrides}
    # Any network access at all is a failure, not a slow success.
    "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_output
)

if (NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "Offline configure failed (exit ${configure_result}). Either a "
    "dependency ignores its FETCHCONTENT_SOURCE_DIR_<NAME> override, or the "
    "build reaches the network during configure. Both break air-gapped "
    "builds.\n\n${configure_output}")
endif()

# Each override must have been honoured, not merely tolerated: FetchContent
# records the resolved source directory in the scratch tree's cache, and a
# bypassed override would leave a freshly downloaded copy under _deps.
file(READ "${GRAPHSCORE_SCRATCH_DIR}/CMakeCache.txt" cache_contents)

foreach (dependency IN LISTS dependency_names)
  string(TOUPPER "${dependency}" dependency_upper)
  set(expected "${GRAPHSCORE_BINARY_DIR}/_deps/${dependency}-src")

  if (NOT cache_contents MATCHES
      "FETCHCONTENT_SOURCE_DIR_${dependency_upper}:PATH=([^\n]*)")
    message(FATAL_ERROR
      "FETCHCONTENT_SOURCE_DIR_${dependency_upper} is absent from the "
      "scratch build tree's cache. The override was not applied.")
  endif()

  if (NOT CMAKE_MATCH_1 STREQUAL expected)
    message(FATAL_ERROR
      "FETCHCONTENT_SOURCE_DIR_${dependency_upper} resolved to "
      "'${CMAKE_MATCH_1}', not the requested '${expected}'.")
  endif()

  if (IS_DIRECTORY "${GRAPHSCORE_SCRATCH_DIR}/_deps/${dependency}-src")
    message(FATAL_ERROR
      "The offline build tree downloaded its own ${dependency} copy at "
      "_deps/${dependency}-src despite "
      "FETCHCONTENT_SOURCE_DIR_${dependency_upper} pointing elsewhere.")
  endif()
endforeach()

message(STATUS
  "Offline configure: succeeded with FETCHCONTENT_FULLY_DISCONNECTED=ON and "
  "local checkouts of ${dependency_list}")
