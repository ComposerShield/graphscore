# SPDX-License-Identifier: Apache-2.0
#
# Repository-level tests: properties of the checkout and the build system
# itself rather than of any compiled target.

add_test(NAME graphscore_claude_md_symlink
  COMMAND ${CMAKE_COMMAND}
    -D "GRAPHSCORE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_symlink.cmake"
)

add_test(NAME graphscore_offline_dependencies
  COMMAND ${CMAKE_COMMAND}
    -D "GRAPHSCORE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -D "GRAPHSCORE_BINARY_DIR=${CMAKE_BINARY_DIR}"
    -D "GRAPHSCORE_SCRATCH_DIR=${CMAKE_BINARY_DIR}/offline-test"
    -D "GRAPHSCORE_GENERATOR=${CMAKE_GENERATOR}"
    -D "GRAPHSCORE_C_COMPILER=${CMAKE_C_COMPILER}"
    -D "GRAPHSCORE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    # The scratch tree must be configured the same way as this one, or it
    # would need dependencies this build never fetched.
    -D "GRAPHSCORE_BUILD_WRITER=${GRAPHSCORE_BUILD_WRITER}"
    # Bravura is a file(DOWNLOAD) asset, not a FetchContent dependency, so it
    # is not discovered by the _deps/*-src glob below. GRAPHSCORE_BRAVURA_FONT_PATH
    # is only set when GRAPHSCORE_BUILD_WRITER=ON pulled in cmake/Bravura.cmake
    # (ADR 0002 §A4); empty otherwise, which the script below treats as
    # "nothing to override".
    -D "GRAPHSCORE_BRAVURA_FONT_SRC=${GRAPHSCORE_BRAVURA_FONT_PATH}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_offline_dependencies.cmake"
)

set_tests_properties(graphscore_offline_dependencies PROPERTIES
  TIMEOUT 300
  LABELS "integration"
)
