# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_domain STATIC
  archive_track_command.cpp
  bind_output_event_command.cpp
  clef_lane.cpp
  command_history.cpp
  command_transaction.cpp
  connect_command.cpp
  connector.cpp
  disconnect_command.cpp
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
  reset_route_command.cpp
  restore_track_command.cpp
  route_geometry.cpp
  set_custom_route_command.cpp
  set_input_connector_name_command.cpp
  set_listener_policy_command.cpp
  set_node_color_command.cpp
  set_node_name_command.cpp
  set_node_notes_command.cpp
  set_node_position_command.cpp
  set_output_connector_name_command.cpp
  set_output_export_enabled_command.cpp
  set_output_priority_command.cpp
  set_output_type_command.cpp
  set_output_weight_command.cpp
  set_project_dynamic_command.cpp
  set_project_name_command.cpp
  set_project_tempo_command.cpp
  set_start_node_command.cpp
  set_track_gain_command.cpp
  set_track_mute_command.cpp
  set_track_name_command.cpp
  set_track_pan_command.cpp
  set_track_solo_command.cpp
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
