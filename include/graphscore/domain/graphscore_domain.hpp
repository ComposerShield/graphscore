// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/archive_track_command.hpp>
#include <graphscore/domain/articulation.hpp>
#include <graphscore/domain/audition_mix.hpp>
#include <graphscore/domain/clef_lane.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/command_history.hpp>
#include <graphscore/domain/command_transaction.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/event_occurrence.hpp>
#include <graphscore/domain/event_queue.hpp>
#include <graphscore/domain/event_registry.hpp>
#include <graphscore/domain/event_state_machine.hpp>
#include <graphscore/domain/graph.hpp>
#include <graphscore/domain/graph_position.hpp>
#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/midi_ownership.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/notation_playback.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/pickdown_bound_oracle.hpp>
#include <graphscore/domain/pickdown_coordinates.hpp>
#include <graphscore/domain/pickdown_ownership.hpp>
#include <graphscore/domain/plugin_chain.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/restore_track_command.hpp>
#include <graphscore/domain/route_geometry.hpp>
#include <graphscore/domain/set_input_connector_name_command.hpp>
#include <graphscore/domain/set_listener_policy_command.hpp>
#include <graphscore/domain/set_node_color_command.hpp>
#include <graphscore/domain/set_node_name_command.hpp>
#include <graphscore/domain/set_node_notes_command.hpp>
#include <graphscore/domain/set_node_position_command.hpp>
#include <graphscore/domain/set_output_connector_name_command.hpp>
#include <graphscore/domain/set_output_export_enabled_command.hpp>
#include <graphscore/domain/set_output_priority_command.hpp>
#include <graphscore/domain/set_output_type_command.hpp>
#include <graphscore/domain/set_output_weight_command.hpp>
#include <graphscore/domain/set_project_dynamic_command.hpp>
#include <graphscore/domain/set_project_name_command.hpp>
#include <graphscore/domain/set_project_tempo_command.hpp>
#include <graphscore/domain/set_start_node_command.hpp>
#include <graphscore/domain/set_track_gain_command.hpp>
#include <graphscore/domain/set_track_mute_command.hpp>
#include <graphscore/domain/set_track_name_command.hpp>
#include <graphscore/domain/set_track_pan_command.hpp>
#include <graphscore/domain/set_track_solo_command.hpp>
#include <graphscore/domain/staff_layout.hpp>
#include <graphscore/domain/tempo_lane.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/vertical_transition.hpp>
#include <graphscore/domain/voice_content.hpp>
#include <graphscore/domain/weighted_selection.hpp>

namespace graphscore {

class Selection;
class ValidationService;

}  // namespace graphscore
