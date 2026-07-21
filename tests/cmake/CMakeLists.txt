# SPDX-License-Identifier: Apache-2.0
#
# Registers the CMake consumer test (ADR 0003 §7.8). The test itself is a
# CMake script that installs the runtime and drives two out-of-tree projects
# against the install tree; see run_consumer_test.cmake.
#
# Nothing is added to the main build here: the point of the test is that the
# consumer projects are configured from scratch, exactly as an external
# integrator would.

add_test(NAME graphscore_cmake_consumer
  COMMAND ${CMAKE_COMMAND}
    -D "GRAPHSCORE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -D "GRAPHSCORE_BINARY_DIR=${CMAKE_BINARY_DIR}"
    -D "GRAPHSCORE_SCRATCH_DIR=${CMAKE_BINARY_DIR}/consumer-test"
    -D "GRAPHSCORE_GENERATOR=${CMAKE_GENERATOR}"
    -D "GRAPHSCORE_CONFIG=$<IF:$<CONFIG:>,Debug,$<CONFIG>>"
    -D "GRAPHSCORE_C_COMPILER=${CMAKE_C_COMPILER}"
    -D "GRAPHSCORE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    # A sanitizer-instrumented runtime leaves its sanitizer-runtime symbols
    # for the consuming link to resolve, so the out-of-tree consumers must be
    # built with the same flags. See cmake/Sanitizers.cmake.
    -D "GRAPHSCORE_SANITIZER_COMPILE_FLAGS=${GRAPHSCORE_SANITIZER_COMPILE_FLAGS}"
    -D "GRAPHSCORE_SANITIZER_LINK_FLAGS=${GRAPHSCORE_SANITIZER_LINK_FLAGS}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_consumer_test.cmake"
)

# The test installs from this build tree, so everything the runtime package
# exports has to be built first. ctest has no dependency on the build, so
# this is expressed as a fixture-free requirement on the caller having run
# the build — which `ctest --preset <p>` after `cmake --build --preset <p>`
# always satisfies.
set_tests_properties(graphscore_cmake_consumer PROPERTIES
  # Configuring and building two projects from scratch is slower than a unit
  # test; the default 1500s ctest timeout is generous but this makes the
  # expectation explicit.
  TIMEOUT 600
  LABELS "integration"
)
