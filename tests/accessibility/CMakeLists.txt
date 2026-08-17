# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_accessibility_test
  accessibility_test.cpp
)

target_compile_features(graphscore_accessibility_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_accessibility_test)

target_include_directories(graphscore_accessibility_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_accessibility_test
  PRIVATE
    GTest::gtest_main
    graphscore_accessibility
)

gtest_discover_tests(graphscore_accessibility_test)
