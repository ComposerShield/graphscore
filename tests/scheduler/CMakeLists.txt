# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_scheduler_test
  scheduler_test.cpp
)

target_compile_features(graphscore_scheduler_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_scheduler_test)

target_include_directories(graphscore_scheduler_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_scheduler_test
  PRIVATE
    GTest::gtest_main
    graphscore_scheduler
)

gtest_discover_tests(graphscore_scheduler_test)
