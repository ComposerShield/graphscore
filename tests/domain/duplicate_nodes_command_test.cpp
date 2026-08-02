// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Clef;
using graphscore::ConnectorId;
using graphscore::ConnectorType;
using graphscore::DuplicateNodesCommand;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_id;
using graphscore::EventId;
using graphscore::EventListener;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Graph;
using graphscore::GraphPosition;
using graphscore::HairpinDirection;
using graphscore::InputConnector;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_beam_override;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeItem;
using graphscore::NodeSet;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::OutputConnector;
using graphscore::PedalSpan;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::QueuePolicy;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::StaveVoices;
using graphscore::Tempo;
using graphscore::TempoPoint;
using graphscore::TempoSegmentKind;
using graphscore::TimeSignature;
using graphscore::Track;
using graphscore::TrackId;
using graphscore::TrackLane;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

namespace {

Project make_project() {
  return Project(ProjectId::generate(), "Test");
}

SpelledPitch pitch(Letter letter, std::int8_t octave = 4) {
  return *SpelledPitch::create(letter, octave);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

Rational rat(std::int64_t num, std::int64_t den) {
  return *Rational::create(num, den);
}

constexpr Voice kVoice1 = *Voice::create(Voice::kMin);

VoiceContent build_voice(std::vector<VoiceEvent> events) {
  VoiceContent voice;
  for (VoiceEvent& event : events) {
    const Result result = voice.append(std::move(event));
    assert(result.ok());
    (void)result;
  }
  return voice;
}

// Builds a single-track, single-node project with a 4-measure 4/4 timeline
// (node_end() == Rational(4)), one stave, and every voice pre-filled with
// automatic rests -- the real shape of an untouched voice in a valid
// project (see clipboard_command_test.cpp's Fixture for the same rule).
struct Fixture {
  Project project = make_project();
  TrackId track;
  StaveId stave;
  NodeId  node_id;

  Fixture() {
    const auto tid = project.add_track("Track", StaffLayout::single_staff(),
                                       *MidiChannel::create(0));
    assert(tid.has_value());
    track                  = *tid;
    const Track* track_ptr = project.find_active_track(track);
    assert(track_ptr != nullptr);
    stave = track_ptr->layout().staves()[0].id;

    node_id      = project.add_node("A");
    Node* node_p = project.find_node(node_id);
    assert(node_p != nullptr);

    std::vector<Measure> measures;
    for (int i = 0; i < 4; ++i) {
      measures.push_back(
          Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
    }
    auto timeline =
        NodeTimeline::create(measures, {StaveDefinition{stave, Clef::kTreble}});
    assert(timeline.has_value());
    node_p->set_timeline(std::move(*timeline));
    node_p->lane(track)->ensure_stave(stave);

    const Rational end = node_p->timeline()->node_end();
    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
      VoiceContent content;
      const Result result = content.normalize(end);
      assert(result.ok());
      (void)result;
      node_p->lane(track)->stave(stave)->voice(*Voice::create(v)) =
          std::move(content);
    }
  }

  Node* node() { return project.find_node(node_id); }
};

// Every duplicate created by executing `cmd` against a project holding only
// `source_id` and its duplicate, found by exclusion.
const Node* find_only_duplicate(const Project& project, NodeId source_id) {
  const Node* found = nullptr;
  for (const Node& node : project.nodes()) {
    if (node.id() == source_id)
      continue;
    found = &node;
  }
  return found;
}

void collect_voice_ids(const VoiceContent&            content,
                       std::vector<NotationEntityId>& out) {
  for (const VoiceEvent& event : content.events()) {
    out.push_back(event_id(event));
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& notehead : chord->notes)
        out.push_back(notehead.id);
    }
  }
  for (const auto& marking : content.dynamics())
    out.push_back(marking.id);
  for (const auto& hairpin : content.hairpins())
    out.push_back(hairpin.id);
  for (const auto& slur : content.slurs())
    out.push_back(slur.id);
  for (const auto& beam : content.beam_overrides())
    out.push_back(beam.id);
  for (const auto& group : content.grace_groups()) {
    out.push_back(group.id);
    for (const auto& note : group.notes)
      out.push_back(note.id);
  }
}

// Collects every NodeId, ConnectorId, and NotationEntityId that appears
// anywhere in `project`: every node id, every input/output connector id on
// every node, and every notation entity id (top-level event, ChordNote,
// GraceNote, marking, pedal span) inside every lane of every active or
// archived track -- a lane keyed to an archived track (Project::archive_track
// leaves a node's already-recorded lane data untouched) is just as much
// "anywhere in the project" as one keyed to an active track.
struct IdInventory {
  std::vector<NodeId>           node_ids;
  std::vector<ConnectorId>      connector_ids;
  std::vector<NotationEntityId> entity_ids;
};

void collect_lane_entity_ids(const std::vector<Track>& tracks, const Node& node,
                             IdInventory& inventory) {
  for (const Track& track : tracks) {
    if (!node.has_lane(track.id()))
      continue;
    const TrackLane* lane = node.lane(track.id());
    for (const StaveId stave_id : lane->stave_ids()) {
      const auto* stave = lane->stave(stave_id);
      if (stave == nullptr)
        continue;
      for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
        collect_voice_ids(stave->voice(*Voice::create(v)),
                          inventory.entity_ids);
      }
      const auto* spans = lane->pedal_spans(stave_id);
      if (spans != nullptr) {
        for (const auto& span : *spans)
          inventory.entity_ids.push_back(span.id);
      }
    }
  }
}

IdInventory collect_ids(const Project& project) {
  IdInventory inventory;
  for (const Node& node : project.nodes()) {
    inventory.node_ids.push_back(node.id());
    for (const auto& input : node.inputs())
      inventory.connector_ids.push_back(input.id());
    for (const auto& output : node.outputs())
      inventory.connector_ids.push_back(output.id());

    collect_lane_entity_ids(project.active_tracks(), node, inventory);
    collect_lane_entity_ids(project.archived_tracks(), node, inventory);
  }
  return inventory;
}

template <typename T>
std::size_t distinct_count(const std::vector<T>& values) {
  return std::unordered_set<T>(values.begin(), values.end()).size();
}

}  // namespace

// ============================================================
// N1, N11 -- identity, verbatim metadata, position offset
// ============================================================

TEST(DuplicateNodesCommandTest, SingleNodeDuplicateFreshIdentityAndOffset) {
  Fixture fx;
  fx.node()->set_color(0x11223344);
  fx.node()->set_notes("original notes");
  fx.node()->set_position(GraphPosition{10.0, 20.0});

  const GraphPosition   offset{5.0, -2.0};
  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}), offset);
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  ASSERT_EQ(fx.project.nodes().size(), 2u);
  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  EXPECT_NE(dup->id(), fx.node_id);
  EXPECT_EQ(dup->name(), "A");
  EXPECT_EQ(dup->color(), 0x11223344u);
  EXPECT_EQ(dup->notes(), "original notes");
  EXPECT_DOUBLE_EQ(dup->position().x, 15.0);
  EXPECT_DOUBLE_EQ(dup->position().y, 18.0);
}

TEST(DuplicateNodesCommandTest, DefaultOffsetLeavesPositionEqual) {
  Fixture fx;
  fx.node()->set_position(GraphPosition{3.0, 4.0});

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  EXPECT_DOUBLE_EQ(dup->position().x, 3.0);
  EXPECT_DOUBLE_EQ(dup->position().y, 4.0);
}

// ============================================================
// N1 -- global UUID-uniqueness probe
// ============================================================

TEST(DuplicateNodesCommandTest, GlobalUuidUniquenessAcrossWholeProject) {
  Fixture      fx;
  const Note   c       = make_note(pitch(Letter::kC), quarter());
  const Note   d       = make_note(pitch(Letter::kD), quarter());
  VoiceContent content = build_voice({c, d});
  ASSERT_TRUE(
      content.add_dynamic(make_dynamic_marking(c.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(content.add_slur(make_slur(c.id, d.id)).ok());
  ASSERT_TRUE(content.normalize(fx.node()->timeline()->node_end()).ok());
  fx.node()->lane(fx.track)->stave(fx.stave)->voice(kVoice1) = content;
  ASSERT_TRUE(
      fx.node()
          ->lane(fx.track)
          ->add_pedal_span(fx.stave, make_pedal_span(Rational(0), Rational(1)))
          .ok());

  Node*                              source     = fx.node();
  [[maybe_unused]] const ConnectorId source_in  = source->add_input("In");
  [[maybe_unused]] const ConnectorId source_out = source->add_output("Out");

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const IdInventory inventory = collect_ids(fx.project);
  EXPECT_EQ(inventory.node_ids.size(), 2u);
  EXPECT_EQ(distinct_count(inventory.node_ids), inventory.node_ids.size());
  EXPECT_EQ(distinct_count(inventory.connector_ids),
            inventory.connector_ids.size());
  EXPECT_EQ(distinct_count(inventory.entity_ids), inventory.entity_ids.size());
  EXPECT_GT(inventory.entity_ids.size(), 0u);
}

// ============================================================
// N2 -- fresh connector ids, source ids never reused
// ============================================================

TEST(DuplicateNodesCommandTest, ConnectorIdsFreshForEveryInputAndOutput) {
  Fixture           fx;
  Node*             source = fx.node();
  const ConnectorId in_id  = source->add_input("In");
  const ConnectorId out_id = source->add_output("Out");

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  ASSERT_EQ(dup->inputs().size(), 1u);
  ASSERT_EQ(dup->outputs().size(), 1u);
  EXPECT_EQ(dup->inputs()[0].name(), "In");
  EXPECT_NE(dup->inputs()[0].id(), in_id);
  EXPECT_EQ(dup->outputs()[0].name(), "Out");
  EXPECT_NE(dup->outputs()[0].id(), out_id);
}

// ============================================================
// N3 -- notation entity ids regenerated, markings remapped
// ============================================================

TEST(DuplicateNodesCommandTest,
     NotationEntityIdsRegeneratedAndMarkingsRemapped) {
  Fixture     fx;
  const Note  c = make_note(pitch(Letter::kC), quarter());
  const Note  d = make_note(pitch(Letter::kD), quarter());
  const Chord chord =
      make_chord(quarter(), {ChordNote{{}, pitch(Letter::kE), false},
                             ChordNote{{}, pitch(Letter::kG), false}});
  VoiceContent content = build_voice({c, d, chord});
  ASSERT_TRUE(
      content.add_dynamic(make_dynamic_marking(c.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(
      content
          .add_hairpin(make_hairpin(c.id, d.id, HairpinDirection::kCrescendo))
          .ok());
  ASSERT_TRUE(content.add_slur(make_slur(d.id, chord.id)).ok());
  ASSERT_TRUE(content
                  .add_beam_override(make_beam_override(
                      BeamOverride::Kind::kJoin, {c.id, d.id}))
                  .ok());
  GraceNote grace_note{
      {}, pitch(Letter::kB), eighth(), GraceNoteType::kAppoggiatura, false};
  ASSERT_TRUE(
      content.add_grace_group(make_grace_group(d.id, {grace_note})).ok());
  ASSERT_TRUE(content.normalize(fx.node()->timeline()->node_end()).ok());
  fx.node()->lane(fx.track)->stave(fx.stave)->voice(kVoice1) = content;

  const NotationEntityId original_dynamic_id = content.dynamics()[0].id;
  const NotationEntityId original_hairpin_id = content.hairpins()[0].id;
  const NotationEntityId original_slur_id    = content.slurs()[0].id;
  const NotationEntityId original_beam_id    = content.beam_overrides()[0].id;
  const NotationEntityId original_group_id   = content.grace_groups()[0].id;
  const NotationEntityId original_grace_id =
      content.grace_groups()[0].notes[0].id;

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  const TrackLane* lane = dup->lane(fx.track);
  ASSERT_NE(lane, nullptr);
  const VoiceContent& dup_content = lane->stave(fx.stave)->voice(kVoice1);

  ASSERT_GE(dup_content.events().size(), 3u);
  const Note&  dup_c     = std::get<Note>(dup_content.events()[0]);
  const Note&  dup_d     = std::get<Note>(dup_content.events()[1]);
  const Chord& dup_chord = std::get<Chord>(dup_content.events()[2]);
  EXPECT_NE(dup_c.id, c.id);
  EXPECT_NE(dup_d.id, d.id);
  EXPECT_NE(dup_chord.id, chord.id);
  ASSERT_EQ(dup_chord.notes.size(), chord.notes.size());
  for (std::size_t i = 0; i < chord.notes.size(); ++i)
    EXPECT_NE(dup_chord.notes[i].id, chord.notes[i].id);

  ASSERT_EQ(dup_content.dynamics().size(), 1u);
  EXPECT_NE(dup_content.dynamics()[0].id, original_dynamic_id);
  EXPECT_EQ(dup_content.dynamics()[0].at_event, dup_c.id);

  ASSERT_EQ(dup_content.hairpins().size(), 1u);
  EXPECT_NE(dup_content.hairpins()[0].id, original_hairpin_id);
  EXPECT_EQ(dup_content.hairpins()[0].start_event, dup_c.id);
  EXPECT_EQ(dup_content.hairpins()[0].end_event, dup_d.id);

  ASSERT_EQ(dup_content.slurs().size(), 1u);
  EXPECT_NE(dup_content.slurs()[0].id, original_slur_id);
  EXPECT_EQ(dup_content.slurs()[0].start_event, dup_d.id);
  EXPECT_EQ(dup_content.slurs()[0].end_event, dup_chord.id);

  ASSERT_EQ(dup_content.beam_overrides().size(), 1u);
  EXPECT_NE(dup_content.beam_overrides()[0].id, original_beam_id);
  ASSERT_EQ(dup_content.beam_overrides()[0].events.size(), 2u);
  EXPECT_EQ(dup_content.beam_overrides()[0].events[0], dup_c.id);
  EXPECT_EQ(dup_content.beam_overrides()[0].events[1], dup_d.id);

  ASSERT_EQ(dup_content.grace_groups().size(), 1u);
  EXPECT_NE(dup_content.grace_groups()[0].id, original_group_id);
  EXPECT_EQ(dup_content.grace_groups()[0].principal_event, dup_d.id);
  ASSERT_EQ(dup_content.grace_groups()[0].notes.size(), 1u);
  EXPECT_NE(dup_content.grace_groups()[0].notes[0].id, original_grace_id);
}

TEST(DuplicateNodesCommandTest,
     EmptyRestFilledVoiceDuplicatesWithFreshRestIds) {
  Fixture     fx;
  const auto& source_events =
      fx.node()->lane(fx.track)->stave(fx.stave)->voice(kVoice1).events();
  ASSERT_FALSE(source_events.empty());
  const NotationEntityId original_rest_id = std::get<Rest>(source_events[0]).id;

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  const VoiceContent& dup_voice =
      dup->lane(fx.track)->stave(fx.stave)->voice(kVoice1);
  ASSERT_FALSE(dup_voice.events().empty());
  ASSERT_TRUE(std::holds_alternative<Rest>(dup_voice.events()[0]));
  EXPECT_NE(std::get<Rest>(dup_voice.events()[0]).id, original_rest_id);
}

TEST(DuplicateNodesCommandTest, PedalSpansDuplicatedWithFreshIdsSameRange) {
  Fixture fx;
  ASSERT_TRUE(
      fx.node()
          ->lane(fx.track)
          ->add_pedal_span(fx.stave, make_pedal_span(rat(1, 4), rat(3, 4)))
          .ok());
  const NotationEntityId original_id =
      fx.node()->lane(fx.track)->pedal_spans(fx.stave)->at(0).id;

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  const std::vector<PedalSpan>* spans =
      dup->lane(fx.track)->pedal_spans(fx.stave);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_NE(spans->at(0).id, original_id);
  EXPECT_EQ(spans->at(0).start, rat(1, 4));
  EXPECT_EQ(spans->at(0).end, rat(3, 4));
}

// ============================================================
// L3(b) -- a stave with three genuinely unwritten voices
// ============================================================

TEST(DuplicateNodesCommandTest,
     StaveWithThreeUnwrittenVoicesDuplicatesCleanly) {
  Fixture fx;
  // Fixture rest-fills every voice by default; reset three of the four
  // back to genuinely unwritten (zero-length, no events) content -- the
  // plan's stated common real shape ("a stave normally holds four voices
  // of which only one is written"), which every other fixture-based test
  // in this file bypasses.
  for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
    if (v == kVoice1.index())
      continue;
    fx.node()->lane(fx.track)->stave(fx.stave)->voice(*Voice::create(v)) =
        VoiceContent();
  }
  fx.node()->lane(fx.track)->stave(fx.stave)->voice(kVoice1) =
      build_voice({make_note(pitch(Letter::kC), quarter())});

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  const StaveVoices* dup_stave = dup->lane(fx.track)->stave(fx.stave);
  ASSERT_NE(dup_stave, nullptr);

  const VoiceContent& written = dup_stave->voice(kVoice1);
  ASSERT_EQ(written.events().size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Note>(written.events()[0]));

  for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
    if (v == kVoice1.index())
      continue;
    const VoiceContent& unwritten = dup_stave->voice(*Voice::create(v));
    EXPECT_TRUE(unwritten.events().empty());
    EXPECT_EQ(unwritten.total_length(), Rational(0));
  }
}

// ============================================================
// N4 -- NodeTimeline copied verbatim
// ============================================================

TEST(DuplicateNodesCommandTest,
     TimelineCopiedVerbatimIncludingMeasuresClefsTempo) {
  Fixture fx;
  ASSERT_TRUE(fx.node()
                  ->timeline()
                  ->clef_lane(fx.stave)
                  ->add_change(rat(1, 2), Clef::kBass)
                  .ok());
  ASSERT_TRUE(fx.node()->timeline()->set_pickdown(rat(1, 2)).ok());
  const Rational node_end = fx.node()->timeline()->node_end();

  const std::vector<TempoPoint> points = {
      TempoPoint{Rational(0),
                 *Tempo::create(Rational(120), NoteValue::kQuarter),
                 TempoSegmentKind::kStep},
      TempoPoint{Rational(2), *Tempo::create(Rational(90), NoteValue::kQuarter),
                 TempoSegmentKind::kLinear}};
  ASSERT_TRUE(fx.node()->timeline()->set_tempo(points).ok());

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  ASSERT_TRUE(dup->has_timeline());
  const auto* dup_timeline = dup->timeline();
  EXPECT_TRUE(dup_timeline->measures() == fx.node()->timeline()->measures());
  EXPECT_EQ(dup_timeline->pickdown_duration(),
            fx.node()->timeline()->pickdown_duration());
  EXPECT_EQ(dup_timeline->node_end(), node_end);
  ASSERT_TRUE(dup_timeline->has_clef_lane(fx.stave));
  EXPECT_TRUE(*dup_timeline->clef_lane(fx.stave) ==
              *fx.node()->timeline()->clef_lane(fx.stave));
  ASSERT_NE(dup_timeline->tempo(), nullptr);
  EXPECT_TRUE(*dup_timeline->tempo() == *fx.node()->timeline()->tempo());
}

TEST(DuplicateNodesCommandTest, NodeWithNoLanesNoTimelineDuplicatesCleanly) {
  Project      project = make_project();
  const NodeId id      = project.add_node("Bare");

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{id}}));
  ASSERT_TRUE(cmd.execute(project).ok());
  EXPECT_EQ(project.nodes().size(), 2u);

  const Node* dup = find_only_duplicate(project, id);
  ASSERT_NE(dup, nullptr);
  EXPECT_FALSE(dup->has_timeline());
  EXPECT_EQ(dup->lane_count(), 0u);
}

// ============================================================
// N5 -- track/stave keys preserved, not duplicated
// ============================================================

TEST(DuplicateNodesCommandTest,
     LaneTrackAndStaveKeysPreservedTracksNotDuplicated) {
  Fixture           fx;
  const std::size_t original_track_count = fx.project.active_tracks().size();

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  EXPECT_EQ(fx.project.active_tracks().size(), original_track_count);
  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  ASSERT_TRUE(dup->has_lane(fx.track));
  const TrackLane* lane = dup->lane(fx.track);
  ASSERT_NE(lane, nullptr);
  EXPECT_TRUE(lane->has_stave(fx.stave));
}

// ============================================================
// L3(a), T1 -- archived-track lane duplicated, and covered by the N1
// global UUID-uniqueness probe
// ============================================================

TEST(DuplicateNodesCommandTest,
     ArchivedTrackLaneDuplicatedAndCoveredByUuidProbe) {
  Fixture      fx;
  const Note   c       = make_note(pitch(Letter::kC), quarter());
  const Note   d       = make_note(pitch(Letter::kD), quarter());
  VoiceContent content = build_voice({c, d});
  ASSERT_TRUE(
      content.add_dynamic(make_dynamic_marking(c.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(content.normalize(fx.node()->timeline()->node_end()).ok());
  fx.node()->lane(fx.track)->stave(fx.stave)->voice(kVoice1) = content;
  ASSERT_TRUE(
      fx.node()
          ->lane(fx.track)
          ->add_pedal_span(fx.stave, make_pedal_span(Rational(0), Rational(1)))
          .ok());
  const NotationEntityId original_pedal_id =
      fx.node()->lane(fx.track)->pedal_spans(fx.stave)->at(0).id;

  ASSERT_TRUE(fx.project.archive_track(fx.track).ok());
  ASSERT_EQ(fx.project.active_tracks().size(), 0u);
  ASSERT_EQ(fx.project.archived_tracks().size(), 1u);

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  EXPECT_EQ(dup->lane_count(), fx.node()->lane_count());
  ASSERT_TRUE(dup->has_lane(fx.track));
  const TrackLane* lane = dup->lane(fx.track);
  ASSERT_NE(lane, nullptr);
  ASSERT_TRUE(lane->has_stave(fx.stave));

  const VoiceContent& dup_voice = lane->stave(fx.stave)->voice(kVoice1);
  ASSERT_GE(dup_voice.events().size(), 2u);
  EXPECT_NE(std::get<Note>(dup_voice.events()[0]).id, c.id);
  ASSERT_EQ(dup_voice.dynamics().size(), 1u);

  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_NE(spans->at(0).id, original_pedal_id);

  // T1: the N1 global UUID-uniqueness probe must see the archived track's
  // lane content too, not just active-track lanes.
  const IdInventory inventory = collect_ids(fx.project);
  EXPECT_EQ(inventory.node_ids.size(), 2u);
  EXPECT_EQ(distinct_count(inventory.node_ids), inventory.node_ids.size());
  EXPECT_EQ(distinct_count(inventory.connector_ids),
            inventory.connector_ids.size());
  EXPECT_EQ(distinct_count(inventory.entity_ids), inventory.entity_ids.size());
  EXPECT_GT(inventory.entity_ids.size(), 0u);
}

// ============================================================
// N6, N7, N8 -- edge remapping, drop-on-exit, automatic route
// ============================================================

TEST(DuplicateNodesCommandTest,
     IntraSelectionEdgesRemappedAndOutOfSelectionDropped) {
  Project      project = make_project();
  const NodeId a_id    = project.add_node("A");
  const NodeId b_id    = project.add_node("B");
  const NodeId c_id    = project.add_node("C");
  Node*        a       = project.find_node(a_id);
  Node*        b       = project.find_node(b_id);
  Node*        c       = project.find_node(c_id);

  const ConnectorId a_in  = a->add_input("AIn");
  const ConnectorId b_in  = b->add_input("BIn");
  const ConnectorId c_in1 = c->add_input("CIn1");
  const ConnectorId c_in2 = c->add_input("CIn2");

  const ConnectorId a_to_b_out = a->add_output("AToB");
  const ConnectorId a_self_out = a->add_output("ASelf");
  const ConnectorId a_to_c_out = a->add_output("AToC");
  const ConnectorId b_to_c_out = b->add_output("BToC");
  const ConnectorId c_to_a_out = c->add_output("CToA");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, a_to_b_out, b_id, b_in).ok());
  ASSERT_TRUE(graph.connect(a_id, a_self_out, a_id, a_in).ok());
  ASSERT_TRUE(graph.connect(a_id, a_to_c_out, c_id, c_in1).ok());
  ASSERT_TRUE(graph.connect(b_id, b_to_c_out, c_id, c_in2).ok());
  ASSERT_TRUE(graph.connect(c_id, c_to_a_out, a_id, a_in).ok());

  const std::size_t c_input_count  = c->inputs().size();
  const std::size_t c_output_count = c->outputs().size();

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{a_id}, NodeItem{b_id}}));
  ASSERT_TRUE(cmd.execute(project).ok());

  NodeId dup_a_id;
  NodeId dup_b_id;
  for (const Node& node : project.nodes()) {
    if (node.id() == a_id || node.id() == b_id || node.id() == c_id)
      continue;
    if (node.name() == "A")
      dup_a_id = node.id();
    if (node.name() == "B")
      dup_b_id = node.id();
  }
  const Node* dup_a = project.find_node(dup_a_id);
  const Node* dup_b = project.find_node(dup_b_id);
  ASSERT_NE(dup_a, nullptr);
  ASSERT_NE(dup_b, nullptr);

  const OutputConnector* dup_a_to_b = nullptr;
  const OutputConnector* dup_a_self = nullptr;
  const OutputConnector* dup_a_to_c = nullptr;
  for (const OutputConnector& out : dup_a->outputs()) {
    if (out.name() == "AToB")
      dup_a_to_b = &out;
    if (out.name() == "ASelf")
      dup_a_self = &out;
    if (out.name() == "AToC")
      dup_a_to_c = &out;
  }
  ASSERT_NE(dup_a_to_b, nullptr);
  ASSERT_NE(dup_a_self, nullptr);
  ASSERT_NE(dup_a_to_c, nullptr);

  const InputConnector* dup_b_in = nullptr;
  for (const InputConnector& in : dup_b->inputs()) {
    if (in.name() == "BIn")
      dup_b_in = &in;
  }
  ASSERT_NE(dup_b_in, nullptr);
  ASSERT_TRUE(dup_a_to_b->destination().has_value());
  EXPECT_EQ(dup_a_to_b->destination()->node, dup_b_id);
  EXPECT_EQ(dup_a_to_b->destination()->connector, dup_b_in->id());

  const InputConnector* dup_a_in = nullptr;
  for (const InputConnector& in : dup_a->inputs()) {
    if (in.name() == "AIn")
      dup_a_in = &in;
  }
  ASSERT_NE(dup_a_in, nullptr);
  ASSERT_TRUE(dup_a_self->destination().has_value());
  EXPECT_EQ(dup_a_self->destination()->node, dup_a_id);
  EXPECT_EQ(dup_a_self->destination()->connector, dup_a_in->id());

  // N7: an edge leaving the selection is dropped.
  EXPECT_FALSE(dup_a_to_c->destination().has_value());
  const OutputConnector* dup_b_to_c = nullptr;
  for (const OutputConnector& out : dup_b->outputs()) {
    if (out.name() == "BToC")
      dup_b_to_c = &out;
  }
  ASSERT_NE(dup_b_to_c, nullptr);
  EXPECT_FALSE(dup_b_to_c->destination().has_value());

  // Source nodes are untouched. DuplicateNodesCommand::execute publishes
  // new nodes via Project::restore_node, which can reallocate the
  // project's node vector, so `a`/`c` (captured before execute()) must be
  // refreshed rather than dereferenced directly.
  const Node* refreshed_a = project.find_node(a_id);
  const Node* refreshed_c = project.find_node(c_id);
  ASSERT_NE(refreshed_a, nullptr);
  ASSERT_NE(refreshed_c, nullptr);

  ASSERT_TRUE(refreshed_a->find_output(a_to_b_out)->destination().has_value());
  EXPECT_EQ(refreshed_a->find_output(a_to_b_out)->destination()->node, b_id);
  ASSERT_TRUE(refreshed_a->find_output(a_self_out)->destination().has_value());
  EXPECT_EQ(refreshed_a->find_output(a_self_out)->destination()->node, a_id);

  // N7: no non-selected node is mutated in any way.
  EXPECT_EQ(refreshed_c->inputs().size(), c_input_count);
  EXPECT_EQ(refreshed_c->outputs().size(), c_output_count);
  ASSERT_TRUE(refreshed_c->find_output(c_to_a_out)->destination().has_value());
  EXPECT_EQ(refreshed_c->find_output(c_to_a_out)->destination()->node, a_id);
  EXPECT_EQ(refreshed_c->find_output(c_to_a_out)->destination()->connector,
            a_in);
}

TEST(DuplicateNodesCommandTest, RemappedEdgeRouteIsAutomaticNotCopied) {
  Project           project = make_project();
  const NodeId      a_id    = project.add_node("A");
  const NodeId      b_id    = project.add_node("B");
  Node*             a       = project.find_node(a_id);
  Node*             b       = project.find_node(b_id);
  const ConnectorId b_in    = b->add_input("BIn");
  const ConnectorId a_out   = a->add_output("AOut");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, b_in).ok());
  OutputConnector* source_output = a->find_output(a_out);
  ASSERT_TRUE(
      source_output->route().set_custom_route({{0.0, 0.0}, {5.0, 0.0}}).ok());

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{a_id}, NodeItem{b_id}}));
  ASSERT_TRUE(cmd.execute(project).ok());

  const Node* dup_a = nullptr;
  for (const Node& node : project.nodes()) {
    if (node.id() != a_id && node.id() != b_id && node.name() == "A")
      dup_a = &node;
  }
  ASSERT_NE(dup_a, nullptr);
  ASSERT_EQ(dup_a->outputs().size(), 1u);
  EXPECT_TRUE(dup_a->outputs()[0].destination().has_value());
  EXPECT_TRUE(dup_a->outputs()[0].route().is_automatic());

  // Source's own custom route is untouched. execute() may have reallocated
  // the project's node vector (see the comment in the sibling test above),
  // so `source_output` must be re-resolved rather than dereferenced.
  const Node* refreshed_a = project.find_node(a_id);
  ASSERT_NE(refreshed_a, nullptr);
  const OutputConnector* refreshed_output = refreshed_a->find_output(a_out);
  ASSERT_NE(refreshed_output, nullptr);
  EXPECT_FALSE(refreshed_output->route().is_automatic());
}

// ============================================================
// N9 -- event bindings preserved, listener policy/capacity preserved
// ============================================================

TEST(DuplicateNodesCommandTest, EventBindingAndListenerPolicyPreserved) {
  Fixture           fx;
  Node*             node   = fx.node();
  const EventId     event  = *fx.project.events().add_event("Attack");
  const ConnectorId out_id = node->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());
  ASSERT_TRUE(node->set_listener_policy(event, QueuePolicy::kFifo, 7).ok());

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());

  const Node* dup = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup, nullptr);
  ASSERT_EQ(dup->outputs().size(), 1u);
  EXPECT_EQ(dup->outputs()[0].event_binding(), event);
  const EventListener* listener = dup->find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(listener->capacity(), 7u);
}

// ============================================================
// N10 -- start node unaffected
// ============================================================

TEST(DuplicateNodesCommandTest, StartNodeUnaffectedByDuplicateExecuteUndoRedo) {
  Fixture fx;
  ASSERT_TRUE(fx.project.set_start_node(fx.node_id).ok());

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());
  ASSERT_TRUE(fx.project.start_node().has_value());
  EXPECT_EQ(*fx.project.start_node(), fx.node_id);

  ASSERT_TRUE(cmd.undo(fx.project).ok());
  ASSERT_TRUE(fx.project.start_node().has_value());
  EXPECT_EQ(*fx.project.start_node(), fx.node_id);

  ASSERT_TRUE(cmd.redo(fx.project).ok());
  ASSERT_TRUE(fx.project.start_node().has_value());
  EXPECT_EQ(*fx.project.start_node(), fx.node_id);
}

// ============================================================
// N12 -- unknown NodeId in selection
// ============================================================

TEST(DuplicateNodesCommandTest,
     UnknownNodeIdSelectionFailsInvalidArgumentNoMutation) {
  Fixture               fx;
  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{NodeId::generate()}}));
  const Result          result = cmd.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fx.project.nodes().size(), 1u);
}

TEST(DuplicateNodesCommandTest,
     PartiallyUnknownSelectionFailsWithoutMutatingKnownNode) {
  Fixture         fx;
  const TrackLane before = *fx.node()->lane(fx.track);

  DuplicateNodesCommand cmd(
      *NodeSet::create({NodeItem{fx.node_id}, NodeItem{NodeId::generate()}}));
  const Result result = cmd.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fx.project.nodes().size(), 1u);
  EXPECT_TRUE(*fx.node()->lane(fx.track) == before);
}

// ============================================================
// N13, N14 -- exact undo, deterministic id-for-id redo
// ============================================================

TEST(DuplicateNodesCommandTest, ExecuteUndoRedoRoundTripIdenticalIds) {
  Fixture fx;
  fx.node()->set_color(0xAABBCCDD);
  [[maybe_unused]] const ConnectorId source_in  = fx.node()->add_input("In");
  [[maybe_unused]] const ConnectorId source_out = fx.node()->add_output("Out");
  VoiceContent content = build_voice({make_note(pitch(Letter::kC), quarter())});
  ASSERT_TRUE(content.normalize(fx.node()->timeline()->node_end()).ok());
  fx.node()->lane(fx.track)->stave(fx.stave)->voice(kVoice1) = content;

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());
  ASSERT_EQ(fx.project.nodes().size(), 2u);
  const Node* dup_before = find_only_duplicate(fx.project, fx.node_id);
  ASSERT_NE(dup_before, nullptr);
  const NodeId dup_id = dup_before->id();
  ASSERT_EQ(dup_before->inputs().size(), 1u);
  ASSERT_EQ(dup_before->outputs().size(), 1u);
  const ConnectorId   dup_in_id  = dup_before->inputs()[0].id();
  const ConnectorId   dup_out_id = dup_before->outputs()[0].id();
  const VoiceContent& dup_voice_before =
      dup_before->lane(fx.track)->stave(fx.stave)->voice(kVoice1);
  ASSERT_FALSE(dup_voice_before.events().empty());
  ASSERT_TRUE(std::holds_alternative<Note>(dup_voice_before.events()[0]));
  const NotationEntityId dup_note_id =
      std::get<Note>(dup_voice_before.events()[0]).id;

  ASSERT_TRUE(cmd.undo(fx.project).ok());
  EXPECT_EQ(fx.project.nodes().size(), 1u);
  EXPECT_EQ(fx.project.find_node(dup_id), nullptr);

  ASSERT_TRUE(cmd.redo(fx.project).ok());
  EXPECT_EQ(fx.project.nodes().size(), 2u);
  const Node* redone = fx.project.find_node(dup_id);
  ASSERT_NE(redone, nullptr);
  EXPECT_EQ(redone->color(), 0xAABBCCDDu);

  // T2: id-for-id redo means every id embedded in the redone duplicate --
  // not just its NodeId -- is identical to what execute() first minted,
  // including its ConnectorIds and a NotationEntityId inside its lane.
  ASSERT_EQ(redone->inputs().size(), 1u);
  ASSERT_EQ(redone->outputs().size(), 1u);
  EXPECT_EQ(redone->inputs()[0].id(), dup_in_id);
  EXPECT_EQ(redone->outputs()[0].id(), dup_out_id);
  const VoiceContent& dup_voice_after =
      redone->lane(fx.track)->stave(fx.stave)->voice(kVoice1);
  ASSERT_FALSE(dup_voice_after.events().empty());
  ASSERT_TRUE(std::holds_alternative<Note>(dup_voice_after.events()[0]));
  EXPECT_EQ(std::get<Note>(dup_voice_after.events()[0]).id, dup_note_id);

  ASSERT_TRUE(cmd.undo(fx.project).ok());
  EXPECT_EQ(fx.project.nodes().size(), 1u);
}

TEST(DuplicateNodesCommandTest, SourceNodeItselfUnmutatedThroughLifecycle) {
  Fixture fx;
  fx.node()->lane(fx.track)->stave(fx.stave)->voice(kVoice1) =
      build_voice({make_note(pitch(Letter::kC), quarter())});
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track)
                  ->stave(fx.stave)
                  ->voice(kVoice1)
                  .normalize(fx.node()->timeline()->node_end())
                  .ok());
  const TrackLane before = *fx.node()->lane(fx.track);

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());
  EXPECT_TRUE(*fx.node()->lane(fx.track) == before);

  ASSERT_TRUE(cmd.undo(fx.project).ok());
  EXPECT_TRUE(*fx.node()->lane(fx.track) == before);

  ASSERT_TRUE(cmd.redo(fx.project).ok());
  EXPECT_TRUE(*fx.node()->lane(fx.track) == before);
}

TEST(DuplicateNodesCommandTest,
     MultiNodeSelectionDuplicatesEachExactlyOnceRoundTrip) {
  Project      project = make_project();
  const NodeId a_id    = project.add_node("A");
  const NodeId b_id    = project.add_node("B");

  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{a_id}, NodeItem{b_id}}));
  ASSERT_TRUE(cmd.execute(project).ok());
  EXPECT_EQ(project.nodes().size(), 4u);

  ASSERT_TRUE(cmd.undo(project).ok());
  EXPECT_EQ(project.nodes().size(), 2u);
  EXPECT_NE(project.find_node(a_id), nullptr);
  EXPECT_NE(project.find_node(b_id), nullptr);

  ASSERT_TRUE(cmd.redo(project).ok());
  EXPECT_EQ(project.nodes().size(), 4u);
}

// ============================================================
// N16 -- state-machine guards
// ============================================================

TEST(DuplicateNodesCommandTest, DoubleExecuteRejected) {
  Fixture               fx;
  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());
  EXPECT_EQ(cmd.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fx.project.nodes().size(), 2u);
}

TEST(DuplicateNodesCommandTest, UndoWithoutExecuteRejected) {
  Fixture               fx;
  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  EXPECT_EQ(cmd.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fx.project.nodes().size(), 1u);
}

TEST(DuplicateNodesCommandTest, RedoWithoutUndoRejected) {
  Fixture               fx;
  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());
  EXPECT_EQ(cmd.redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fx.project.nodes().size(), 2u);
}

TEST(DuplicateNodesCommandTest, DoubleUndoRejected) {
  Fixture               fx;
  DuplicateNodesCommand cmd(*NodeSet::create({NodeItem{fx.node_id}}));
  ASSERT_TRUE(cmd.execute(fx.project).ok());
  ASSERT_TRUE(cmd.undo(fx.project).ok());
  EXPECT_EQ(cmd.undo(fx.project).code(), ResultCode::kInvalidArgument);
}
