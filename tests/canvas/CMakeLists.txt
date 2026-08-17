# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_canvas_test
  viewport_transform_test.cpp
  trackpad_gesture_test.cpp
  canvas_navigation_test.cpp
  spatial_index_test.cpp
  node_notation_test.cpp
  node_header_test.cpp
  node_geometry_test.cpp
)

target_compile_features(graphscore_canvas_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_canvas_test)

target_link_libraries(graphscore_canvas_test
  PRIVATE
    GTest::gtest_main
    graphscore_canvas
)

gtest_discover_tests(graphscore_canvas_test
  TEST_PREFIX "graphscore_canvas_test."
)
