# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_domain_test
  articulation_test.cpp
  clef_lane_test.cpp
  connector_test.cpp
  event_queue_test.cpp
  event_registry_test.cpp
  event_state_machine_test.cpp
  graph_test.cpp
  lane_alignment_test.cpp
  measure_map_test.cpp
  node_timeline_test.cpp
  notation_event_test.cpp
  notation_markings_test.cpp
  notation_validation_test.cpp
  project_track_test.cpp
  route_geometry_test.cpp
  tempo_lane_test.cpp
  track_test.cpp
  voice_content_test.cpp
  voice_normalization_test.cpp
)

target_compile_features(graphscore_domain_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_domain_test)
graphscore_apply_const_correctness(graphscore_domain_test)

target_include_directories(graphscore_domain_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_domain_test
  PRIVATE
    GTest::gtest_main
    graphscore_domain
)

gtest_discover_tests(graphscore_domain_test)
