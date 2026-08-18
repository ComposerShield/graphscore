// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

using graphscore::Accidental;
using graphscore::AccidentalStepDirection;
using graphscore::AddIntervalCommand;
using graphscore::ArbitraryRangeItem;
using graphscore::ArbitraryRangeSet;
using graphscore::audition_for_add_interval;
using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordItem;
using graphscore::ChordNote;
using graphscore::ChordSet;
using graphscore::Command;
using graphscore::CommandHistory;
using graphscore::CommandTransaction;
using graphscore::ConnectorId;
using graphscore::ConnectorItem;
using graphscore::ConnectorSet;
using graphscore::decompose_measure_aligned_rests;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::FullMeasureItem;
using graphscore::FullMeasureSet;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Hairpin;
using graphscore::HairpinDirection;
using graphscore::InsertionCaretItem;
using graphscore::InsertionCaretSet;
using graphscore::interval_target_pitch;
using graphscore::IntervalDirection;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_add_interval_command;
using graphscore::make_beam_override;
using graphscore::make_chord;
using graphscore::make_convert_event_to_rest_command;
using graphscore::make_delete_notehead_command;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_move_notehead_command;
using graphscore::make_note;
using graphscore::make_note_entry_command;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::make_step_accidental_command;
using graphscore::MarkingItem;
using graphscore::MarkingKind;
using graphscore::MarkingSet;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::Mode;
using graphscore::MusicalSpan;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeItem;
using graphscore::NodeSet;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::notehead_key_signature;
using graphscore::notehead_measure_index;
using graphscore::NoteheadItem;
using graphscore::NoteheadSet;
using graphscore::NoteheadStepDirection;
using graphscore::NotePaletteEntryKind;
using graphscore::NotePaletteEntrySpec;
using graphscore::NotePaletteState;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::RestItem;
using graphscore::RestSet;
using graphscore::Selection;
using graphscore::selection_after_convert_to_rest;
using graphscore::selection_after_notehead_delete;
using graphscore::SetEventCommand;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::StemDirection;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::TupletRatio;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

namespace note_entry_test {

[[nodiscard]] Measure measure(std::uint8_t  numerator   = 4,
                              std::uint16_t denominator = 4);

struct Fixture {
  Project project{ProjectId::generate(), "Entry"};
  NodeId  node_id;
  TrackId track_id;

  // Defaults to two 4/4 measures (node_end() == 2); pass an explicit
  // `measures` to exercise a meter where the measure-aligned fill differs
  // from a naive whole-span decomposition (e.g. three 3/4 bars).
  explicit Fixture(std::vector<Measure> measures = {measure(), measure()}) {
    const auto added = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
    EXPECT_TRUE(added.has_value());
    track_id   = *added;
    node_id    = project.add_node("Node");
    auto* lane = project.find_node(node_id)->lane(track_id);
    lane->ensure_stave(stave_id());
    auto timeline = NodeTimeline::create(
        std::move(measures), {project.active_tracks()[0].layout().staves()[0]});
    EXPECT_TRUE(timeline.has_value());
    project.find_node(node_id)->set_timeline(std::move(*timeline));
  }

  [[nodiscard]] StaveId stave_id() const {
    return project.active_tracks()[0].layout().staves()[0].id;
  }

  [[nodiscard]] TrackId track() const { return track_id; }

  [[nodiscard]] VoiceContent& voice(std::uint8_t voice_index = 1) {
    return project.find_node(node_id)
        ->lane(track_id)
        ->stave(stave_id())
        ->voice(*Voice::create(voice_index));
  }

  // Normalizes the voice to the node's total length (fills with rests).
  void normalize_voice(std::uint8_t voice_index = 1) {
    const Rational end = node_end();
    EXPECT_TRUE(voice(voice_index).normalize(end).ok());
  }

  [[nodiscard]] Rational node_end() const {
    return project.find_node(node_id)->timeline()->node_end();
  }
};

// Helper: appends a whole-note rest and returns it.
Rest append_whole_rest(Fixture& fixture, std::uint8_t voice_index = 1);

// Helper: appends a quarter note, returns it.
Note append_quarter_note(Fixture& fixture, const SpelledPitch& pitch,
                         std::uint8_t voice_index = 1);

// Helper: construct an armed entry spec.
[[nodiscard]] NotePaletteEntrySpec armed(
    NoteValue            note_value  = NoteValue::kQuarter,
    NotePaletteEntryKind entry_kind  = NotePaletteEntryKind::kNote,
    std::uint8_t         voice_index = 1);

// Helper: constructs an acciaccatura grace note with a fresh id. Shared
// by the notehead-deletion, event-to-rest conversion, and interval-entry
// suites.
[[nodiscard]] GraceNote grace_note(const SpelledPitch& pitch);

}  // namespace note_entry_test
