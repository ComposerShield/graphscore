# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_notation_test
  headers/notation_editing_smoke_test.cpp
  headers/notation_layout_smoke_test.cpp
  headers/notation_palette_smoke_test.cpp
  headers/notation_selection_smoke_test.cpp
  headers/notation_types_smoke_test.cpp
  notation_test.cpp
  note_audition_test.cpp
  note_entry_test.cpp
  note_palette_test.cpp
  note_preview_test.cpp
  selection_resolver_test.cpp
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
