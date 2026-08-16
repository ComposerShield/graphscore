// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

namespace clipboard_test {

using graphscore::Accidental;
using graphscore::ArbitraryRangeItem;
using graphscore::ArbitraryRangeSet;
using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Clef;
using graphscore::ClefChange;
using graphscore::ClefLane;
using graphscore::CommandTransaction;
using graphscore::CutFragmentCommand;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_id;
using graphscore::extract_fragment;
using graphscore::FragmentClefChange;
using graphscore::FragmentExtraction;
using graphscore::FragmentMeasureContext;
using graphscore::FragmentPedalSpan;
using graphscore::FragmentStaveContext;
using graphscore::FragmentTrackShape;
using graphscore::FragmentVoicePart;
using graphscore::FullMeasureItem;
using graphscore::FullMeasureSet;
using graphscore::HairpinDirection;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::MusicalSpan;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationDiagnostic;
using graphscore::NotationEntityId;
using graphscore::NotationFragment;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::PasteAnchor;
using graphscore::PasteFragmentCommand;
using graphscore::PasteScope;
using graphscore::PedalSpan;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::Selection;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::TimeSignature;
using graphscore::Track;
using graphscore::TrackId;
using graphscore::TrackLane;
using graphscore::TupletRatio;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

SpelledPitch pitch(Letter letter, std::int8_t octave = 4);
Duration     whole();
Duration     half();
Duration     quarter();
Duration     eighth();
Duration     tuplet_eighth();
Rational     rat(std::int64_t num, std::int64_t den);

inline constexpr Voice kVoice1 = *Voice::create(Voice::kMin);
inline constexpr Voice kVoice2 = *Voice::create(2);
inline constexpr Voice kVoice3 = *Voice::create(3);
inline constexpr Voice kVoice4 = *Voice::create(Voice::kMax);

VoiceContent                        build_voice(std::vector<VoiceEvent> events);
VoiceContent                        rest_filled(Rational length);
std::vector<FragmentMeasureContext> default_measure_contexts();
NotationFragment                    make_fragment(
                       Rational span, std::vector<FragmentTrackShape> tracks,
                       std::vector<FragmentVoicePart>      parts,
                       std::vector<FragmentPedalSpan>      pedal_spans    = {},
                       std::vector<FragmentClefChange>     clef_changes   = {},
                       std::vector<FragmentStaveContext>   stave_contexts = {},
                       std::vector<FragmentMeasureContext> measure_contexts =
                           default_measure_contexts());
bool fragments_structurally_equal(const NotationFragment& a,
                                  const NotationFragment& b);
void collect_ids(const VoiceContent&            content,
                 std::vector<NotationEntityId>& ids);

struct Fixture {
  Project project{ProjectId::generate(), "Test"};
  TrackId track_a;
  TrackId track_b;
  StaveId stave_a_treble;
  StaveId stave_a_bass;
  StaveId stave_b;
  NodeId  node_id;

  static constexpr std::size_t kMeasureCount = 4;

  Fixture();
  Node*         node();
  NodeTimeline* timeline();
  Rational      node_end();
  void          assign(TrackId track, StaveId stave_id, Voice voice,
                       VoiceContent content);
  void      assign_and_complete(TrackId track, StaveId stave_id, Voice voice,
                                std::vector<VoiceEvent> events);
  TrackLane lane_of(TrackId track);
};

struct UnfilledFixture {
  Project project{ProjectId::generate(), "Test"};
  TrackId track_a;
  TrackId track_b;
  StaveId stave_a_treble;
  StaveId stave_b;
  NodeId  node_id;

  UnfilledFixture();
  Node*    node();
  Rational node_end();
  void     assign_note(TrackId track, StaveId stave_id, Voice voice,
                       VoiceEvent event);
};

struct PedalOnlyFixture {
  Project project{ProjectId::generate(), "PedalOnly"};
  TrackId track_a;
  StaveId stave_a_treble;
  StaveId stave_a_bass;
  NodeId  node_id;

  static constexpr std::size_t kMeasureCount = 4;

  PedalOnlyFixture();
  Node*    node();
  Rational node_end();
};

}  // namespace clipboard_test
