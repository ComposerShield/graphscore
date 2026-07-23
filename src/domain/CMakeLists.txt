# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_domain STATIC
  clef_lane.cpp
  connector.cpp
  event_queue.cpp
  event_registry.cpp
  event_state_machine.cpp
  graph.cpp
  measure_map.cpp
  midi_ownership.cpp
  node.cpp
  node_timeline.cpp
  notation_event.cpp
  notation_markings.cpp
  notation_playback.cpp
  notation_validation.cpp
  pickdown_bound_oracle.cpp
  pickdown_coordinates.cpp
  pickdown_ownership.cpp
  project.cpp
  route_geometry.cpp
  staff_layout.cpp
  tempo_lane.cpp
  track.cpp
  vertical_transition.cpp
  voice_content.cpp
  weighted_selection.cpp
)

target_compile_features(graphscore_domain PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_domain)
graphscore_apply_const_correctness(graphscore_domain)

target_include_directories(graphscore_domain
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_domain PUBLIC graphscore_core)
