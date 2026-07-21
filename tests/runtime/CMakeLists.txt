# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_runtime_test
  runtime_test.cpp
)

target_compile_features(graphscore_runtime_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_runtime_test)
graphscore_apply_const_correctness(graphscore_runtime_test)

target_include_directories(graphscore_runtime_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_runtime_test
  PRIVATE
    GTest::gtest_main
    graphscore_runtime
)

# gtest_discover_tests runs the test binary at build time to enumerate
# tests. On Windows, the binary depends on graphscore_runtime.dll whose
# directory is not on PATH during the build, so the enumeration fails.
# Pre-registering the six test cases with add_test avoids that build-time
# execution, and the ENVIRONMENT_MODIFICATION below ensures the DLL is
# found at test time on every platform.
add_test(NAME RuntimeTest.VersionQueryReturnsExpected
  COMMAND graphscore_runtime_test
          --gtest_filter=RuntimeTest.VersionQueryReturnsExpected)
add_test(NAME RuntimeTest.CreateDestroySucceeds
  COMMAND graphscore_runtime_test
          --gtest_filter=RuntimeTest.CreateDestroySucceeds)
add_test(NAME RuntimeTest.CreateNullOutPlayerFails
  COMMAND graphscore_runtime_test
          --gtest_filter=RuntimeTest.CreateNullOutPlayerFails)
add_test(NAME RuntimeTest.PollDiagnosticsReturnsZero
  COMMAND graphscore_runtime_test
          --gtest_filter=RuntimeTest.PollDiagnosticsReturnsZero)
add_test(NAME RuntimeTest.QueryCapacityReturnsSuccess
  COMMAND graphscore_runtime_test
          --gtest_filter=RuntimeTest.QueryCapacityReturnsSuccess)
add_test(NAME RuntimeTest.ProcessReturnsSuccess
  COMMAND graphscore_runtime_test
          --gtest_filter=RuntimeTest.ProcessReturnsSuccess)

set_tests_properties(
  RuntimeTest.VersionQueryReturnsExpected
  RuntimeTest.CreateDestroySucceeds
  RuntimeTest.CreateNullOutPlayerFails
  RuntimeTest.PollDiagnosticsReturnsZero
  RuntimeTest.QueryCapacityReturnsSuccess
  RuntimeTest.ProcessReturnsSuccess
  PROPERTIES
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:graphscore_runtime>"
)
