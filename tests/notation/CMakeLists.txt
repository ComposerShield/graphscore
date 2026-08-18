# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_notation_test
  headers/notation_editing_smoke_test.cpp
  headers/notation_layout_smoke_test.cpp
  headers/notation_palette_smoke_test.cpp
  headers/notation_selection_smoke_test.cpp
  headers/notation_types_smoke_test.cpp
  beam_override_edit_test.cpp
  convert_event_to_rest_test.cpp
  event_style_edit_test.cpp
  marking_style_edit_test.cpp
  measure_edit_test.cpp
  notation_golden_test.cpp
  notation_test.cpp
  note_audition_test.cpp
  note_entry_invariants_test.cpp
  note_entry_replacement_test.cpp
  note_entry_test_support.cpp
  note_entry_voice_creation_test.cpp
  note_interval_test.cpp
  note_palette_test.cpp
  note_preview_test.cpp
  notehead_edit_test.cpp
  pickdown_layout_test.cpp
  tuplet_edit_test.cpp
  selection/selection_drag_lifecycle_test.cpp
  selection/selection_highlight_rects_test.cpp
  selection/selection_hit_dense_notation_test.cpp
  selection/selection_hit_isolation_test.cpp
  selection/selection_hit_notehead_column_test.cpp
  selection/selection_hit_object_kind_test.cpp
  selection/selection_hit_regression_test.cpp
  selection/selection_hit_stemless_defect_test.cpp
  selection/selection_hit_tie_geometry_test.cpp
  selection/selection_measure_extension_test.cpp
  selection/selection_measure_test.cpp
  selection/selection_range_extension_test.cpp
  selection/selection_range_spec_test.cpp
  selection/selection_range_test.cpp
  selection/selection_staff_step_test.cpp
  selection/selection_test_support.cpp
)

target_compile_features(graphscore_notation_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_notation_test)

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
