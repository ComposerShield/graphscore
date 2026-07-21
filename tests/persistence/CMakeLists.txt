# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_persistence_test
  persistence_test.cpp
)

target_compile_features(graphscore_persistence_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_persistence_test)
graphscore_apply_const_correctness(graphscore_persistence_test)

target_include_directories(graphscore_persistence_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_persistence_test
  PRIVATE
    GTest::gtest_main
    graphscore_persistence
)

gtest_discover_tests(graphscore_persistence_test)
