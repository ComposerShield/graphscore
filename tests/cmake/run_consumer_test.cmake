# SPDX-License-Identifier: Apache-2.0
#
# Driver for the CMake consumer test (ADR 0003 §7.8). Run as a ctest test in
# script mode; see tests/cmake/CMakeLists.txt.
#
# Four stages:
#
#   1. Install GraphScore Runtime into a scratch prefix.
#   2. Assert the install tree contains only the runtime closure and the
#      public C header — no writer library, no writer header.
#   3. Configure, build, and run the out-of-tree consumer project against
#      that prefix, from both C and C++.
#   4. Assert the writer-leak probe fails to configure.
#
# Nothing here reads the GraphScore source tree except the two consumer
# project directories, so the test cannot accidentally succeed via a path
# that a real consumer would not have.

cmake_minimum_required(VERSION 3.25)

foreach (required
    GRAPHSCORE_SOURCE_DIR GRAPHSCORE_BINARY_DIR GRAPHSCORE_SCRATCH_DIR
    GRAPHSCORE_CONFIG)
  if (NOT ${required})
    message(FATAL_ERROR "${required} must be set")
  endif()
endforeach()

# Under a sanitizer preset, the installed runtime is instrumented and leaves
# its sanitizer-runtime references for the consuming link to resolve. A
# consumer built without matching flags fails to link on `__asan_*` /
# `__tsan_*`. This is not a weakening of the test: it is what a real
# integrator building against a sanitizer-instrumented GraphScore would also
# have to do. On a normal preset both variables are empty and nothing is
# added.
set(sanitizer_arguments "")

if (GRAPHSCORE_SANITIZER_COMPILE_FLAGS OR GRAPHSCORE_SANITIZER_LINK_FLAGS)
  list(APPEND sanitizer_arguments
    "-DCMAKE_C_FLAGS=${GRAPHSCORE_SANITIZER_COMPILE_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${GRAPHSCORE_SANITIZER_COMPILE_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${GRAPHSCORE_SANITIZER_LINK_FLAGS}"
  )
  message(STATUS
    "CMake consumer test: forwarding sanitizer flags to the out-of-tree "
    "consumer (${GRAPHSCORE_SANITIZER_COMPILE_FLAGS})")
endif()

set(prefix "${GRAPHSCORE_SCRATCH_DIR}/install")
set(consumer_build "${GRAPHSCORE_SCRATCH_DIR}/consumer-build")
set(leak_build "${GRAPHSCORE_SCRATCH_DIR}/writer-leak-build")

# Start from a clean scratch directory so a stale install tree from an
# earlier run can never make this test pass.
file(REMOVE_RECURSE "${GRAPHSCORE_SCRATCH_DIR}")
file(MAKE_DIRECTORY "${GRAPHSCORE_SCRATCH_DIR}")

function(run_step description)
  cmake_parse_arguments(step "" "EXPECT_FAILURE" "COMMAND" ${ARGN})

  execute_process(
    COMMAND ${step_COMMAND}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
  )

  if (step_EXPECT_FAILURE)
    if (result EQUAL 0)
      message(FATAL_ERROR
        "${description}: expected failure, but it succeeded.\n\n${output}")
    endif()
    message(STATUS "${description}: failed as required")
  else()
    if (NOT result EQUAL 0)
      message(FATAL_ERROR
        "${description}: failed (exit ${result}).\n\n${output}")
    endif()
    message(STATUS "${description}: ok")
  endif()

  set(step_output "${output}" PARENT_SCOPE)
endfunction()

# --- 1. install -----------------------------------------------------------

run_step("install runtime package"
  COMMAND ${CMAKE_COMMAND} --install "${GRAPHSCORE_BINARY_DIR}"
          --prefix "${prefix}" --config "${GRAPHSCORE_CONFIG}")

# --- 2. the install tree contains only what a runtime consumer needs ------

file(GLOB_RECURSE installed_headers RELATIVE "${prefix}" "${prefix}/include/*")

foreach (header IN LISTS installed_headers)
  if (NOT header MATCHES "^include/graphscore/c_abi/")
    message(FATAL_ERROR
      "The runtime install tree contains '${header}'. Only the public C ABI "
      "header may be installed: a consumer able to include a C++ header "
      "would have a second, unstable interface to the runtime "
      "(ADR 0003 §7.8).")
  endif()
endforeach()

if (NOT EXISTS "${prefix}/include/graphscore/c_abi/graphscore_c_abi.h")
  message(FATAL_ERROR
    "The runtime install tree has no graphscore_c_abi.h. A consumer cannot "
    "use the runtime without it.")
endif()

file(GLOB_RECURSE installed_libraries RELATIVE "${prefix}"
  "${prefix}/lib/*" "${prefix}/bin/*")

foreach (library IN LISTS installed_libraries)
  if (library MATCHES
      "(writer|canvas|rendering|notation|domain|persistence|compiler|vst3|audio_device|midi_io|accessibility|plugin_scanner)")
    message(FATAL_ERROR
      "The runtime install tree contains the writer-side artifact "
      "'${library}'. The runtime package installs only the ADR 0003 §3.1 "
      "runtime closure.")
  endif()
endforeach()

# --- 3. the out-of-tree consumer -----------------------------------------

run_step("configure consumer project"
  COMMAND ${CMAKE_COMMAND}
    -S "${GRAPHSCORE_SOURCE_DIR}/tests/cmake/consumer"
    -B "${consumer_build}"
    -G "${GRAPHSCORE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DCMAKE_BUILD_TYPE=${GRAPHSCORE_CONFIG}"
    "-DCMAKE_C_COMPILER=${GRAPHSCORE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${GRAPHSCORE_CXX_COMPILER}"
    ${sanitizer_arguments})

run_step("build consumer project"
  COMMAND ${CMAKE_COMMAND} --build "${consumer_build}"
          --config "${GRAPHSCORE_CONFIG}")

run_step("run consumer executables"
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${consumer_build}"
          --build-config "${GRAPHSCORE_CONFIG}" --output-on-failure)

# --- 4. the writer must not be consumable --------------------------------

run_step("writer-leak probe" EXPECT_FAILURE TRUE
  COMMAND ${CMAKE_COMMAND}
    -S "${GRAPHSCORE_SOURCE_DIR}/tests/cmake/writer_leak"
    -B "${leak_build}"
    -G "${GRAPHSCORE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DCMAKE_BUILD_TYPE=${GRAPHSCORE_CONFIG}"
    "-DCMAKE_CXX_COMPILER=${GRAPHSCORE_CXX_COMPILER}")

# The probe must fail for the declared reason, not because of an unrelated
# typo in its own CMakeLists. Without this the test would keep passing after
# the guarantee it checks had been lost.
if (NOT step_output MATCHES "graphscore::writer_app")
  message(FATAL_ERROR
    "The writer-leak probe failed, but not because graphscore::writer_app "
    "was unavailable. The probe is no longer testing what it claims:\n\n"
    "${step_output}")
endif()

message(STATUS "CMake consumer test: runtime is consumable in isolation")
