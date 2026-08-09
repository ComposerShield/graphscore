# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_notation_test
  notation_test.cpp
  note_palette_test.cpp
)

target_compile_features(graphscore_notation_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_notation_test)
graphscore_apply_const_correctness(graphscore_notation_test)

target_include_directories(graphscore_notation_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_notation_test
  PRIVATE
    GTest::gtest_main
    graphscore_notation
)

gtest_discover_tests(graphscore_notation_test)
