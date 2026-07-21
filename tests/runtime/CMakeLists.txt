# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_runtime_test
  runtime_test.cpp
)

target_compile_features(graphscore_runtime_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_runtime_test)

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

gtest_discover_tests(graphscore_runtime_test)
