# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_core_test
  core_test.cpp
)

target_compile_features(graphscore_core_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_core_test)
graphscore_apply_const_correctness(graphscore_core_test)

target_include_directories(graphscore_core_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_core_test
  PRIVATE
    GTest::gtest_main
    graphscore_core
)

gtest_discover_tests(graphscore_core_test)
