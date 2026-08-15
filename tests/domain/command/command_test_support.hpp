// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Accidental;
using graphscore::AccidentalStepDirection;
using graphscore::AddBeamOverrideCommand;
using graphscore::AddClefChangeCommand;
using graphscore::AddDynamicCommand;
using graphscore::AddGraceGroupCommand;
using graphscore::AddHairpinCommand;
using graphscore::AddInputConnectorCommand;
using graphscore::AddNodeCommand;
using graphscore::AddOutputConnectorCommand;
using graphscore::AddPedalSpanCommand;
using graphscore::AddSlurCommand;
using graphscore::AddTempoPointCommand;
using graphscore::AddTrackCommand;
using graphscore::ArchiveTrackCommand;
using graphscore::BeamOverride;
using graphscore::BindOutputEventCommand;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::ClearPickdownCommand;
using graphscore::Clef;
using graphscore::ClefLane;
using graphscore::Command;
using graphscore::CommandHistory;
using graphscore::CommandTransaction;
using graphscore::ConnectCommand;
using graphscore::ConnectorId;
using graphscore::ConnectorType;
using graphscore::ConvertEventToRestCommand;
using graphscore::DisconnectCommand;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::event_id;
using graphscore::EventId;
using graphscore::EventListener;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Graph;
using graphscore::GraphPosition;
using graphscore::Hairpin;
using graphscore::HairpinDirection;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_beam_override;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::MoveClefChangeCommand;
using graphscore::MoveNoteheadCommand;
using graphscore::MoveTempoPointCommand;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::notehead_move_scope;
using graphscore::NoteheadItem;
using graphscore::NoteheadMoveScope;
using graphscore::NoteheadStepDirection;
using graphscore::NoteValue;
using graphscore::OutputConnector;
using graphscore::PedalSpan;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::QueuePolicy;
using graphscore::Rational;
using graphscore::RegisterEventCommand;
using graphscore::RemoveBeamOverrideCommand;
using graphscore::RemoveClefChangeCommand;
using graphscore::RemoveDynamicCommand;
using graphscore::RemoveEventCommand;
using graphscore::RemoveGraceGroupCommand;
using graphscore::RemoveHairpinCommand;
using graphscore::RemoveInputConnectorCommand;
using graphscore::RemoveNodeCommand;
using graphscore::RemoveOutputConnectorCommand;
using graphscore::RemovePedalSpanCommand;
using graphscore::RemoveSlurCommand;
using graphscore::RemoveTempoPointCommand;
using graphscore::ResetRouteCommand;
using graphscore::Rest;
using graphscore::RestoreTrackCommand;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::RouteGeometry;
using graphscore::RoutePoint;
using graphscore::SetCustomRouteCommand;
using graphscore::SetEventCommand;
using graphscore::SetInputConnectorNameCommand;
using graphscore::SetListenerPolicyCommand;
using graphscore::SetMeasureKeySignatureCommand;
using graphscore::SetNodeColorCommand;
using graphscore::SetNodeNameCommand;
using graphscore::SetNodeNotesCommand;
using graphscore::SetNodePositionCommand;
using graphscore::SetOutputConnectorNameCommand;
using graphscore::SetOutputExportEnabledCommand;
using graphscore::SetOutputPriorityCommand;
using graphscore::SetOutputTypeCommand;
using graphscore::SetOutputWeightCommand;
using graphscore::SetPickdownCommand;
using graphscore::SetProjectDynamicCommand;
using graphscore::SetProjectNameCommand;
using graphscore::SetProjectTempoCommand;
using graphscore::SetStartNodeCommand;
using graphscore::SetTempoPointCommand;
using graphscore::SetTieCommand;
using graphscore::SetTrackGainCommand;
using graphscore::SetTrackMuteCommand;
using graphscore::SetTrackNameCommand;
using graphscore::SetTrackPanCommand;
using graphscore::SetTrackSoloCommand;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::StepAccidentalCommand;
using graphscore::Tempo;
using graphscore::TempoLane;
using graphscore::TempoPoint;
using graphscore::TempoSegmentKind;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::TrackLane;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

Project make_project();

// Builds a project with one active track, one node with a single 4/4
// measure timeline, and one stave whose voices are fillable.  Returns the
// project, node end, and the stable ids needed to address specific voices.
struct NotationSetup {
  Project  project;
  NodeId   node_id;
  TrackId  track_id;
  StaveId  stave_id;
  Rational node_end;
};

NotationSetup make_notation_setup();

SpelledPitch pitch_c4();
SpelledPitch pitch_d4();
SpelledPitch pitch_e4();
SpelledPitch pitch_g4();

Duration quarter();
Duration half();
Duration whole();
Duration eighth();
Duration dotted_half();

// Fill all four voices on a stave with one quarter note each and
// normalize to `node_end`.  Required for whole-TrackLane candidate
// validation now that validate_lane_candidate checks rhythmic
// completeness of every voice in every stave.
void fill_all_voices(TrackLane* lane, StaveId stave_id, Rational node_end);
