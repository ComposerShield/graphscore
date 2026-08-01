// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ArbitraryRangeItem;
using graphscore::ArbitraryRangeSet;
using graphscore::Articulation;
using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::ChordSet;
using graphscore::Clef;
using graphscore::ConnectorItem;
using graphscore::ConnectorSet;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
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
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::HairpinDirection;
using graphscore::InsertionCaretItem;
using graphscore::InsertionCaretSet;
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
using graphscore::MusicalSpan;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeItem;
using graphscore::NodeSet;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::NotationFragment;
using graphscore::Note;
using graphscore::NoteheadItem;
using graphscore::NoteheadSet;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
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

namespace {

// ---- small value builders ----

SpelledPitch pitch(Letter letter, std::int8_t octave = 4) {
  return *SpelledPitch::create(letter, octave);
}

Duration whole() {
  return *Duration::create(NoteValue::kWhole, 0);
}

Duration half() {
  return *Duration::create(NoteValue::kHalf, 0);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

Duration tuplet_eighth() {
  return *Duration::create(NoteValue::kEighth, 0, TupletRatio::create(3, 2));
}

Rational rat(std::int64_t num, std::int64_t den) {
  return *Rational::create(num, den);
}

constexpr Voice kVoice1 = *Voice::create(Voice::kMin);
constexpr Voice kVoice2 = *Voice::create(2);
constexpr Voice kVoice3 = *Voice::create(3);
constexpr Voice kVoice4 = *Voice::create(Voice::kMax);

VoiceContent build_voice(std::vector<VoiceEvent> events) {
  VoiceContent voice;
  for (VoiceEvent& event : events) {
    const Result result = voice.append(std::move(event));
    assert(result.ok());
    (void)result;
  }
  return voice;
}

// A voice consisting entirely of automatically-decomposed rests covering
// [0, length).
VoiceContent rest_filled(Rational length) {
  VoiceContent voice;
  const Result result = voice.normalize(length);
  assert(result.ok());
  (void)result;
  return voice;
}

const FragmentVoicePart* find_part(const NotationFragment& fragment,
                                   std::size_t             track_ordinal,
                                   std::size_t stave_ordinal, Voice voice) {
  for (const FragmentVoicePart& part : fragment.parts()) {
    if (part.track_ordinal == track_ordinal &&
        part.stave_ordinal == stave_ordinal && part.voice == voice)
      return &part;
  }
  return nullptr;
}

bool same_event_structure(const VoiceEvent& a, const VoiceEvent& b) {
  if (a.index() != b.index())
    return false;
  if (const auto* note_a = std::get_if<Note>(&a)) {
    const auto& note_b = std::get<Note>(b);
    return note_a->pitch == note_b.pitch &&
           note_a->duration == note_b.duration &&
           note_a->tied_to_next == note_b.tied_to_next &&
           note_a->articulations == note_b.articulations &&
           note_a->stem == note_b.stem;
  }
  if (const auto* chord_a = std::get_if<Chord>(&a)) {
    const auto& chord_b = std::get<Chord>(b);
    if (chord_a->duration != chord_b.duration ||
        chord_a->articulations != chord_b.articulations ||
        chord_a->stem != chord_b.stem)
      return false;
    if (chord_a->notes.size() != chord_b.notes.size())
      return false;
    for (std::size_t i = 0; i < chord_a->notes.size(); ++i) {
      if (chord_a->notes[i].pitch != chord_b.notes[i].pitch ||
          chord_a->notes[i].tied_to_next != chord_b.notes[i].tied_to_next)
        return false;
    }
    return true;
  }
  return std::get<graphscore::Rest>(a).duration ==
         std::get<graphscore::Rest>(b).duration;
}

void collect_ids(const NotationFragment&        fragment,
                 std::vector<NotationEntityId>& ids) {
  for (const FragmentVoicePart& part : fragment.parts()) {
    for (const VoiceEvent& event : part.content.events()) {
      ids.push_back(event_id(event));
      if (const auto* chord = std::get_if<Chord>(&event)) {
        for (const ChordNote& notehead : chord->notes)
          ids.push_back(notehead.id);
      }
    }
    for (const DynamicMarking& marking : part.content.dynamics())
      ids.push_back(marking.id);
    for (const auto& hairpin : part.content.hairpins())
      ids.push_back(hairpin.id);
    for (const auto& slur : part.content.slurs())
      ids.push_back(slur.id);
    for (const auto& beam : part.content.beam_overrides())
      ids.push_back(beam.id);
    for (const GraceGroup& group : part.content.grace_groups()) {
      ids.push_back(group.id);
      for (const GraceNote& note : group.notes)
        ids.push_back(note.id);
    }
  }
}

// Builds a project with two tracks (A: grand staff, B: single staff), one
// node with a four-measure 4/4 timeline (measure i's key signature has
// `i` sharps, distinguishing measures in R12 tests), and empty lanes ready
// for direct voice assignment via `assign`.
struct Fixture {
  Project project{ProjectId::generate(), "Test"};
  TrackId track_a;
  TrackId track_b;
  StaveId stave_a_treble;
  StaveId stave_a_bass;
  StaveId stave_b;
  NodeId  node_id;

  static constexpr std::size_t kMeasureCount = 4;

  Fixture() {
    const auto tid_a = project.add_track("A", StaffLayout::grand_staff(),
                                         *MidiChannel::create(0));
    assert(tid_a.has_value());
    track_a                  = *tid_a;
    const Track* track_a_ptr = project.find_active_track(track_a);
    assert(track_a_ptr != nullptr);
    stave_a_treble = track_a_ptr->layout().staves()[0].id;
    stave_a_bass   = track_a_ptr->layout().staves()[1].id;

    const auto tid_b = project.add_track("B", StaffLayout::single_staff(),
                                         *MidiChannel::create(1));
    assert(tid_b.has_value());
    track_b                  = *tid_b;
    const Track* track_b_ptr = project.find_active_track(track_b);
    assert(track_b_ptr != nullptr);
    stave_b = track_b_ptr->layout().staves()[0].id;

    node_id      = project.add_node("Node");
    Node* node_p = project.find_node(node_id);
    assert(node_p != nullptr);

    std::vector<Measure> measures;
    for (std::size_t i = 0; i < kMeasureCount; ++i) {
      measures.push_back(
          Measure{*TimeSignature::create(4, 4),
                  *KeySignature::create(static_cast<std::int8_t>(i))});
    }
    auto timeline = NodeTimeline::create(
        measures, {StaveDefinition{stave_a_treble, Clef::kTreble},
                   StaveDefinition{stave_a_bass, Clef::kBass},
                   StaveDefinition{stave_b, Clef::kTreble}});
    assert(timeline.has_value());
    node_p->set_timeline(std::move(*timeline));

    node_p->lane(track_a)->ensure_stave(stave_a_treble);
    node_p->lane(track_a)->ensure_stave(stave_a_bass);
    node_p->lane(track_b)->ensure_stave(stave_b);
  }

  Node* node() { return project.find_node(node_id); }

  const Node* node() const { return project.find_node(node_id); }

  NodeTimeline* timeline() { return node()->timeline(); }

  void assign(TrackId track, StaveId stave_id, Voice voice,
              VoiceContent content) {
    node()->lane(track)->stave(stave_id)->voice(voice) = std::move(content);
  }

  // Assigns `content` to voice 1 of (track, stave_id) and leaves voices
  // 2-4 at their default-constructed, never-populated state -- the shape
  // an ordinary stave actually has (Track::layout() always reserves four
  // VoiceContent slots per stave, but only touched voices have content;
  // see track.hpp). A FullMeasureSet copy of a stave like this must
  // rest-fill the three untouched voices itself; that is the behavior
  // under test, not something the fixture should paper over.
  void assign_measure(TrackId track, StaveId stave_id, VoiceContent content) {
    assign(track, stave_id, kVoice1, std::move(content));
  }
};

}  // namespace

// ============================================================
// NotationFragment::create -- validation
// ============================================================

TEST(NotationFragmentTest, CreateRejectsNonPositiveSpanLength) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  EXPECT_FALSE(NotationFragment::create(Rational(0), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsEmptyTracks) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {}, parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsEmptyParts) {
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        {}, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsOutOfRangeTrackOrdinal) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{1, 0, kVoice1, rest_filled(Rational(1))}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsOutOfRangeStaveOrdinal) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 1, kVoice1, rest_filled(Rational(1))}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsOutOfRangePedalSpanOrdinal) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentPedalSpan> pedal_spans{
      FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, pedal_spans, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsOutOfRangeClefChangeOrdinal) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentClefChange> clef_changes{
      FragmentClefChange{1, 0, rat(1, 4), Clef::kBass}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, clef_changes, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsOutOfRangeStaveContextOrdinal) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentStaveContext> stave_contexts{
      FragmentStaveContext{0, 1, Clef::kTreble}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, stave_contexts, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsDuplicatePart) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))},
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsIncompleteVoiceContent) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsInvalidVoiceContent) {
  const NotationEntityId a = NotationEntityId::generate();
  VoiceContent           voice;
  ASSERT_TRUE(voice
                  .append(Note{a,
                               pitch(Letter::kC),
                               quarter(),
                               /*tied_to_next=*/true,
                               {},
                               graphscore::StemDirection::kAuto})
                  .ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  std::vector<FragmentVoicePart> parts{FragmentVoicePart{0, 0, kVoice1, voice}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsPedalSpanOutOfRange) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentPedalSpan> pedal_spans{
      FragmentPedalSpan{0, 0, rat(1, 2), Rational(2)}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, pedal_spans, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsClefChangeOutOfRange) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentClefChange> clef_changes{
      FragmentClefChange{0, 0, Rational(1), Clef::kBass}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, clef_changes, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsMeasureContextOutOfRange) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentMeasureContext> measure_contexts{FragmentMeasureContext{
      Rational(1), *TimeSignature::create(4, 4), *KeySignature::create(0)}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, measure_contexts)
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsDuplicateIdAcrossParts) {
  const NotationEntityId shared  = NotationEntityId::generate();
  VoiceContent           voice_a = build_voice({Note{shared,
                                           pitch(Letter::kC),
                                           whole(),
                                           false,
                                                     {},
                                           graphscore::StemDirection::kAuto}});
  VoiceContent           voice_b = build_voice({Note{shared,
                                           pitch(Letter::kD),
                                           whole(),
                                           false,
                                                     {},
                                           graphscore::StemDirection::kAuto}});

  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, voice_a},
      FragmentVoicePart{0, 0, kVoice2, voice_b}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsDuplicateStaveContext) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentStaveContext> stave_contexts{
      FragmentStaveContext{0, 0, Clef::kTreble},
      FragmentStaveContext{0, 0, Clef::kBass}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, stave_contexts, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsDuplicateClefChangePosition) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentClefChange> clef_changes{
      FragmentClefChange{0, 0, rat(1, 4), Clef::kBass},
      FragmentClefChange{0, 0, rat(1, 4), Clef::kTenor}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, clef_changes, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsDuplicateMeasureContextPosition) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentMeasureContext> measure_contexts{
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(0)},
      FragmentMeasureContext{Rational(0), *TimeSignature::create(3, 4),
                             *KeySignature::create(3)}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, {}, {}, {}, measure_contexts)
                   .has_value());
}

TEST(NotationFragmentTest, CreateRejectsDuplicatePedalSpanStart) {
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentPedalSpan> pedal_spans{
      FragmentPedalSpan{0, 0, Rational(0), rat(1, 2)},
      FragmentPedalSpan{0, 0, Rational(0), rat(3, 4)}};
  EXPECT_FALSE(NotationFragment::create(Rational(1), {FragmentTrackShape{1}},
                                        parts, pedal_spans, {}, {}, {})
                   .has_value());
}

TEST(NotationFragmentTest, CreateAcceptsValidFragmentAndSortsEveryCollection) {
  std::vector<FragmentTrackShape> tracks{FragmentTrackShape{2},
                                         FragmentTrackShape{1}};

  // Deliberately unsorted input order.
  std::vector<FragmentVoicePart> parts{
      FragmentVoicePart{1, 0, kVoice2, rest_filled(Rational(1))},
      FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))},
      FragmentVoicePart{0, 1, kVoice1, rest_filled(Rational(1))}};
  std::vector<FragmentPedalSpan> pedal_spans{
      FragmentPedalSpan{0, 0, rat(1, 2), Rational(1)},
      FragmentPedalSpan{0, 0, Rational(0), rat(1, 4)}};
  std::vector<FragmentClefChange> clef_changes{
      FragmentClefChange{0, 0, rat(3, 4), Clef::kBass},
      FragmentClefChange{0, 0, rat(1, 4), Clef::kAlto}};
  std::vector<FragmentStaveContext> stave_contexts{
      FragmentStaveContext{1, 0, Clef::kTenor},
      FragmentStaveContext{0, 0, Clef::kTreble}};
  std::vector<FragmentMeasureContext> measure_contexts{
      FragmentMeasureContext{rat(1, 2), *TimeSignature::create(4, 4),
                             *KeySignature::create(0)},
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(1)}};

  const std::optional<NotationFragment> fragment =
      NotationFragment::create(Rational(1), tracks, parts, pedal_spans,
                               clef_changes, stave_contexts, measure_contexts);
  ASSERT_TRUE(fragment.has_value());

  ASSERT_EQ(fragment->parts().size(), 3u);
  EXPECT_EQ(fragment->parts()[0].track_ordinal, 0u);
  EXPECT_EQ(fragment->parts()[0].stave_ordinal, 0u);
  EXPECT_EQ(fragment->parts()[1].stave_ordinal, 1u);
  EXPECT_EQ(fragment->parts()[2].track_ordinal, 1u);

  ASSERT_EQ(fragment->pedal_spans().size(), 2u);
  EXPECT_EQ(fragment->pedal_spans()[0].start, Rational(0));
  EXPECT_EQ(fragment->pedal_spans()[1].start, rat(1, 2));

  ASSERT_EQ(fragment->clef_changes().size(), 2u);
  EXPECT_EQ(fragment->clef_changes()[0].position, rat(1, 4));
  EXPECT_EQ(fragment->clef_changes()[1].position, rat(3, 4));

  ASSERT_EQ(fragment->stave_contexts().size(), 2u);
  EXPECT_EQ(fragment->stave_contexts()[0].track_ordinal, 0u);
  EXPECT_EQ(fragment->stave_contexts()[1].track_ordinal, 1u);

  ASSERT_EQ(fragment->measure_contexts().size(), 2u);
  EXPECT_EQ(fragment->measure_contexts()[0].position, Rational(0));
  EXPECT_EQ(fragment->measure_contexts()[1].position, rat(1, 2));
}

// ============================================================
// extract_fragment -- accepted arms, preconditions
// ============================================================

TEST(NotationFragmentTest, ExtractRejectsNoteheadSelection) {
  Fixture         fx;
  const Selection selection = *NoteheadSet::create(
      {NoteheadItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                    NotationEntityId::generate()}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(result.fragment.has_value());
}

TEST(NotationFragmentTest, ExtractRejectsChordSelection) {
  Fixture         fx;
  const Selection selection = *ChordSet::create(
      {graphscore::ChordItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                             NotationEntityId::generate()}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsNodeSelection) {
  Fixture                  fx;
  const Selection          selection = *NodeSet::create({NodeItem{fx.node_id}});
  const FragmentExtraction result    = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsConnectorSelection) {
  Fixture                       fx;
  const graphscore::ConnectorId connector = fx.node()->add_input("in");
  const Selection               selection =
      *ConnectorSet::create({ConnectorItem{fx.node_id, connector}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsInsertionCaretSelection) {
  Fixture         fx;
  const Selection selection = *InsertionCaretSet::create({InsertionCaretItem{
      fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1, Rational(0)}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsSelectionWithValidationDiagnostics) {
  Fixture         fx;
  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{NodeId::generate(), fx.track_a, fx.stave_a_treble, 0}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(result.fragment.has_value());
}

TEST(NotationFragmentTest, ExtractRejectsMixedNodes) {
  Fixture fx;
  fx.assign_measure(fx.track_a, fx.stave_a_treble,
                    build_voice({make_note(pitch(Letter::kC), whole())}));

  const NodeId second_node = fx.project.add_node("Second");
  Node*        second      = fx.project.find_node(second_node);
  auto         timeline    = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{fx.stave_a_treble, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  second->set_timeline(std::move(*timeline));
  second->lane(fx.track_a)->ensure_stave(fx.stave_a_treble);
  second->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1) =
      build_voice({make_note(pitch(Letter::kC), whole())});

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0},
       FullMeasureItem{second_node, fx.track_a, fx.stave_a_treble, 0}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsMixedMeasureIndexes) {
  Fixture fx;
  fx.assign_measure(fx.track_a, fx.stave_a_treble,
                    build_voice({make_note(pitch(Letter::kC), whole())}));

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0},
       FullMeasureItem{fx.node_id, fx.track_b, fx.stave_b, 1}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsMixedSpans) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), whole())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}},
       ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice2,
                          MusicalSpan{Rational(0), rat(1, 4)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsDegenerateRangeSpan) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), whole())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 2), rat(1, 2)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

TEST(NotationFragmentTest, ExtractRejectsRangeSpanExceedingNodeEnd) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, rest_filled(Rational(4)));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(5)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
}

// ============================================================
// extract_fragment -- basic mechanics and ordinal assignment
// ============================================================

TEST(NotationFragmentTest,
     ExtractFullMeasureSingleStaveRestFillsThreeUnpopulatedVoices) {
  Fixture fx;
  fx.assign_measure(fx.track_a, fx.stave_a_treble,
                    build_voice({make_note(pitch(Letter::kC), quarter()),
                                 make_note(pitch(Letter::kD), quarter()),
                                 make_note(pitch(Letter::kE), quarter()),
                                 make_note(pitch(Letter::kF), quarter())}));

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  EXPECT_EQ(result.fragment->span_length(), Rational(1));
  ASSERT_EQ(result.fragment->tracks().size(), 1u);
  EXPECT_EQ(result.fragment->tracks()[0].stave_count, 2u);
  ASSERT_EQ(result.fragment->parts().size(), 4u);

  const FragmentVoicePart* voice1 = find_part(*result.fragment, 0, 0, kVoice1);
  ASSERT_NE(voice1, nullptr);
  EXPECT_TRUE(
      voice1->content.check_complete(result.fragment->span_length()).ok());
  ASSERT_EQ(voice1->content.events().size(), 4u);

  for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
    const FragmentVoicePart* part = find_part(*result.fragment, 0, 0, voice);
    ASSERT_NE(part, nullptr);
    EXPECT_TRUE(
        part->content.check_complete(result.fragment->span_length()).ok());
    ASSERT_EQ(part->content.events().size(), 1u);
    EXPECT_TRUE(
        std::holds_alternative<graphscore::Rest>(part->content.events()[0]));
    EXPECT_EQ(std::get<graphscore::Rest>(part->content.events()[0]).duration,
              whole());
  }
}

TEST(NotationFragmentTest,
     ExtractArbitraryRangeExtendingPastShortVoiceIsRestFilled) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_EQ(result.fragment->span_length(), rat(1, 2));

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 2u);
  const Note& note = std::get<Note>(content.events()[0]);
  EXPECT_EQ(note.pitch, pitch(Letter::kC));
  EXPECT_EQ(note.duration, quarter());
  EXPECT_TRUE(std::holds_alternative<graphscore::Rest>(content.events()[1]));
  EXPECT_EQ(std::get<graphscore::Rest>(content.events()[1]).duration,
            quarter());
  EXPECT_TRUE(content.check_complete(rat(1, 2)).ok());
}

TEST(NotationFragmentTest,
     ExtractFullMeasureAlignedAcrossTwoTracksAssignsOrdinalsByIndex) {
  Fixture fx;
  fx.assign_measure(fx.track_a, fx.stave_a_treble,
                    build_voice({make_note(pitch(Letter::kC), whole())}));
  fx.assign_measure(fx.track_b, fx.stave_b,
                    build_voice({make_note(pitch(Letter::kG), whole())}));

  // track_b listed first; ordinals must still follow Track::index()
  // (track_a == 0, track_b == 1), not selection order.
  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_b, fx.stave_b, 0},
       FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  ASSERT_EQ(result.fragment->tracks().size(), 2u);
  EXPECT_EQ(result.fragment->tracks()[0].stave_count,
            2u);  // track_a: grand staff
  EXPECT_EQ(result.fragment->tracks()[1].stave_count,
            1u);  // track_b: single staff

  const FragmentVoicePart* part_a = find_part(*result.fragment, 0, 0, kVoice1);
  ASSERT_NE(part_a, nullptr);
  ASSERT_EQ(part_a->content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(part_a->content.events()[0]).pitch,
            pitch(Letter::kC));

  const FragmentVoicePart* part_b = find_part(*result.fragment, 1, 0, kVoice1);
  ASSERT_NE(part_b, nullptr);
  ASSERT_EQ(part_b->content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(part_b->content.events()[0]).pitch,
            pitch(Letter::kG));
}

TEST(NotationFragmentTest, ExtractGrandStaffAssignsDistinctStaveOrdinals) {
  Fixture fx;
  fx.assign_measure(fx.track_a, fx.stave_a_treble,
                    build_voice({make_note(pitch(Letter::kC), whole())}));
  fx.assign_measure(fx.track_a, fx.stave_a_bass,
                    build_voice({make_note(pitch(Letter::kC, 3), whole())}));

  // Stave ordinals index Track::layout().staves(), never
  // TrackLane::stave_ids(); this is the only end-to-end extraction that
  // names the second (bass) stave of a track, proving that rule for a
  // non-zero ordinal too.
  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0},
       FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_bass, 0}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  ASSERT_EQ(result.fragment->tracks().size(), 1u);
  EXPECT_EQ(result.fragment->tracks()[0].stave_count, 2u);

  const FragmentVoicePart* treble = find_part(*result.fragment, 0, 0, kVoice1);
  const FragmentVoicePart* bass   = find_part(*result.fragment, 0, 1, kVoice1);
  ASSERT_NE(treble, nullptr);
  ASSERT_NE(bass, nullptr);
  ASSERT_EQ(treble->content.events().size(), 1u);
  ASSERT_EQ(bass->content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(treble->content.events()[0]).pitch,
            pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(bass->content.events()[0]).pitch,
            pitch(Letter::kC, 3));

  ASSERT_EQ(result.fragment->stave_contexts().size(), 2u);
  EXPECT_EQ(result.fragment->stave_contexts()[0].stave_ordinal, 0u);
  EXPECT_EQ(result.fragment->stave_contexts()[0].clef_at_origin, Clef::kTreble);
  EXPECT_EQ(result.fragment->stave_contexts()[1].stave_ordinal, 1u);
  EXPECT_EQ(result.fragment->stave_contexts()[1].clef_at_origin, Clef::kBass);
}

TEST(NotationFragmentTest, ExtractArbitraryRangeMeasureAligned) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), whole())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_EQ(result.fragment->span_length(), Rational(1));
  ASSERT_EQ(result.fragment->parts().size(), 1u);
  EXPECT_TRUE(
      result.fragment->parts()[0].content.check_complete(Rational(1)).ok());
}

TEST(NotationFragmentTest, ExtractArbitraryRangeNotMeasureAligned) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter()),
                         make_note(pitch(Letter::kE), quarter()),
                         make_note(pitch(Letter::kF), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(3, 4)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_EQ(result.fragment->span_length(), rat(1, 2));

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 2u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kD));
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kE));
}

TEST(NotationFragmentTest, ExtractEveryPartTilesSpanExactly) {
  Fixture fx;
  fx.assign_measure(fx.track_a, fx.stave_a_treble,
                    build_voice({make_note(pitch(Letter::kC), whole())}));
  fx.assign_measure(fx.track_b, fx.stave_b,
                    build_voice({make_note(pitch(Letter::kG), whole())}));

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0},
       FullMeasureItem{fx.node_id, fx.track_b, fx.stave_b, 0}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  for (const FragmentVoicePart& part : result.fragment->parts())
    EXPECT_EQ(part.content.total_length(), result.fragment->span_length());
}

// ============================================================
// Clipping rules R1-R12
// ============================================================

TEST(NotationFragmentTest,
     R1HeadStraddleBecomesRestsAndR2SinglePieceTailClears) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), half()),
                         make_note(pitch(Letter::kD), half())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(3, 4)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<graphscore::Rest>(content.events()[0]));
  EXPECT_EQ(std::get<graphscore::Rest>(content.events()[0]).duration,
            quarter());
  const Note& tail = std::get<Note>(content.events()[1]);
  EXPECT_EQ(tail.pitch, pitch(Letter::kD));
  EXPECT_EQ(tail.duration, quarter());
  EXPECT_FALSE(tail.tied_to_next);
}

TEST(NotationFragmentTest, R2TailStraddleTruncatesIntoMultiPieceTieChain) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), whole(), false,
                                   {Articulation::kAccent})}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(5, 8)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 2u);
  const Note& first = std::get<Note>(content.events()[0]);
  EXPECT_EQ(first.pitch, pitch(Letter::kC));
  EXPECT_EQ(first.duration, half());
  EXPECT_TRUE(first.tied_to_next);
  ASSERT_EQ(first.articulations.size(), 1u);
  EXPECT_EQ(first.articulations[0], Articulation::kAccent);

  const Note& second = std::get<Note>(content.events()[1]);
  EXPECT_EQ(second.pitch, pitch(Letter::kC));
  EXPECT_EQ(second.duration, eighth());
  EXPECT_FALSE(second.tied_to_next);
  EXPECT_TRUE(second.articulations.empty());

  EXPECT_NE(first.id, second.id);
}

TEST(NotationFragmentTest, R2TailStraddleChordTruncatesIntoTiedPieces) {
  Fixture         fx;
  const ChordNote source_c{NotationEntityId::generate(), pitch(Letter::kC),
                           false};
  const ChordNote source_e{NotationEntityId::generate(), pitch(Letter::kE),
                           false};
  const Chord     chord =
      make_chord(whole(), {source_c, source_e}, {Articulation::kAccent});
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({chord}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(5, 8)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 2u);

  const Chord& first = std::get<Chord>(content.events()[0]);
  EXPECT_EQ(first.duration, half());
  ASSERT_EQ(first.notes.size(), 2u);
  EXPECT_EQ(first.notes[0].pitch, pitch(Letter::kC));
  EXPECT_EQ(first.notes[1].pitch, pitch(Letter::kE));
  EXPECT_TRUE(first.notes[0].tied_to_next);
  EXPECT_TRUE(first.notes[1].tied_to_next);
  ASSERT_EQ(first.articulations.size(), 1u);
  EXPECT_EQ(first.articulations[0], Articulation::kAccent);

  const Chord& second = std::get<Chord>(content.events()[1]);
  EXPECT_EQ(second.duration, eighth());
  ASSERT_EQ(second.notes.size(), 2u);
  EXPECT_EQ(second.notes[0].pitch, pitch(Letter::kC));
  EXPECT_EQ(second.notes[1].pitch, pitch(Letter::kE));
  EXPECT_FALSE(second.notes[0].tied_to_next);
  EXPECT_FALSE(second.notes[1].tied_to_next);
  EXPECT_TRUE(second.articulations.empty());

  // Identity: every ChordNote::id is freshly generated, colliding neither
  // with the source noteheads nor with each other.
  const std::unordered_set<NotationEntityId> fragment_notehead_ids{
      first.notes[0].id, first.notes[1].id, second.notes[0].id,
      second.notes[1].id};
  EXPECT_EQ(fragment_notehead_ids.size(), 4u);
  EXPECT_FALSE(fragment_notehead_ids.contains(source_c.id));
  EXPECT_FALSE(fragment_notehead_ids.contains(source_e.id));
  EXPECT_NE(first.id, chord.id);
  EXPECT_NE(second.id, chord.id);
  EXPECT_NE(first.id, second.id);
}

TEST(NotationFragmentTest, R3RestStraddlesBothEnds) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_rest(whole())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(3, 4)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 1u);
  EXPECT_TRUE(std::holds_alternative<graphscore::Rest>(content.events()[0]));
  EXPECT_EQ(std::get<graphscore::Rest>(content.events()[0]).duration, half());
}

TEST(NotationFragmentTest, R4PartialTupletFailsExtraction) {
  Fixture fx;
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                   make_note(pitch(Letter::kD), tuplet_eighth()),
                   make_note(pitch(Letter::kE), tuplet_eighth()),
                   make_rest(*Duration::create(NoteValue::kQuarter, 1))}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 24), rat(1, 2)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(result.fragment.has_value());
}

TEST(NotationFragmentTest, R4WhollyContainedTupletCopiedVerbatim) {
  Fixture fx;
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                   make_note(pitch(Letter::kD), tuplet_eighth()),
                   make_note(pitch(Letter::kE), tuplet_eighth()),
                   make_rest(*Duration::create(NoteValue::kQuarter, 1))}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 12)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 1u);
  const Note& note = std::get<Note>(content.events()[0]);
  EXPECT_EQ(note.pitch, pitch(Letter::kC));
  EXPECT_TRUE(note.duration.tuplet().has_value());
  EXPECT_EQ(note.duration, tuplet_eighth());
}

TEST(NotationFragmentTest, R4WhollyContainedTupletChordCopiedVerbatim) {
  Fixture         fx;
  const ChordNote source_c{NotationEntityId::generate(), pitch(Letter::kC),
                           false};
  const ChordNote source_e{NotationEntityId::generate(), pitch(Letter::kE),
                           false};
  const Chord tuplet_chord = make_chord(tuplet_eighth(), {source_c, source_e});
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({tuplet_chord, make_note(pitch(Letter::kD), tuplet_eighth()),
                   make_note(pitch(Letter::kF), tuplet_eighth()),
                   make_rest(*Duration::create(NoteValue::kQuarter, 1))}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 12)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 1u);
  const Chord& chord = std::get<Chord>(content.events()[0]);
  EXPECT_TRUE(chord.duration.tuplet().has_value());
  EXPECT_EQ(chord.duration, tuplet_eighth());
  ASSERT_EQ(chord.notes.size(), 2u);
  EXPECT_EQ(chord.notes[0].pitch, pitch(Letter::kC));
  EXPECT_EQ(chord.notes[1].pitch, pitch(Letter::kE));
  EXPECT_NE(chord.notes[0].id, source_c.id);
  EXPECT_NE(chord.notes[1].id, source_e.id);
  EXPECT_NE(chord.notes[0].id, chord.notes[1].id);
  EXPECT_NE(chord.id, tuplet_chord.id);
}

TEST(NotationFragmentTest, R4PartialTupletRestFailsExtraction) {
  Fixture fx;
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_rest(tuplet_eighth()),
                   make_note(pitch(Letter::kD), tuplet_eighth()),
                   make_note(pitch(Letter::kE), tuplet_eighth()),
                   make_rest(*Duration::create(NoteValue::kQuarter, 1))}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 24), rat(1, 2)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(result.fragment.has_value());
}

TEST(NotationFragmentTest, R4PartialTupletChordFailsExtraction) {
  Fixture         fx;
  const ChordNote source_c{NotationEntityId::generate(), pitch(Letter::kC),
                           false};
  const ChordNote source_e{NotationEntityId::generate(), pitch(Letter::kE),
                           false};
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_chord(tuplet_eighth(), {source_c, source_e}),
                   make_note(pitch(Letter::kD), tuplet_eighth()),
                   make_note(pitch(Letter::kF), tuplet_eighth()),
                   make_rest(*Duration::create(NoteValue::kQuarter, 1))}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 24), rat(1, 2)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  EXPECT_EQ(result.status.code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(result.fragment.has_value());
}

TEST(NotationFragmentTest, R5InternalTiePreservedFinalTieCleared) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice(
                {make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true),
                 make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true),
                 make_note(pitch(Letter::kC), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.events().size(), 2u);
  EXPECT_TRUE(std::get<Note>(content.events()[0]).tied_to_next);
  EXPECT_FALSE(std::get<Note>(content.events()[1]).tied_to_next);
}

TEST(NotationFragmentTest, R6SlurClippedToSurvivingEndpoints) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), quarter());
  const Note   note_b = make_note(pitch(Letter::kD), quarter());
  const Note   note_c = make_note(pitch(Letter::kE), quarter());
  const Note   note_d = make_note(pitch(Letter::kF), quarter());
  VoiceContent voice  = build_voice({note_a, note_b, note_c, note_d});
  ASSERT_TRUE(voice.add_slur(make_slur(note_a.id, note_d.id)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.slurs().size(), 1u);
  const auto&            slur     = content.slurs()[0];
  const NotationEntityId first_id = event_id(content.events()[0]);
  const NotationEntityId last_id  = event_id(content.events()[2]);
  EXPECT_EQ(slur.start_event, first_id);
  EXPECT_EQ(slur.end_event, last_id);
}

TEST(NotationFragmentTest, R6SlurDroppedWhenDegenerate) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), quarter());
  const Note   note_b = make_note(pitch(Letter::kD), quarter());
  const Note   note_c = make_note(pitch(Letter::kE), quarter());
  const Note   note_d = make_note(pitch(Letter::kF), quarter());
  VoiceContent voice  = build_voice({note_a, note_b, note_c, note_d});
  ASSERT_TRUE(voice.add_slur(make_slur(note_a.id, note_d.id)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(3, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_TRUE(result.fragment->parts()[0].content.slurs().empty());
}

TEST(NotationFragmentTest, R6HairpinClippedToSurvivingEndpoints) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), quarter());
  const Note   note_b = make_note(pitch(Letter::kD), quarter());
  const Note   note_c = make_note(pitch(Letter::kE), quarter());
  const Note   note_d = make_note(pitch(Letter::kF), quarter());
  VoiceContent voice  = build_voice({note_a, note_b, note_c, note_d});
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(note_a.id, note_d.id,
                                            HairpinDirection::kCrescendo))
                  .ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.hairpins().size(), 1u);
  EXPECT_EQ(content.hairpins()[0].start_event, event_id(content.events()[0]));
  EXPECT_EQ(content.hairpins()[0].end_event, event_id(content.events()[2]));
}

TEST(NotationFragmentTest, R7BeamOverrideFilteredKeepsEnoughSurvivors) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), eighth());
  const Note   note_b = make_note(pitch(Letter::kD), eighth());
  const Note   note_c = make_note(pitch(Letter::kE), eighth());
  const Note   note_d = make_note(pitch(Letter::kF), eighth());
  VoiceContent voice =
      build_voice({note_a, note_b, note_c, note_d, make_rest(half())});
  ASSERT_TRUE(voice
                  .add_beam_override(make_beam_override(
                      BeamOverride::Kind::kJoin,
                      {note_a.id, note_b.id, note_c.id, note_d.id}))
                  .ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 8), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.beam_overrides().size(), 1u);
  EXPECT_EQ(content.beam_overrides()[0].events.size(), 3u);
}

TEST(NotationFragmentTest, R7BeamOverrideDroppedWhenFewerThanTwoSurvive) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), eighth());
  const Note   note_b = make_note(pitch(Letter::kD), eighth());
  const Note   note_c = make_note(pitch(Letter::kE), eighth());
  const Note   note_d = make_note(pitch(Letter::kF), eighth());
  VoiceContent voice =
      build_voice({note_a, note_b, note_c, note_d, make_rest(half())});
  ASSERT_TRUE(voice
                  .add_beam_override(make_beam_override(
                      BeamOverride::Kind::kJoin,
                      {note_a.id, note_b.id, note_c.id, note_d.id}))
                  .ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(3, 8), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_TRUE(result.fragment->parts()[0].content.beam_overrides().empty());
}

TEST(NotationFragmentTest, R8DynamicKeptWhenEventSurvives) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), quarter());
  VoiceContent voice =
      build_voice({note_a, make_note(pitch(Letter::kD), quarter()),
                   make_note(pitch(Letter::kE), half())});
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(note_a.id, Dynamic::kFf)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event, event_id(content.events()[0]));
  EXPECT_EQ(content.dynamics()[0].value, Dynamic::kFf);
}

TEST(NotationFragmentTest, R8DynamicDroppedWhenEventExcluded) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), quarter());
  VoiceContent voice =
      build_voice({note_a, make_note(pitch(Letter::kD), quarter()),
                   make_note(pitch(Letter::kE), half())});
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(note_a.id, Dynamic::kFf)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_TRUE(result.fragment->parts()[0].content.dynamics().empty());
}

TEST(NotationFragmentTest, R9GraceGroupKeptWithRegeneratedIds) {
  Fixture         fx;
  const Note      note_a = make_note(pitch(Letter::kC), whole());
  VoiceContent    voice  = build_voice({note_a});
  const GraceNote source_grace{NotationEntityId::generate(), pitch(Letter::kB),
                               eighth(), GraceNoteType::kAcciaccatura, true};
  ASSERT_TRUE(
      voice.add_grace_group(make_grace_group(note_a.id, {source_grace})).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  const VoiceContent& content = result.fragment->parts()[0].content;
  ASSERT_EQ(content.grace_groups().size(), 1u);
  EXPECT_EQ(content.grace_groups()[0].principal_event,
            event_id(content.events()[0]));
  ASSERT_EQ(content.grace_groups()[0].notes.size(), 1u);
  EXPECT_NE(content.grace_groups()[0].notes[0].id, source_grace.id);
  EXPECT_EQ(content.grace_groups()[0].notes[0].pitch, source_grace.pitch);
  EXPECT_EQ(content.grace_groups()[0].notes[0].slashed, source_grace.slashed);
}

TEST(NotationFragmentTest, R9GraceGroupDroppedWhenPrincipalExcluded) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), quarter());
  VoiceContent voice  = build_voice(
      {note_a,
        make_note(pitch(Letter::kD), *Duration::create(NoteValue::kQuarter, 1)),
        make_rest(*Duration::create(NoteValue::kQuarter, 1))});
  const GraceNote source_grace{NotationEntityId::generate(), pitch(Letter::kB),
                               eighth(), GraceNoteType::kAcciaccatura, false};
  ASSERT_TRUE(
      voice.add_grace_group(make_grace_group(note_a.id, {source_grace})).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_TRUE(result.fragment->parts()[0].content.grace_groups().empty());
}

TEST(NotationFragmentTest, R10PedalSpanIntersectedAndRelocated) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, rest_filled(Rational(1)));
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_a)
                  ->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(1, 8), rat(3, 4)))
                  .ok());

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  ASSERT_EQ(result.fragment->pedal_spans().size(), 1u);
  EXPECT_EQ(result.fragment->pedal_spans()[0].start, Rational(0));
  EXPECT_EQ(result.fragment->pedal_spans()[0].end, rat(1, 2));
}

TEST(NotationFragmentTest, R10PedalSpanDroppedWhenOutsideRange) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, rest_filled(Rational(1)));
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_a)
                  ->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(Rational(0), rat(1, 8)))
                  .ok());

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  EXPECT_TRUE(result.fragment->pedal_spans().empty());
}

TEST(NotationFragmentTest, R11ClefChangeInsideCopiedAndOriginRecorded) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, rest_filled(Rational(1)));
  ASSERT_TRUE(fx.timeline()
                  ->clef_lane(fx.stave_a_treble)
                  ->add_change(rat(1, 4), Clef::kBass)
                  .ok());

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 8), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  ASSERT_EQ(result.fragment->clef_changes().size(), 1u);
  EXPECT_EQ(result.fragment->clef_changes()[0].position, rat(1, 8));
  EXPECT_EQ(result.fragment->clef_changes()[0].clef, Clef::kBass);

  ASSERT_EQ(result.fragment->stave_contexts().size(), 1u);
  EXPECT_EQ(result.fragment->stave_contexts()[0].clef_at_origin, Clef::kTreble);
}

TEST(NotationFragmentTest, R11ClefAtOriginFallsBackToDefaultWithoutClefLane) {
  Project    project(ProjectId::generate(), "NoClefLane");
  const auto tid = project.add_track(
      "A", StaffLayout::single_staff(Clef::kAlto), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const Track*  track    = project.find_active_track(*tid);
  const StaveId stave_id = track->layout().staves()[0].id;

  const NodeId node_id = project.add_node("Node");
  Node*        node    = project.find_node(node_id);
  // A clef lane needs a stave to be seeded for at NodeTimeline::create time;
  // pass an empty stave list so this node has no clef lane at all.
  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}}, {});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));
  node->lane(*tid)->ensure_stave(stave_id);
  node->lane(*tid)->stave(stave_id)->voice(kVoice1) = rest_filled(Rational(1));

  const Selection selection = *ArbitraryRangeSet::create({ArbitraryRangeItem{
      node_id, *tid, stave_id, kVoice1, MusicalSpan{rat(1, 4), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  ASSERT_EQ(result.fragment->stave_contexts().size(), 1u);
  EXPECT_EQ(result.fragment->stave_contexts()[0].clef_at_origin, Clef::kAlto);
  EXPECT_TRUE(result.fragment->clef_changes().empty());
}

TEST(NotationFragmentTest, R12MeasureContextsAtOriginAndInteriorStarts) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, rest_filled(Rational(3)));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 2), rat(5, 2)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  ASSERT_EQ(result.fragment->measure_contexts().size(), 3u);
  EXPECT_EQ(result.fragment->measure_contexts()[0].position, Rational(0));
  EXPECT_EQ(result.fragment->measure_contexts()[0].key_signature.fifths(), 0);
  EXPECT_EQ(result.fragment->measure_contexts()[1].position, rat(1, 2));
  EXPECT_EQ(result.fragment->measure_contexts()[1].key_signature.fifths(), 1);
  EXPECT_EQ(result.fragment->measure_contexts()[2].position, rat(3, 2));
  EXPECT_EQ(result.fragment->measure_contexts()[2].key_signature.fifths(), 2);
}

TEST(NotationFragmentTest, R12SingleMeasureContextWhenNotCrossingBoundary) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, rest_filled(Rational(1)));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(3, 4)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());
  ASSERT_EQ(result.fragment->measure_contexts().size(), 1u);
  EXPECT_EQ(result.fragment->measure_contexts()[0].position, Rational(0));
}

TEST(NotationFragmentTest, R12PickdownOriginUsesFinalMeasureSignatures) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()->set_pickdown(rat(1, 2)).ok());
  const Rational node_end = fx.timeline()->node_end();
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, rest_filled(node_end));

  // The main region is [0, 4); the pickdown is [4, 4.5). This range's
  // origin lies entirely inside the pickdown, where MeasureMap has no
  // containing measure index.
  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(4), node_end}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  ASSERT_EQ(result.fragment->measure_contexts().size(), 1u);
  EXPECT_EQ(result.fragment->measure_contexts()[0].position, Rational(0));
  EXPECT_TRUE(result.fragment->measure_contexts()[0].time_signature ==
              *TimeSignature::create(4, 4));
  EXPECT_EQ(result.fragment->measure_contexts()[0].key_signature.fifths(), 3);
}

// ============================================================
// Identity
// ============================================================

TEST(NotationFragmentTest, FragmentIdsAreDisjointFromSourceProject) {
  Fixture      fx;
  const Note   note_a = make_note(pitch(Letter::kC), quarter());
  VoiceContent voice =
      build_voice({note_a, make_note(pitch(Letter::kD), quarter()),
                   make_note(pitch(Letter::kE), half())});
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(note_a.id, Dynamic::kFf)).ok());
  const NotationEntityId source_dynamic_id = voice.dynamics()[0].id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(voice));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  std::vector<NotationEntityId> fragment_ids;
  collect_ids(*result.fragment, fragment_ids);

  const std::vector<NotationEntityId> source_ids{note_a.id, source_dynamic_id};
  for (const NotationEntityId fragment_id : fragment_ids) {
    for (const NotationEntityId source_id : source_ids)
      EXPECT_NE(fragment_id, source_id);
  }
}

TEST(NotationFragmentTest, FragmentIdsAreUniqueWithinThemselves) {
  Fixture fx;
  fx.assign_measure(fx.track_a, fx.stave_a_treble,
                    build_voice({make_note(pitch(Letter::kC), quarter()),
                                 make_note(pitch(Letter::kD), quarter()),
                                 make_note(pitch(Letter::kE), half())}));

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});
  const FragmentExtraction result = extract_fragment(fx.project, selection);
  ASSERT_EQ(result.status.code(), ResultCode::kSuccess);
  ASSERT_TRUE(result.fragment.has_value());

  std::vector<NotationEntityId> ids;
  collect_ids(*result.fragment, ids);
  const std::unordered_set<NotationEntityId> unique_ids(ids.begin(), ids.end());
  EXPECT_EQ(ids.size(), unique_ids.size());
}

TEST(NotationFragmentTest, TwoExtractionsAreDisjointInIdsButStructurallyEqual) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter()),
                         make_note(pitch(Letter::kE), half())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});
  const FragmentExtraction first  = extract_fragment(fx.project, selection);
  const FragmentExtraction second = extract_fragment(fx.project, selection);
  ASSERT_TRUE(first.fragment.has_value());
  ASSERT_TRUE(second.fragment.has_value());

  std::vector<NotationEntityId> first_ids;
  std::vector<NotationEntityId> second_ids;
  collect_ids(*first.fragment, first_ids);
  collect_ids(*second.fragment, second_ids);
  const std::unordered_set<NotationEntityId> first_set(first_ids.begin(),
                                                       first_ids.end());
  for (const NotationEntityId id : second_ids)
    EXPECT_FALSE(first_set.contains(id));

  ASSERT_EQ(first.fragment->parts().size(), second.fragment->parts().size());
  for (std::size_t i = 0; i < first.fragment->parts().size(); ++i) {
    const auto& events_a = first.fragment->parts()[i].content.events();
    const auto& events_b = second.fragment->parts()[i].content.events();
    ASSERT_EQ(events_a.size(), events_b.size());
    for (std::size_t j = 0; j < events_a.size(); ++j)
      EXPECT_TRUE(same_event_structure(events_a[j], events_b[j]));
  }
}

// ============================================================
// Non-mutation
// ============================================================

TEST(NotationFragmentTest, ExtractionDoesNotMutateProjectOnSuccessOrFailure) {
  Fixture fx;
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                   make_note(pitch(Letter::kD), tuplet_eighth()),
                   make_note(pitch(Letter::kE), tuplet_eighth()),
                   make_rest(*Duration::create(NoteValue::kQuarter, 1))}));
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_a)
                  ->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(1, 8), rat(3, 4)))
                  .ok());
  ASSERT_TRUE(fx.timeline()
                  ->clef_lane(fx.stave_a_treble)
                  ->add_change(rat(1, 4), Clef::kBass)
                  .ok());

  const TrackLane lane_before = *fx.node()->lane(fx.track_a);
  const auto      clef_before = *fx.timeline()->clef_lane(fx.stave_a_treble);

  // A successful extraction (wholly-contained tuplet, no boundary issues).
  const Selection success_selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 12)}}});
  const FragmentExtraction success_result =
      extract_fragment(fx.project, success_selection);
  ASSERT_EQ(success_result.status.code(), ResultCode::kSuccess);
  EXPECT_TRUE(*fx.node()->lane(fx.track_a) == lane_before);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == clef_before);

  // A failing extraction (a tuplet straddling the range boundary, R4).
  const Selection failing_selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 24), rat(1, 2)}}});
  const FragmentExtraction failing_result =
      extract_fragment(fx.project, failing_selection);
  ASSERT_EQ(failing_result.status.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.node()->lane(fx.track_a) == lane_before);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == clef_before);
}
