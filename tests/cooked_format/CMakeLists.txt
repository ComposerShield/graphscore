# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_cooked_format_test
  cooked_format_test.cpp
)

target_compile_features(graphscore_cooked_format_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_cooked_format_test)
graphscore_apply_const_correctness(graphscore_cooked_format_test)

target_include_directories(graphscore_cooked_format_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_cooked_format_test
  PRIVATE
    GTest::gtest_main
    graphscore_cooked_format
)

gtest_discover_tests(graphscore_cooked_format_test)
