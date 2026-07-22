# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_domain STATIC
  clef_lane.cpp
  event_registry.cpp
  measure_map.cpp
  node.cpp
  node_timeline.cpp
  notation_event.cpp
  notation_markings.cpp
  notation_validation.cpp
  project.cpp
  staff_layout.cpp
  tempo_lane.cpp
  track.cpp
  voice_content.cpp
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
