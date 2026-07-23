// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/articulation.hpp>
#include <graphscore/domain/clef_lane.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/dynamic.hpp>
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
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/pickdown_bound_oracle.hpp>
#include <graphscore/domain/pickdown_coordinates.hpp>
#include <graphscore/domain/pickdown_ownership.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/route_geometry.hpp>
#include <graphscore/domain/staff_layout.hpp>
#include <graphscore/domain/tempo_lane.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/vertical_transition.hpp>
#include <graphscore/domain/voice_content.hpp>
#include <graphscore/domain/weighted_selection.hpp>

namespace graphscore {

class Command;
class Selection;
class ValidationService;

}  // namespace graphscore
