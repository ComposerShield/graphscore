// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ArbitraryRangeItem;
using graphscore::ArbitraryRangeSet;
using graphscore::Articulation;
using graphscore::ChordItem;
using graphscore::ChordNote;
using graphscore::ChordSet;
using graphscore::Clef;
using graphscore::ConnectorId;
using graphscore::ConnectorItem;
using graphscore::ConnectorSet;
using graphscore::ConnectorType;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_id;
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
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::MarkingItem;
using graphscore::MarkingKind;
using graphscore::MarkingSet;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::MusicalSpan;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeItem;
using graphscore::NodeSet;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteheadItem;
using graphscore::NoteheadSet;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::RestItem;
using graphscore::RestSet;
using graphscore::Selection;
using graphscore::SelectionDiagnostic;
using graphscore::SelectionDiagnosticCode;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::StaveVoices;
using graphscore::TimeSignature;
using graphscore::Track;
using graphscore::TrackId;
using graphscore::TrackLane;
using graphscore::TupletRatio;
using graphscore::validate_selection;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

namespace {

// ---- helpers ----

SpelledPitch pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Rational rat(std::int64_t num, std::int64_t den) {
  return *Rational::create(num, den);
}

// Builds a minimal project with one active track (single staff), one
// node with a timeline (one 4/4 measure, no pickdown), an empty lane,
// and returns the ids for reuse in tests.
struct Fixture {
  Project          project{ProjectId::generate(), "Test"};
  TrackId          track_id;
  StaveId          stave_id;
  NodeId           node_id;
  const Track*     track;
  const Node*      node;
  const TrackLane* lane;

  Fixture() {
    const auto tid = project.add_track("Track", StaffLayout::single_staff(),
                                       *MidiChannel::create(0));
    assert(tid.has_value());
    track_id = *tid;
    track    = project.find_active_track(track_id);
    assert(track != nullptr);

    stave_id = track->layout().staves()[0].id;

    const auto nid = project.add_node("Node A");
    node_id        = nid;
    node           = project.find_node(node_id);
    assert(node != nullptr);

    auto measures = std::vector<Measure>{
        {*TimeSignature::create(4, 4), *KeySignature::create(0)},
    };
    auto timeline = NodeTimeline::create(
        measures, {StaveDefinition{stave_id, graphscore::Clef::kTreble}});
    assert(timeline.has_value());
    const_cast<Node*>(node)->set_timeline(std::move(*timeline));

    lane = node->lane(track_id);
    assert(lane != nullptr);
    const_cast<TrackLane*>(lane)->ensure_stave(stave_id);
  }

  // Appends a quarter-note Note to (stave, voice), returns its entity id.
  NotationEntityId add_note(Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    VoiceEvent       e   = make_note(pitch(Letter::kC), quarter());
    NotationEntityId eid = event_id(e);
    (void)sv->voice(voice_num).append(std::move(e));
    return eid;
  }

  // Appends a quarter-note Chord (2 notes) to (stave, voice), returns
  // the chord's top-level entity id and the first ChordNote's id.
  struct ChordIds {
    NotationEntityId chord_id;
    NotationEntityId note_id;
  };

  ChordIds add_chord(Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    auto c = make_chord(
        quarter(),
        {ChordNote{NotationEntityId::generate(), pitch(Letter::kC)},
         ChordNote{NotationEntityId::generate(), pitch(Letter::kE)}});
    NotationEntityId cid = c.id;
    NotationEntityId nid = c.notes[0].id;
    (void)sv->voice(voice_num).append(std::move(c));
    return {cid, nid};
  }

  // Appends a grace group with one GraceNote, principal = `principal_id`,
  // returns the GraceNote's id.
  NotationEntityId add_grace_note(NotationEntityId principal_id,
                                  Voice            voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    auto gn = GraceNote{NotationEntityId::generate(), pitch(Letter::kD),
                        quarter(), GraceNoteType::kAppoggiatura, false};
    NotationEntityId gn_id = gn.id;
    auto             group = make_grace_group(principal_id, {gn});
    (void)sv->voice(voice_num).add_grace_group(std::move(group));
    return gn_id;
  }

  // Appends a quarter-note Rest to (stave, voice), returns its entity id.
  NotationEntityId add_rest(Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    VoiceEvent       e   = make_rest(quarter());
    NotationEntityId eid = event_id(e);
    (void)sv->voice(voice_num).append(std::move(e));
    return eid;
  }

  // Appends a quarter-note Note carrying `articulations`, returns its id.
  NotationEntityId add_articulated_note(std::vector<Articulation> articulations,
                                        Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    VoiceEvent       e   = make_note(pitch(Letter::kC), quarter(), false,
                                     std::move(articulations));
    NotationEntityId eid = event_id(e);
    (void)sv->voice(voice_num).append(std::move(e));
    return eid;
  }

  // Appends two same-pitch quarter notes, the first tied into the second;
  // returns the tie origin's id.
  NotationEntityId add_tied_note_pair(Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    VoiceEvent first =
        make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true);
    VoiceEvent       second = make_note(pitch(Letter::kC), quarter());
    NotationEntityId eid    = event_id(first);
    (void)sv->voice(voice_num).append(std::move(first));
    (void)sv->voice(voice_num).append(std::move(second));
    return eid;
  }

  // Appends two identical chords, the first chord's lowest notehead tied
  // into the second; returns that tied ChordNote's id.
  NotationEntityId add_tied_chord_pair(Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    auto first = make_chord(
        quarter(),
        {ChordNote{NotationEntityId::generate(), pitch(Letter::kC), true},
         ChordNote{NotationEntityId::generate(), pitch(Letter::kE)}});
    NotationEntityId       nid = first.notes[0].id;
    std::vector<ChordNote> continuation{
        ChordNote{NotationEntityId::generate(), pitch(Letter::kC)},
        ChordNote{NotationEntityId::generate(), pitch(Letter::kE)}};
    auto second = make_chord(quarter(), std::move(continuation));
    (void)sv->voice(voice_num).append(std::move(first));
    (void)sv->voice(voice_num).append(std::move(second));
    return nid;
  }

  // Appends a three-eighth 3:2 tuplet run, returns the ids in voice order.
  std::vector<NotationEntityId> add_triplet_run(Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    const Duration eighth_triplet =
        *Duration::create(NoteValue::kEighth, 0, TupletRatio::create(3, 2));
    std::vector<NotationEntityId> ids;
    for (int i = 0; i < 3; ++i) {
      VoiceEvent e = make_note(pitch(Letter::kC), eighth_triplet);
      ids.push_back(event_id(e));
      (void)sv->voice(voice_num).append(std::move(e));
    }
    return ids;
  }

  NotationEntityId add_dynamic(NotationEntityId at_event,
                               Voice            voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    auto             marking = make_dynamic_marking(at_event, Dynamic::kMf);
    NotationEntityId id      = marking.id;
    (void)sv->voice(voice_num).add_dynamic(std::move(marking));
    return id;
  }

  NotationEntityId add_hairpin(NotationEntityId start, NotationEntityId end,
                               Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    auto marking = make_hairpin(start, end, HairpinDirection::kCrescendo);
    NotationEntityId id = marking.id;
    (void)sv->voice(voice_num).add_hairpin(std::move(marking));
    return id;
  }

  NotationEntityId add_slur(NotationEntityId start, NotationEntityId end,
                            Voice voice_num = Voice{}) {
    auto* sv = const_cast<StaveVoices*>(lane->stave(stave_id));
    assert(sv != nullptr);
    auto             marking = make_slur(start, end);
    NotationEntityId id      = marking.id;
    (void)sv->voice(voice_num).add_slur(std::move(marking));
    return id;
  }

  NotationEntityId add_pedal_span() {
    auto span           = make_pedal_span(Rational(0), *Rational::create(1, 2));
    NotationEntityId id = span.id;
    (void)const_cast<TrackLane*>(lane)->add_pedal_span(stave_id,
                                                       std::move(span));
    return id;
  }

  // Archives the track.
  void archive() { (void)project.archive_track(track_id); }

  // Returns a non-const pointer to stave voices for mutating.
  StaveVoices* mutable_sv() {
    return const_cast<StaveVoices*>(lane->stave(stave_id));
  }
};

// Returns true if `diags` contains a diagnostic with `code` for
// `item_index`.
bool has_diag(const std::vector<SelectionDiagnostic>& diags,
              SelectionDiagnosticCode code, std::size_t item_index) {
  for (const auto& d : diags) {
    if (d.code == code && d.item_index == item_index)
      return true;
  }
  return false;
}

// A shape-correct MarkingItem in `f`'s scope: voice engaged for every kind
// but kPedalSpan, articulation engaged only for kArticulation.
MarkingItem marking(const Fixture& f, MarkingKind kind, NotationEntityId anchor,
                    std::optional<Articulation> articulation = std::nullopt) {
  std::optional<Voice> voice;
  if (kind != MarkingKind::kPedalSpan)
    voice = Voice{};
  return MarkingItem{f.node_id, f.track_id, f.stave_id,  voice,
                     kind,      anchor,     articulation};
}

}  // namespace

// ============================================================
// Intrinsic structure — construction rejects empty & duplicate
// ============================================================

TEST(SelectionTest, NoteheadSetRejectsEmpty) {
  EXPECT_FALSE(NoteheadSet::create({}).has_value());
}

TEST(SelectionTest, NoteheadSetRejectsDuplicate) {
  NoteheadItem a{NodeId::generate(), TrackId::generate(), StaveId::generate(),
                 Voice{}, NotationEntityId::generate()};
  EXPECT_FALSE(NoteheadSet::create({a, a}).has_value());
}

TEST(SelectionTest, NoteheadSetAcceptsSingle) {
  NoteheadItem a{NodeId::generate(), TrackId::generate(), StaveId::generate(),
                 Voice{}, NotationEntityId::generate()};
  EXPECT_TRUE(NoteheadSet::create({a}).has_value());
}

TEST(SelectionTest, ChordSetRejectsEmpty) {
  EXPECT_FALSE(ChordSet::create({}).has_value());
}

TEST(SelectionTest, ChordSetRejectsDuplicate) {
  ChordItem a{NodeId::generate(), TrackId::generate(), StaveId::generate(),
              Voice{}, NotationEntityId::generate()};
  EXPECT_FALSE(ChordSet::create({a, a}).has_value());
}

TEST(SelectionTest, FullMeasureSetRejectsEmpty) {
  EXPECT_FALSE(FullMeasureSet::create({}).has_value());
}

TEST(SelectionTest, FullMeasureSetRejectsDuplicate) {
  FullMeasureItem a{NodeId::generate(), TrackId::generate(),
                    StaveId::generate(), 0};
  EXPECT_FALSE(FullMeasureSet::create({a, a}).has_value());
}

TEST(SelectionTest, FullMeasureSetAcceptsContiguousRangeAndRejectsEmptyRange) {
  const NodeId  node  = NodeId::generate();
  const TrackId track = TrackId::generate();
  const StaveId stave = StaveId::generate();
  EXPECT_TRUE(
      FullMeasureSet::create({FullMeasureItem{node, track, stave, 2, 3}})
          .has_value());
  EXPECT_FALSE(
      FullMeasureSet::create({FullMeasureItem{node, track, stave, 2, 0}})
          .has_value());
}

TEST(SelectionTest, ArbitraryRangeSetRejectsEmpty) {
  EXPECT_FALSE(ArbitraryRangeSet::create({}).has_value());
}

TEST(SelectionTest, ArbitraryRangeSetRejectsDuplicate) {
  ArbitraryRangeItem a{NodeId::generate(), TrackId::generate(),
                       StaveId::generate(), Voice{},
                       MusicalSpan{Rational(0), Rational(1)}};
  EXPECT_FALSE(ArbitraryRangeSet::create({a, a}).has_value());
}

TEST(SelectionTest, ArbitraryRangeSetRejectsInvertedSpan) {
  ArbitraryRangeItem a{NodeId::generate(), TrackId::generate(),
                       StaveId::generate(), Voice{},
                       MusicalSpan{Rational(2), Rational(1)}};
  EXPECT_FALSE(ArbitraryRangeSet::create({a}).has_value());
}

TEST(SelectionTest, ArbitraryRangeSetAcceptsValidSpan) {
  ArbitraryRangeItem a{NodeId::generate(), TrackId::generate(),
                       StaveId::generate(), Voice{},
                       MusicalSpan{Rational(1), Rational(2)}};
  EXPECT_TRUE(ArbitraryRangeSet::create({a}).has_value());
}

TEST(SelectionTest, NodeSetRejectsEmpty) {
  EXPECT_FALSE(NodeSet::create({}).has_value());
}

TEST(SelectionTest, NodeSetRejectsDuplicate) {
  NodeItem a{NodeId::generate()};
  EXPECT_FALSE(NodeSet::create({a, a}).has_value());
}

TEST(SelectionTest, ConnectorSetRejectsEmpty) {
  EXPECT_FALSE(ConnectorSet::create({}).has_value());
}

TEST(SelectionTest, ConnectorSetRejectsDuplicate) {
  ConnectorItem a{NodeId::generate(), ConnectorId::generate()};
  EXPECT_FALSE(ConnectorSet::create({a, a}).has_value());
}

TEST(SelectionTest, InsertionCaretSetRejectsEmpty) {
  EXPECT_FALSE(InsertionCaretSet::create({}).has_value());
}

TEST(SelectionTest, InsertionCaretSetRejectsDuplicate) {
  InsertionCaretItem a{NodeId::generate(), TrackId::generate(),
                       StaveId::generate(), Voice{}, Rational(0)};
  EXPECT_FALSE(InsertionCaretSet::create({a, a}).has_value());
}

// ============================================================
// Multi-item / multi-scope acceptance
// ============================================================

TEST(SelectionTest, NoteheadSetAcceptsMultiItem) {
  NoteheadItem a{NodeId::generate(), TrackId::generate(), StaveId::generate(),
                 Voice{}, NotationEntityId::generate()};
  NoteheadItem b{NodeId::generate(), TrackId::generate(), StaveId::generate(),
                 *Voice::create(2), NotationEntityId::generate()};
  auto         s = NoteheadSet::create({a, b});
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->items().size(), 2u);
}

TEST(SelectionTest, NodeSetAcceptsMultiNode) {
  NodeItem a{NodeId::generate()};
  NodeItem b{NodeId::generate()};
  auto     s = NodeSet::create({a, b});
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s->items().size(), 2u);
}

// ============================================================
// Selection variant construction
// ============================================================

TEST(SelectionTest, VariantConstructionFromValidSet) {
  auto ns = NoteheadSet::create({NoteheadItem{
      NodeId::generate(), TrackId::generate(), StaveId::generate(), Voice{},
      NotationEntityId::generate()}});
  ASSERT_TRUE(ns.has_value());
  Selection sel{*ns};
  EXPECT_TRUE(std::holds_alternative<NoteheadSet>(sel));
}

// ============================================================
// validate_selection — notehead
// ============================================================

TEST(SelectionTest, ValidateNoteheadValidNote) {
  Fixture          f;
  NotationEntityId eid = f.add_note();
  auto             s   = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, eid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateNoteheadValidChordNote) {
  Fixture f;
  auto    ids = f.add_chord();
  auto    s   = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, ids.note_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateNoteheadValidGraceNote) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  NotationEntityId gn_id   = f.add_grace_note(note_id);
  auto             s       = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, gn_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateNoteheadRejectsRest) {
  Fixture f;
  // Fill the voice with a Rest by normalizing it.
  (void)f.mutable_sv()->voice(Voice{}).normalize(Rational(1));
  const auto& vc = f.lane->stave(f.stave_id)->voice(Voice{});
  ASSERT_FALSE(vc.events().empty());
  ASSERT_TRUE(std::holds_alternative<graphscore::Rest>(vc.events()[0]));
  NotationEntityId rest_id = event_id(vc.events()[0]);

  auto s = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 0));
}

TEST(SelectionTest, ValidateNoteheadRejectsTopLevelChord) {
  Fixture f;
  auto    ids = f.add_chord();
  auto    s   = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, ids.chord_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 0));
}

TEST(SelectionTest, ValidateNoteheadNodeNotFound) {
  Fixture f;
  auto    s = NoteheadSet::create(
      {NoteheadItem{NodeId::generate(), f.track_id, f.stave_id, Voice{},
                    NotationEntityId::generate()}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kNodeNotFound, 0));
}

TEST(SelectionTest, ValidateNoteheadTrackNotFound) {
  Fixture          f;
  NotationEntityId eid = f.add_note();
  auto             s   = NoteheadSet::create(
      {NoteheadItem{f.node_id, TrackId::generate(), f.stave_id, Voice{}, eid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kTrackNotFound, 0));
}

TEST(SelectionTest, ValidateNoteheadTrackArchived) {
  Fixture          f;
  NotationEntityId eid = f.add_note();
  f.archive();
  auto s = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, eid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kTrackArchived, 0));
}

TEST(SelectionTest, ValidateNoteheadEntityNotFound) {
  Fixture f;
  auto    s = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{},
                    NotationEntityId::generate()}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kEntityNotFound, 0));
}

TEST(SelectionTest, ValidateNoteheadStaveNotFound) {
  Fixture          f;
  NotationEntityId eid = f.add_note();
  auto             s   = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, StaveId::generate(), Voice{}, eid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kStaveNotFound, 0));
}

// ============================================================
// validate_selection — chord
// ============================================================

TEST(SelectionTest, ValidateChordValidTopLevel) {
  Fixture f;
  auto    ids = f.add_chord();
  auto    s   = ChordSet::create(
      {ChordItem{f.node_id, f.track_id, f.stave_id, Voice{}, ids.chord_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateChordRejectsNote) {
  Fixture          f;
  NotationEntityId eid = f.add_note();
  auto             s   = ChordSet::create(
      {ChordItem{f.node_id, f.track_id, f.stave_id, Voice{}, eid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 0));
}

TEST(SelectionTest, ValidateChordRejectsChordNote) {
  Fixture f;
  auto    ids = f.add_chord();
  auto    s   = ChordSet::create(
      {ChordItem{f.node_id, f.track_id, f.stave_id, Voice{}, ids.note_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 0));
}

// ============================================================
// validate_selection — full measure
// ============================================================

TEST(SelectionTest, ValidateFullMeasureValid) {
  Fixture f;
  auto    s = FullMeasureSet::create(
      {FullMeasureItem{f.node_id, f.track_id, f.stave_id, 0}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateFullMeasureIndexOutOfRange) {
  Fixture f;
  auto    s = FullMeasureSet::create(
      {FullMeasureItem{f.node_id, f.track_id, f.stave_id, 999}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(
      has_diag(diags, SelectionDiagnosticCode::kMeasureIndexOutOfRange, 0));
}

TEST(SelectionTest, ValidateFullMeasureRangeEndOutOfRange) {
  Fixture f;
  auto    set = FullMeasureSet::create(
      {FullMeasureItem{f.node_id, f.track_id, f.stave_id, 0, 2}});
  ASSERT_TRUE(set.has_value());
  Selection  selection{*set};
  const auto diagnostics = validate_selection(f.project, selection);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            SelectionDiagnosticCode::kMeasureIndexOutOfRange);
}

TEST(SelectionTest, ValidateFullMeasureNoTimeline) {
  Project p{ProjectId::generate(), "No TL"};
  auto    tid =
      p.add_track("T", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  StaveId sid = p.find_active_track(*tid)->layout().staves()[0].id;
  NodeId  nid = p.add_node("N");
  const_cast<TrackLane*>(p.find_node(nid)->lane(*tid))->ensure_stave(sid);
  auto s = FullMeasureSet::create({FullMeasureItem{nid, *tid, sid, 0}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(p, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].item_index, 0u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kNoTimeline);
  EXPECT_EQ(diags[0].message, "node has no timeline");
}

// ============================================================
// validate_selection — arbitrary range
// ============================================================

TEST(SelectionTest, ValidateRangeValidMainRegion) {
  Fixture f;
  auto    s = ArbitraryRangeSet::create(
      {ArbitraryRangeItem{f.node_id, f.track_id, f.stave_id, Voice{},
                          MusicalSpan{Rational(0), Rational(1)}}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateRangeValidSinglePoint) {
  Fixture f;
  auto    s = ArbitraryRangeSet::create(
      {ArbitraryRangeItem{f.node_id, f.track_id, f.stave_id, Voice{},
                          MusicalSpan{Rational(0), Rational(0)}}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  // start == end is a valid degenerate span.
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateRangeNoTimeline) {
  Project p{ProjectId::generate(), "No TL"};
  auto    tid =
      p.add_track("T", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  StaveId sid = p.find_active_track(*tid)->layout().staves()[0].id;
  NodeId  nid = p.add_node("N");
  const_cast<TrackLane*>(p.find_node(nid)->lane(*tid))->ensure_stave(sid);
  auto s = ArbitraryRangeSet::create({ArbitraryRangeItem{
      nid, *tid, sid, Voice{}, MusicalSpan{Rational(0), Rational(1)}}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(p, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kNoTimeline);
}

TEST(SelectionTest, ValidateRangeInPickdown) {
  Project p{ProjectId::generate(), "Pickdown"};
  auto    tid =
      p.add_track("T", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  StaveId sid = p.find_active_track(*tid)->layout().staves()[0].id;
  NodeId  nid = p.add_node("N");

  auto measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto timeline =
      NodeTimeline::create(measures, {StaveDefinition{sid, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  // Set a pickdown of 1 quarter note.
  ASSERT_TRUE(timeline->set_pickdown(rat(1, 4)).ok());
  const_cast<Node*>(p.find_node(nid))->set_timeline(std::move(*timeline));
  const_cast<TrackLane*>(p.find_node(nid)->lane(*tid))->ensure_stave(sid);

  // Span entirely in pickdown: main region is [0, 1), pickdown is
  // [1, 5/4).
  auto s = ArbitraryRangeSet::create({ArbitraryRangeItem{
      nid, *tid, sid, Voice{}, MusicalSpan{rat(1, 1), rat(5, 4)}}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(p, sel).empty());
}

TEST(SelectionTest, ValidateRangeCrossingBoundary) {
  Project p{ProjectId::generate(), "Crossing"};
  auto    tid =
      p.add_track("T", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  StaveId sid = p.find_active_track(*tid)->layout().staves()[0].id;
  NodeId  nid = p.add_node("N");

  auto measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto timeline =
      NodeTimeline::create(measures, {StaveDefinition{sid, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(rat(1, 4)).ok());
  const_cast<Node*>(p.find_node(nid))->set_timeline(std::move(*timeline));
  const_cast<TrackLane*>(p.find_node(nid)->lane(*tid))->ensure_stave(sid);

  // Span straddling main/pickdown boundary at Rational(1).
  auto s = ArbitraryRangeSet::create({ArbitraryRangeItem{
      nid, *tid, sid, Voice{}, MusicalSpan{rat(3, 4), rat(5, 4)}}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(p, sel).empty());
}

// ============================================================
// validate_selection — node
// ============================================================

TEST(SelectionTest, ValidateNodeValid) {
  Fixture f;
  auto    s = NodeSet::create({NodeItem{f.node_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateNodeNotFound) {
  Fixture f;
  auto    s = NodeSet::create({NodeItem{NodeId::generate()}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kNodeNotFound, 0));
}

// ============================================================
// validate_selection — connector
// ============================================================

TEST(SelectionTest, ValidateConnectorValidInput) {
  Fixture f;
  auto    cid = const_cast<Node*>(f.node)->add_input("in");
  auto    s   = ConnectorSet::create({ConnectorItem{f.node_id, cid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateConnectorValidOutput) {
  Fixture f;
  auto    cid = const_cast<Node*>(f.node)->add_output("out");
  auto    s   = ConnectorSet::create({ConnectorItem{f.node_id, cid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateConnectorNotFound) {
  Fixture f;
  auto    s =
      ConnectorSet::create({ConnectorItem{f.node_id, ConnectorId::generate()}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kConnectorNotFound, 0));
}

TEST(SelectionTest, ValidateConnectorNodeNotFound) {
  Fixture f;
  auto    s = ConnectorSet::create(
      {ConnectorItem{NodeId::generate(), ConnectorId::generate()}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kNodeNotFound, 0));
}

TEST(SelectionTest, ValidateConnectorOwnershipMismatch) {
  // A connector owned by node B is selected under node A.
  // Validation is node-local: the supplied node exists but does not
  // own the connector → kConnectorNotFound.
  Fixture f;
  NodeId  node_b = f.project.add_node("Node B");
  auto*   nb     = const_cast<Node*>(f.project.find_node(node_b));
  ASSERT_TRUE(nb != nullptr);
  ConnectorId cid = nb->add_input("in_on_b");

  auto s = ConnectorSet::create({ConnectorItem{f.node_id, cid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kConnectorNotFound);
  EXPECT_EQ(diags[0].item_index, 0u);
  EXPECT_EQ(diags[0].message, "connector not found on node");
}

// ============================================================
// validate_selection — insertion caret
// ============================================================

TEST(SelectionTest, ValidateCaretPositionZero) {
  Fixture f;
  f.add_note();
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, Rational(0)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateCaretAtEventBoundary) {
  Fixture f;
  f.add_note();  // quarter at position 0
  f.add_note();  // quarter at position 1/4
  // Event boundary at 1/4 is valid.
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, rat(1, 4)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateCaretAtTotalLength) {
  Fixture f;
  f.add_note();  // quarter at pos 0 → voice total = 1/4; lane total = 1/4
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, rat(1, 4)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateCaretAtLaneExtentBeyondVoiceLength) {
  // Selected voice has 1 quarter; another voice extends further.
  Fixture f;
  f.add_note(Voice{});            // voice 1: 1/4
  f.add_note(*Voice::create(2));  // voice 2: 1/4
  f.add_note(*Voice::create(2));  // voice 2: 2/4
  // Voice 1: total = 1/4. Lane total = max(1/4, 2/4) = 2/4.
  // Caret at lane extent 2/4 is valid even though voice 1's total is 1/4.
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, rat(1, 2)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateCaretNonBoundary) {
  Fixture f;
  f.add_note();  // quarter at pos 0 → voice total = 1/4
  // Position 1/8 is not an event boundary.
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, rat(1, 8)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(
      has_diag(diags, SelectionDiagnosticCode::kCaretPositionNotBoundary, 0));
}

TEST(SelectionTest, ValidateCaretEmptyVoicePositionZero) {
  Fixture f;
  // Empty voice: position 0 is valid.
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, Rational(0)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateCaretPositionZeroAlwaysValid) {
  // Voice 1 is empty; Voice 2 has notes giving lane extent > 0.
  // Position 0 must still be valid (spec: "position 0 is always valid").
  Fixture f;
  f.add_note(*Voice::create(2));
  f.add_note(*Voice::create(2));  // lane total_length = 2/4
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, Rational(0)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

// ============================================================
// Multi-item validation
// ============================================================

TEST(SelectionTest, MultiItemValidationMixedResults) {
  Fixture          f;
  NotationEntityId valid_eid = f.add_note();
  NotationEntityId bogus_eid = NotationEntityId::generate();

  auto s = NoteheadSet::create({
      NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, valid_eid},
      NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, bogus_eid},
  });
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].item_index, 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kEntityNotFound);
}

// ============================================================
// position_of_event
// ============================================================

TEST(SelectionTest, PositionOfEventTopLevelNote) {
  Fixture          f;
  NotationEntityId eid = f.add_note();
  const auto&      vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto             pos = vc.position_of_event(eid);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, Rational(0));
}

TEST(SelectionTest, PositionOfEventTopLevelChord) {
  Fixture     f;
  auto        ids = f.add_chord();
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(ids.chord_id);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, Rational(0));
}

TEST(SelectionTest, PositionOfEventChordNote) {
  Fixture     f;
  auto        ids = f.add_chord();
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(ids.note_id);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, Rational(0));
}

TEST(SelectionTest, PositionOfEventGraceNote) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  NotationEntityId gn_id   = f.add_grace_note(note_id);
  const auto&      vc      = f.lane->stave(f.stave_id)->voice(Voice{});
  auto             pos     = vc.position_of_event(gn_id);
  ASSERT_TRUE(pos.has_value());
  // The grace note's principal is the note at position 0.
  EXPECT_EQ(*pos, Rational(0));
}

TEST(SelectionTest, PositionOfEventAbsentId) {
  Fixture f;
  f.add_note();
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(NotationEntityId::generate());
  EXPECT_FALSE(pos.has_value());
}

TEST(SelectionTest, PositionOfEventSecondEvent) {
  Fixture f;
  f.add_note();                          // quarter at 0
  NotationEntityId eid2 = f.add_note();  // quarter at 1/4
  const auto&      vc   = f.lane->stave(f.stave_id)->voice(Voice{});
  auto             pos  = vc.position_of_event(eid2);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, rat(1, 4));
}

TEST(SelectionTest, PositionOfEventMarkingIdReturnsNullopt) {
  Fixture          f;
  NotationEntityId eid = f.add_note();
  // Add a dynamic marking — its id.  position_of_event marks that
  // markings (non-event entities) return nullopt.
  auto dm = graphscore::make_dynamic_marking(eid, graphscore::Dynamic::kMf);
  NotationEntityId marking_id = dm.id;
  (void)f.mutable_sv()->voice(Voice{}).add_dynamic(std::move(dm));
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(marking_id);
  EXPECT_FALSE(pos.has_value());
}

TEST(SelectionTest, PositionOfEventChordNoteAtNonzeroPosition) {
  Fixture f;
  f.add_note();                     // quarter at 0
  auto        ids = f.add_chord();  // chord at 1/4
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(ids.note_id);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, rat(1, 4));
}

TEST(SelectionTest, PositionOfEventGraceNotePrincipalAtNonzero) {
  Fixture f;
  f.add_note();                           // quarter at 0
  NotationEntityId note2 = f.add_note();  // quarter at 1/4
  NotationEntityId gn_id = f.add_grace_note(note2);
  const auto&      vc    = f.lane->stave(f.stave_id)->voice(Voice{});
  auto             pos   = vc.position_of_event(gn_id);
  ASSERT_TRUE(pos.has_value());
  // Principal note2 is at position 1/4.
  EXPECT_EQ(*pos, rat(1, 4));
}

TEST(SelectionTest, PositionOfEventDanglingPrincipal) {
  // GraceNote whose principal_event does not exist in the voice.
  Fixture f;
  f.add_note();
  // Add a grace group with a bogus (generated, never added) principal.
  auto gn = GraceNote{NotationEntityId::generate(), pitch(Letter::kD),
                      quarter(), GraceNoteType::kAppoggiatura, false};
  NotationEntityId gn_id = gn.id;
  auto             group = make_grace_group(NotationEntityId::generate(), {gn});
  (void)f.mutable_sv()->voice(Voice{}).add_grace_group(std::move(group));
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(gn_id);
  EXPECT_FALSE(pos.has_value());
}

TEST(SelectionTest, PositionOfEventGraceToGraceReference) {
  // A GraceNote whose principal_event is another GraceNote's id.
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  NotationEntityId gn1_id  = f.add_grace_note(note_id);
  // Add a second grace group whose principal is gn1_id (grace-to-grace).
  auto gn2 = GraceNote{NotationEntityId::generate(), pitch(Letter::kE),
                       quarter(), GraceNoteType::kAcciaccatura, false};
  NotationEntityId gn2_id = gn2.id;
  auto             group  = make_grace_group(gn1_id, {gn2});
  (void)f.mutable_sv()->voice(Voice{}).add_grace_group(std::move(group));
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(gn2_id);
  ASSERT_TRUE(pos.has_value());
  // The principal gn1_id resolves through note_id at position 0.
  EXPECT_EQ(*pos, Rational(0));
}

TEST(SelectionTest, PositionOfEventCyclicGraceReference) {
  // GraceNote whose principal_event points to itself (cycle).
  Fixture f;
  f.add_note();
  auto gn = GraceNote{NotationEntityId::generate(), pitch(Letter::kD),
                      quarter(), GraceNoteType::kAppoggiatura, false};
  NotationEntityId gn_id = gn.id;
  auto             group = make_grace_group(gn_id, {gn});  // self-referential
  (void)f.mutable_sv()->voice(Voice{}).add_grace_group(std::move(group));
  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(gn_id);
  // Cycle detected → nullopt.
  EXPECT_FALSE(pos.has_value());
}

// ============================================================
// validate_selection — chord rejects Rest / GraceNote
// ============================================================

TEST(SelectionTest, ValidateChordRejectsRest) {
  Fixture f;
  (void)f.mutable_sv()->voice(Voice{}).normalize(Rational(1));
  const auto& vc = f.lane->stave(f.stave_id)->voice(Voice{});
  ASSERT_FALSE(vc.events().empty());
  ASSERT_TRUE(std::holds_alternative<graphscore::Rest>(vc.events()[0]));
  NotationEntityId rest_id = event_id(vc.events()[0]);
  auto             s       = ChordSet::create(
      {ChordItem{f.node_id, f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 0));
}

TEST(SelectionTest, ValidateChordRejectsGraceNote) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  NotationEntityId gn_id   = f.add_grace_note(note_id);
  auto             s       = ChordSet::create(
      {ChordItem{f.node_id, f.track_id, f.stave_id, Voice{}, gn_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 0));
}

// ============================================================
// Multi-track / multi-stave / multi-voice scoped sets
// ============================================================

TEST(SelectionTest, MultiScopeNoteheadAcrossVoices) {
  Fixture          f;
  NotationEntityId eid1 = f.add_note(Voice{});
  NotationEntityId eid2 = f.add_note(*Voice::create(2));
  auto             s    = NoteheadSet::create({
      NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, eid1},
      NoteheadItem{f.node_id, f.track_id, f.stave_id, *Voice::create(2), eid2},
  });
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, MultiScopeMeasureAcrossNodes) {
  Fixture f;
  NodeId  nid2 = f.project.add_node("Node 2");
  auto*   n2   = const_cast<Node*>(f.project.find_node(nid2));
  ASSERT_TRUE(n2 != nullptr);
  auto measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto tl = NodeTimeline::create(measures,
                                 {StaveDefinition{f.stave_id, Clef::kTreble}});
  ASSERT_TRUE(tl.has_value());
  n2->set_timeline(std::move(*tl));
  const_cast<TrackLane*>(n2->lane(f.track_id))->ensure_stave(f.stave_id);

  auto s = FullMeasureSet::create({
      FullMeasureItem{f.node_id, f.track_id, f.stave_id, 0},
      FullMeasureItem{nid2, f.track_id, f.stave_id, 0},
  });
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

// ============================================================
// Multi-track / multi-stave / scope-disambiguation NoteheadSet
// ============================================================

TEST(SelectionTest, NoteheadSetAcrossMultipleTracksAndStaves) {
  // Two tracks: Track A (single staff), Track B (grand staff = 2 staves).
  // One node aligned to both tracks.  Noteheads in all three staves.
  // A single NoteheadSet spanning distinct tracks and distinct staves
  // validates cleanly.
  Project p{ProjectId::generate(), "MultiTrack"};
  auto    ta = p.add_track("A", StaffLayout::single_staff(Clef::kTreble),
                           *MidiChannel::create(0));
  ASSERT_TRUE(ta.has_value());
  TrackId track_a_id = *ta;
  StaveId stave_a_id = p.find_active_track(track_a_id)->layout().staves()[0].id;

  auto tb =
      p.add_track("B", StaffLayout::grand_staff(), *MidiChannel::create(1));
  ASSERT_TRUE(tb.has_value());
  TrackId track_b_id = *tb;
  StaveId upper_b_id = p.find_active_track(track_b_id)->layout().staves()[0].id;
  StaveId lower_b_id = p.find_active_track(track_b_id)->layout().staves()[1].id;

  NodeId nid      = p.add_node("N");
  auto   measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto tl = NodeTimeline::create(measures,
                                 {StaveDefinition{stave_a_id, Clef::kTreble},
                                  StaveDefinition{upper_b_id, Clef::kTreble},
                                  StaveDefinition{lower_b_id, Clef::kBass}});
  ASSERT_TRUE(tl.has_value());
  auto* node = const_cast<Node*>(p.find_node(nid));
  node->set_timeline(std::move(*tl));
  const_cast<TrackLane*>(node->lane(track_a_id))->ensure_stave(stave_a_id);
  const_cast<TrackLane*>(node->lane(track_b_id))->ensure_stave(upper_b_id);
  const_cast<TrackLane*>(node->lane(track_b_id))->ensure_stave(lower_b_id);

  // Place noteheads: one per stave.
  VoiceEvent e_a = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  NotationEntityId eid_a = event_id(e_a);
  (void)node->lane(track_a_id)
      ->stave(stave_a_id)
      ->voice(Voice{})
      .append(std::move(e_a));

  VoiceEvent e_bu = make_note(*SpelledPitch::create(Letter::kD, 4), quarter());
  NotationEntityId eid_bu = event_id(e_bu);
  (void)node->lane(track_b_id)
      ->stave(upper_b_id)
      ->voice(Voice{})
      .append(std::move(e_bu));

  VoiceEvent e_bl = make_note(*SpelledPitch::create(Letter::kE, 3), quarter());
  NotationEntityId eid_bl = event_id(e_bl);
  (void)node->lane(track_b_id)
      ->stave(lower_b_id)
      ->voice(Voice{})
      .append(std::move(e_bl));

  // Selection spans all three distinct (TrackId, StaveId) scopes.
  auto s = NoteheadSet::create({
      NoteheadItem{nid, track_a_id, stave_a_id, Voice{}, eid_a},
      NoteheadItem{nid, track_b_id, upper_b_id, Voice{}, eid_bu},
      NoteheadItem{nid, track_b_id, lower_b_id, Voice{}, eid_bl},
  });
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(p, sel).empty());
}

TEST(SelectionTest, NoteheadTrackIdMismatchRejected) {
  // Notehead placed in track A; selected with track B's TrackId and
  // track A's StaveId.  track_has_stave determines that track B does
  // not own track A's StaveId → kStaveNotFound deterministically.
  Project p{ProjectId::generate(), "SwappedTrack"};
  auto    ta = p.add_track("A", StaffLayout::single_staff(Clef::kTreble),
                           *MidiChannel::create(0));
  ASSERT_TRUE(ta.has_value());
  TrackId track_a_id = *ta;
  StaveId stave_a_id = p.find_active_track(track_a_id)->layout().staves()[0].id;

  auto tb = p.add_track("B", StaffLayout::single_staff(Clef::kTreble),
                        *MidiChannel::create(1));
  ASSERT_TRUE(tb.has_value());
  TrackId track_b_id = *tb;
  // Track B has its own stave (different StaveId from track A's).

  NodeId nid      = p.add_node("N");
  auto   measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto tl = NodeTimeline::create(measures,
                                 {StaveDefinition{stave_a_id, Clef::kTreble}});
  ASSERT_TRUE(tl.has_value());
  auto* node = const_cast<Node*>(p.find_node(nid));
  node->set_timeline(std::move(*tl));
  const_cast<TrackLane*>(node->lane(track_a_id))->ensure_stave(stave_a_id);

  // Place the notehead in track A only.
  VoiceEvent e = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  NotationEntityId eid = event_id(e);
  (void)node->lane(track_a_id)
      ->stave(stave_a_id)
      ->voice(Voice{})
      .append(std::move(e));

  // Selection item uses track B (wrong track) with track A's StaveId.
  auto s = NoteheadSet::create(
      {NoteheadItem{nid, track_b_id, stave_a_id, Voice{}, eid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(p, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].item_index, 0u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kStaveNotFound);
  EXPECT_EQ(diags[0].message, "stave not in track layout");
}

TEST(SelectionTest, NoteheadStaveIdMismatchRejected) {
  // Notehead placed in grand-staff upper stave; selected with lower
  // stave's StaveId.  Same track, same node, wrong stave — the entity
  // was added to the other stave → kEntityNotFound deterministically.
  Project p{ProjectId::generate(), "SwappedStave"};
  auto    tb =
      p.add_track("Piano", StaffLayout::grand_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tb.has_value());
  TrackId track_id = *tb;
  StaveId upper_id = p.find_active_track(track_id)->layout().staves()[0].id;
  StaveId lower_id = p.find_active_track(track_id)->layout().staves()[1].id;

  NodeId nid      = p.add_node("N");
  auto   measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto tl =
      NodeTimeline::create(measures, {StaveDefinition{upper_id, Clef::kTreble},
                                      StaveDefinition{lower_id, Clef::kBass}});
  ASSERT_TRUE(tl.has_value());
  auto* node = const_cast<Node*>(p.find_node(nid));
  node->set_timeline(std::move(*tl));
  const_cast<TrackLane*>(node->lane(track_id))->ensure_stave(upper_id);
  const_cast<TrackLane*>(node->lane(track_id))->ensure_stave(lower_id);

  // Place notehead in upper stave only.
  VoiceEvent e = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  NotationEntityId eid = event_id(e);
  (void)node->lane(track_id)->stave(upper_id)->voice(Voice{}).append(
      std::move(e));

  // Selection item uses lower stave (wrong stave).
  auto s = NoteheadSet::create(
      {NoteheadItem{nid, track_id, lower_id, Voice{}, eid}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(p, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].item_index, 0u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kEntityNotFound);
  EXPECT_EQ(diags[0].message, "entity not found in voice");
}

TEST(SelectionTest, NoteheadIdenticalEntityIdAcrossVoices) {
  // VoiceContent id-uniqueness is voice-scoped: two different voices
  // on the same stave may each carry an event with the same
  // NotationEntityId.  A NoteheadSet distinguishes them by their
  // full (NodeId, TrackId, StaveId, Voice) scope, and validation
  // succeeds for both items independently.
  Fixture f;

  const NotationEntityId shared_id = NotationEntityId::generate();
  VoiceEvent             e1 =
      Note{shared_id, *SpelledPitch::create(Letter::kC, 4), quarter(), false,
           {},        graphscore::StemDirection::kAuto};
  VoiceEvent e2 =
      Note{shared_id, *SpelledPitch::create(Letter::kD, 4), quarter(), false,
           {},        graphscore::StemDirection::kAuto};

  auto* sv = const_cast<StaveVoices*>(f.lane->stave(f.stave_id));
  ASSERT_TRUE(sv->voice(Voice{}).append(std::move(e1)).ok());
  ASSERT_TRUE(sv->voice(*Voice::create(2)).append(std::move(e2)).ok());

  // Both items carry the same entity id but different Voice.
  auto s = NoteheadSet::create({
      NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, shared_id},
      NoteheadItem{f.node_id, f.track_id, f.stave_id, *Voice::create(2),
                   shared_id},
  });
  ASSERT_TRUE(s.has_value());
  ASSERT_EQ(s->items().size(), 2u);
  EXPECT_EQ(s->items()[0].entity, shared_id);
  EXPECT_EQ(s->items()[1].entity, shared_id);
  EXPECT_NE(s->items()[0].voice, s->items()[1].voice);

  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

// ============================================================
// Full measure — pickdown exclusion / ordinal bounds
// ============================================================

TEST(SelectionTest, ValidateFullMeasureRejectsPickdownOrdinal) {
  // measure_index_at returns nullopt for pickdown positions, so an
  // ordinal that would fall in pickdown is simply out of range.
  Project p{ProjectId::generate(), "Pickdown"};
  auto    tid =
      p.add_track("T", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  StaveId sid = p.find_active_track(*tid)->layout().staves()[0].id;
  NodeId  nid = p.add_node("N");

  auto measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto timeline =
      NodeTimeline::create(measures, {StaveDefinition{sid, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(rat(1, 4)).ok());
  const_cast<Node*>(p.find_node(nid))->set_timeline(std::move(*timeline));
  const_cast<TrackLane*>(p.find_node(nid)->lane(*tid))->ensure_stave(sid);

  // measure_count() is 1 (main region only); index 0 is valid main.
  auto s_valid = FullMeasureSet::create({FullMeasureItem{nid, *tid, sid, 0}});
  ASSERT_TRUE(s_valid.has_value());
  EXPECT_TRUE(validate_selection(p, *s_valid).empty());

  // Index 1 is out of range — pickdown does not have measure ordinals.
  auto s_oob = FullMeasureSet::create({FullMeasureItem{nid, *tid, sid, 1}});
  ASSERT_TRUE(s_oob.has_value());
  auto diags = validate_selection(p, *s_oob);
  EXPECT_TRUE(
      has_diag(diags, SelectionDiagnosticCode::kMeasureIndexOutOfRange, 0));
}

// ============================================================
// TrackLane::total_length
// ============================================================

TEST(SelectionTest, TrackLaneTotalLengthEmpty) {
  EXPECT_EQ(Fixture().lane->total_length(), Rational(0));
}

TEST(SelectionTest, TrackLaneTotalLengthMaxVoice) {
  Fixture f;
  f.add_note(Voice{});            // voice 1: 1/4
  f.add_note(*Voice::create(2));  // voice 2: 1/4
  f.add_note(*Voice::create(2));  // voice 2: 2/4
  EXPECT_EQ(f.lane->total_length(), rat(1, 2));
}

// ============================================================
// Deterministic exact diagnostic messages
// ============================================================

TEST(SelectionTest, CaretNonBoundaryExactDiagnostic) {
  Fixture f;
  f.add_note();
  auto s = InsertionCaretSet::create({InsertionCaretItem{
      f.node_id, f.track_id, f.stave_id, Voice{}, rat(1, 8)}});
  ASSERT_TRUE(s.has_value());
  auto diags = validate_selection(f.project, *s);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kCaretPositionNotBoundary);
  EXPECT_EQ(diags[0].item_index, 0u);
  EXPECT_EQ(diags[0].message,
            "caret position is not an event boundary or lane total_length()");
}

TEST(SelectionTest, NoTimelineExactDiagnostic) {
  Project p{ProjectId::generate(), "No TL"};
  auto    tid =
      p.add_track("T", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  StaveId sid = p.find_active_track(*tid)->layout().staves()[0].id;
  NodeId  nid = p.add_node("N");
  const_cast<TrackLane*>(p.find_node(nid)->lane(*tid))->ensure_stave(sid);
  auto s = ArbitraryRangeSet::create({ArbitraryRangeItem{
      nid, *tid, sid, Voice{}, MusicalSpan{Rational(0), Rational(1)}}});
  ASSERT_TRUE(s.has_value());
  auto diags = validate_selection(p, *s);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kNoTimeline);
  EXPECT_EQ(diags[0].item_index, 0u);
  EXPECT_EQ(diags[0].message,
            "node has no timeline, range classification unavailable");
}

// ============================================================
// position_of_event — long chains and multi-group cycles
// ============================================================

TEST(SelectionTest, PositionOfEventLongGraceChain) {
  // A chain of 10 GraceNote → GraceNote → … → Note.
  // Proves that there is no fixed depth cap.
  Fixture          f;
  NotationEntityId note_id = f.add_note();

  // Build a chain: gn10 → gn9 → … → gn1 → note_id.
  // Each add_grace_group succeeds (principal_event references a
  // well-formed NotationEntityId — another GraceNote).
  NotationEntityId chain_id = note_id;
  for (int i = 0; i < 10; ++i) {
    auto gn = GraceNote{NotationEntityId::generate(), pitch(Letter::kD),
                        quarter(), GraceNoteType::kAppoggiatura, false};
    NotationEntityId gn_id = gn.id;
    auto             group = make_grace_group(chain_id, {gn});
    // Each insertion must succeed (not rejected).
    auto result =
        f.mutable_sv()->voice(Voice{}).add_grace_group(std::move(group));
    ASSERT_TRUE(result.ok());
    chain_id = gn_id;
  }

  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(chain_id);
  ASSERT_TRUE(pos.has_value());
  // The chain terminates at note_id, position 0.
  EXPECT_EQ(*pos, Rational(0));
}

TEST(SelectionTest, PositionOfEventMultiGroupCycle) {
  // GraceGroup A → GraceGroup B → GraceGroup A (cycle across
  // groups).  Both insertions succeed; resolution returns nullopt.
  Fixture f;

  // Group A: gn_a with principal = gn_b.id.
  auto gn_b = GraceNote{NotationEntityId::generate(), pitch(Letter::kE),
                        quarter(), GraceNoteType::kAcciaccatura, false};
  NotationEntityId gn_b_id = gn_b.id;
  auto gn_a = GraceNote{NotationEntityId::generate(), pitch(Letter::kD),
                        quarter(), GraceNoteType::kAppoggiatura, false};
  NotationEntityId gn_a_id = gn_a.id;
  auto             group_a = make_grace_group(gn_b_id, {gn_a});
  ASSERT_TRUE(
      f.mutable_sv()->voice(Voice{}).add_grace_group(std::move(group_a)).ok());

  // Group B: gn_b with principal = gn_a.id (cycle back).
  auto group_b = make_grace_group(gn_a_id, {gn_b});
  ASSERT_TRUE(
      f.mutable_sv()->voice(Voice{}).add_grace_group(std::move(group_b)).ok());

  const auto& vc  = f.lane->stave(f.stave_id)->voice(Voice{});
  auto        pos = vc.position_of_event(gn_a_id);
  // Cycle A→B→A detected → nullopt.
  EXPECT_FALSE(pos.has_value());
}

// ============================================================
// Grand-staff TrackLane::total_length and caret
// ============================================================

TEST(SelectionTest, TrackLaneTotalLengthGrandStaff) {
  // Two staves: upper has 1/4, lower has 2/4 → lane max = 2/4.
  Project p{ProjectId::generate(), "GS"};
  auto    tid =
      p.add_track("Piano", StaffLayout::grand_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const Track* track = p.find_active_track(*tid);
  ASSERT_EQ(track->layout().stave_count(), 2u);
  StaveId upper_id = track->layout().staves()[0].id;
  StaveId lower_id = track->layout().staves()[1].id;

  NodeId nid      = p.add_node("N");
  auto   measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto tl =
      NodeTimeline::create(measures, {StaveDefinition{upper_id, Clef::kTreble},
                                      StaveDefinition{lower_id, Clef::kBass}});
  ASSERT_TRUE(tl.has_value());
  const_cast<Node*>(p.find_node(nid))->set_timeline(std::move(*tl));
  auto* lane = const_cast<TrackLane*>(p.find_node(nid)->lane(*tid));
  lane->ensure_stave(upper_id);
  lane->ensure_stave(lower_id);

  // Upper stave voice 1: one quarter note = 1/4.
  VoiceEvent ne = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  (void)lane->stave(upper_id)->voice(Voice{}).append(std::move(ne));

  // Lower stave voice 1: two quarter notes = 2/4.
  VoiceEvent ne2 = make_note(*SpelledPitch::create(Letter::kC, 3), quarter());
  VoiceEvent ne3 = make_note(*SpelledPitch::create(Letter::kD, 3), quarter());
  (void)lane->stave(lower_id)->voice(Voice{}).append(std::move(ne2));
  (void)lane->stave(lower_id)->voice(Voice{}).append(std::move(ne3));

  EXPECT_EQ(lane->total_length(), rat(1, 2));
}

TEST(SelectionTest, ValidateCaretGrandStaffLaneExtent) {
  // Upper stave has 1/4. Lower stave has 2/4. Lane extent = 2/4.
  // Select caret on upper stave at position 2/4 (lane extent).
  Project p{ProjectId::generate(), "GS2"};
  auto    tid =
      p.add_track("Piano", StaffLayout::grand_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const Track* track    = p.find_active_track(*tid);
  StaveId      upper_id = track->layout().staves()[0].id;
  StaveId      lower_id = track->layout().staves()[1].id;

  NodeId nid      = p.add_node("N");
  auto   measures = std::vector<Measure>{
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
  };
  auto tl =
      NodeTimeline::create(measures, {StaveDefinition{upper_id, Clef::kTreble},
                                      StaveDefinition{lower_id, Clef::kBass}});
  ASSERT_TRUE(tl.has_value());
  const_cast<Node*>(p.find_node(nid))->set_timeline(std::move(*tl));
  auto* lane = const_cast<TrackLane*>(p.find_node(nid)->lane(*tid));
  lane->ensure_stave(upper_id);
  lane->ensure_stave(lower_id);

  VoiceEvent ne = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  (void)lane->stave(upper_id)->voice(Voice{}).append(std::move(ne));
  VoiceEvent ne2 = make_note(*SpelledPitch::create(Letter::kC, 3), quarter());
  VoiceEvent ne3 = make_note(*SpelledPitch::create(Letter::kD, 3), quarter());
  (void)lane->stave(lower_id)->voice(Voice{}).append(std::move(ne2));
  (void)lane->stave(lower_id)->voice(Voice{}).append(std::move(ne3));

  // Caret on upper stave at lane extent 2/4 (beyond upper's 1/4).
  auto s = InsertionCaretSet::create(
      {InsertionCaretItem{nid, *tid, upper_id, Voice{}, rat(1, 2)}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(p, sel).empty());
}

// ============================================================
// Connector exact diagnostic
// ============================================================

TEST(SelectionTest, ConnectorNotFoundExactDiagnostic) {
  Fixture f;
  auto    s =
      ConnectorSet::create({ConnectorItem{f.node_id, ConnectorId::generate()}});
  ASSERT_TRUE(s.has_value());
  auto diags = validate_selection(f.project, *s);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kConnectorNotFound);
  EXPECT_EQ(diags[0].item_index, 0u);
  EXPECT_EQ(diags[0].message, "connector not found on node");
}

// ============================================================
// RestSet — intrinsic structure
// ============================================================

TEST(SelectionTest, RestSetRejectsEmpty) {
  EXPECT_FALSE(RestSet::create({}).has_value());
}

TEST(SelectionTest, RestSetRejectsDuplicate) {
  RestItem a{NodeId::generate(), TrackId::generate(), StaveId::generate(),
             Voice{}, NotationEntityId::generate()};
  EXPECT_FALSE(RestSet::create({a, a}).has_value());
}

TEST(SelectionTest, RestSetRoundTripsItems) {
  RestItem a{NodeId::generate(), TrackId::generate(), StaveId::generate(),
             Voice{}, NotationEntityId::generate()};
  RestItem b{a.node, a.track, a.stave, Voice{}, NotationEntityId::generate()};
  auto     s = RestSet::create({a, b});
  ASSERT_TRUE(s.has_value());
  ASSERT_EQ(s->items().size(), 2u);
  EXPECT_EQ(s->items()[0], a);
  EXPECT_EQ(s->items()[1], b);
  EXPECT_EQ(*s, *RestSet::create({a, b}));
}

TEST(SelectionTest, VariantConstructionFromRestSet) {
  auto s = RestSet::create(
      {RestItem{NodeId::generate(), TrackId::generate(), StaveId::generate(),
                Voice{}, NotationEntityId::generate()}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(std::holds_alternative<RestSet>(sel));
}

// ============================================================
// validate_selection — rest
// ============================================================

TEST(SelectionTest, ValidateRestValidRest) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto             s       = RestSet::create(
      {RestItem{f.node_id, f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateRestRejectsNote) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  auto             s       = RestSet::create(
      {RestItem{f.node_id, f.track_id, f.stave_id, Voice{}, note_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "entity is not a top-level rest");
}

TEST(SelectionTest, ValidateRestRejectsChordAndChordNote) {
  Fixture f;
  auto    ids = f.add_chord();
  auto    s   = RestSet::create(
      {RestItem{f.node_id, f.track_id, f.stave_id, Voice{}, ids.chord_id},
            RestItem{f.node_id, f.track_id, f.stave_id, Voice{}, ids.note_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 0));
  EXPECT_TRUE(has_diag(diags, SelectionDiagnosticCode::kWrongEntityKind, 1));
}

TEST(SelectionTest, ValidateRestRejectsGraceNote) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  NotationEntityId gn_id   = f.add_grace_note(note_id);
  auto             s       = RestSet::create(
      {RestItem{f.node_id, f.track_id, f.stave_id, Voice{}, gn_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kWrongEntityKind, 0));
}

TEST(SelectionTest, ValidateRestEntityNotFound) {
  Fixture f;
  (void)f.add_rest();
  auto s = RestSet::create({RestItem{f.node_id, f.track_id, f.stave_id, Voice{},
                                     NotationEntityId::generate()}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kEntityNotFound, 0));
}

TEST(SelectionTest, ValidateRestPropagatesScopeDiagnostics) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto             s       = RestSet::create(
      {RestItem{NodeId::generate(), f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kNodeNotFound, 0));
}

// A rest is still not a notehead and not a chord: the two existing arms
// are unchanged by the new one.
TEST(SelectionTest, ValidateNoteheadStillRejectsRestEntity) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto             s       = NoteheadSet::create(
      {NoteheadItem{f.node_id, f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "entity is not a notehead");
}

TEST(SelectionTest, ValidateChordStillRejectsRestEntity) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto             s       = ChordSet::create(
      {ChordItem{f.node_id, f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "entity is not a top-level chord");
}

// ============================================================
// MarkingSet — intrinsic structure
// ============================================================

TEST(SelectionTest, MarkingSetRejectsEmpty) {
  EXPECT_FALSE(MarkingSet::create({}).has_value());
}

TEST(SelectionTest, MarkingSetRejectsDuplicate) {
  Fixture           f;
  const MarkingItem item =
      marking(f, MarkingKind::kSlur, NotationEntityId::generate());
  EXPECT_FALSE(MarkingSet::create({item, item}).has_value());
}

TEST(SelectionTest, MarkingSetRoundTripsItems) {
  Fixture           f;
  const MarkingItem dynamic =
      marking(f, MarkingKind::kDynamic, NotationEntityId::generate());
  const MarkingItem pedal =
      marking(f, MarkingKind::kPedalSpan, NotationEntityId::generate());
  auto s = MarkingSet::create({dynamic, pedal});
  ASSERT_TRUE(s.has_value());
  ASSERT_EQ(s->items().size(), 2u);
  EXPECT_EQ(s->items()[0], dynamic);
  EXPECT_EQ(s->items()[1], pedal);
  EXPECT_FALSE(s->items()[1].voice.has_value());
  EXPECT_EQ(*s, *MarkingSet::create({dynamic, pedal}));
}

TEST(SelectionTest, VariantConstructionFromMarkingSet) {
  Fixture f;
  auto    s = MarkingSet::create(
      {marking(f, MarkingKind::kTie, NotationEntityId::generate())});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(std::holds_alternative<MarkingSet>(sel));
}

TEST(SelectionTest, MarkingSetRejectsArticulationWithoutDiscriminator) {
  Fixture     f;
  MarkingItem item =
      marking(f, MarkingKind::kArticulation, NotationEntityId::generate());
  ASSERT_FALSE(item.articulation.has_value());
  EXPECT_FALSE(MarkingSet::create({item}).has_value());
}

TEST(SelectionTest, MarkingSetAcceptsArticulationWithDiscriminator) {
  Fixture f;
  EXPECT_TRUE(MarkingSet::create({marking(f, MarkingKind::kArticulation,
                                          NotationEntityId::generate(),
                                          Articulation::kAccent)})
                  .has_value());
}

TEST(SelectionTest, MarkingSetRejectsArticulationOnOtherKinds) {
  Fixture f;
  for (const MarkingKind kind :
       {MarkingKind::kDynamic, MarkingKind::kHairpin, MarkingKind::kSlur,
        MarkingKind::kPedalSpan, MarkingKind::kTie, MarkingKind::kTuplet}) {
    MarkingItem item  = marking(f, kind, NotationEntityId::generate());
    item.articulation = Articulation::kStaccato;
    EXPECT_FALSE(MarkingSet::create({item}).has_value())
        << "kind " << static_cast<int>(kind);
  }
}

TEST(SelectionTest, MarkingSetRejectsPedalSpanWithVoice) {
  Fixture     f;
  MarkingItem item =
      marking(f, MarkingKind::kPedalSpan, NotationEntityId::generate());
  item.voice = Voice{};
  EXPECT_FALSE(MarkingSet::create({item}).has_value());
}

TEST(SelectionTest, MarkingSetRejectsVoicelessNonPedalKinds) {
  Fixture f;
  for (const MarkingKind kind :
       {MarkingKind::kDynamic, MarkingKind::kHairpin, MarkingKind::kSlur,
        MarkingKind::kTie, MarkingKind::kTuplet}) {
    MarkingItem item = marking(f, kind, NotationEntityId::generate());
    item.voice.reset();
    EXPECT_FALSE(MarkingSet::create({item}).has_value())
        << "kind " << static_cast<int>(kind);
  }
  MarkingItem articulation =
      marking(f, MarkingKind::kArticulation, NotationEntityId::generate(),
              Articulation::kTenuto);
  articulation.voice.reset();
  EXPECT_FALSE(MarkingSet::create({articulation}).has_value());
}

// ============================================================
// validate_selection — marking, each kind accepted when present
// ============================================================

TEST(SelectionTest, ValidateMarkingDynamic) {
  Fixture          f;
  NotationEntityId note_id    = f.add_note();
  NotationEntityId dynamic_id = f.add_dynamic(note_id);
  auto s = MarkingSet::create({marking(f, MarkingKind::kDynamic, dynamic_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateMarkingHairpin) {
  Fixture          f;
  NotationEntityId first      = f.add_note();
  NotationEntityId second     = f.add_note();
  NotationEntityId hairpin_id = f.add_hairpin(first, second);
  auto s = MarkingSet::create({marking(f, MarkingKind::kHairpin, hairpin_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateMarkingSlur) {
  Fixture          f;
  NotationEntityId first   = f.add_note();
  NotationEntityId second  = f.add_note();
  NotationEntityId slur_id = f.add_slur(first, second);
  auto s = MarkingSet::create({marking(f, MarkingKind::kSlur, slur_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateMarkingPedalSpan) {
  Fixture          f;
  NotationEntityId pedal_id = f.add_pedal_span();
  auto s = MarkingSet::create({marking(f, MarkingKind::kPedalSpan, pedal_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateMarkingArticulationOnNote) {
  Fixture          f;
  NotationEntityId note_id =
      f.add_articulated_note({Articulation::kAccent, Articulation::kStaccato});
  auto s = MarkingSet::create({marking(f, MarkingKind::kArticulation, note_id,
                                       Articulation::kStaccato)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateMarkingTieOnNote) {
  Fixture          f;
  NotationEntityId origin = f.add_tied_note_pair();
  auto s = MarkingSet::create({marking(f, MarkingKind::kTie, origin)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateMarkingTieOnChordNote) {
  Fixture          f;
  NotationEntityId origin = f.add_tied_chord_pair();
  auto s = MarkingSet::create({marking(f, MarkingKind::kTie, origin)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

TEST(SelectionTest, ValidateMarkingTupletOnRunStart) {
  Fixture    f;
  const auto ids = f.add_triplet_run();
  ASSERT_EQ(ids.size(), 3u);
  auto s = MarkingSet::create({marking(f, MarkingKind::kTuplet, ids[0])});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

// ============================================================
// validate_selection — marking not actually present
// ============================================================

TEST(SelectionTest, ValidateMarkingTieRejectsUntiedNote) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  auto s = MarkingSet::create({marking(f, MarkingKind::kTie, note_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kMarkingNotPresent);
  EXPECT_EQ(diags[0].message, "note is not tied to the following event");
}

TEST(SelectionTest, ValidateMarkingTieRejectsUntiedChordNote) {
  Fixture f;
  auto    ids = f.add_chord();
  auto    s = MarkingSet::create({marking(f, MarkingKind::kTie, ids.note_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kMarkingNotPresent, 0));
}

TEST(SelectionTest, ValidateMarkingArticulationRejectsAbsentArticulation) {
  Fixture          f;
  NotationEntityId note_id = f.add_articulated_note({Articulation::kAccent});
  auto             s       = MarkingSet::create(
      {marking(f, MarkingKind::kArticulation, note_id, Articulation::kTenuto)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kMarkingNotPresent);
  EXPECT_EQ(diags[0].message, "event does not carry that articulation");
}

TEST(SelectionTest, ValidateMarkingTupletRejectsPlainEvent) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  auto s = MarkingSet::create({marking(f, MarkingKind::kTuplet, note_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kMarkingNotPresent);
  EXPECT_EQ(diags[0].message, "event carries no tuplet");
}

TEST(SelectionTest, ValidateMarkingTupletRejectsMidRunEvent) {
  Fixture    f;
  const auto ids = f.add_triplet_run();
  ASSERT_EQ(ids.size(), 3u);
  auto s = MarkingSet::create({marking(f, MarkingKind::kTuplet, ids[1]),
                               marking(f, MarkingKind::kTuplet, ids[2])});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 2u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kMarkingNotPresent);
  EXPECT_EQ(diags[0].message, "event is not the first event of its tuplet run");
  EXPECT_EQ(diags[1].item_index, 1u);
  EXPECT_EQ(diags[1].code, SelectionDiagnosticCode::kMarkingNotPresent);
}

// A run whose ratio changes mid-way starts a new run, so the event where
// the new ratio begins is itself a valid tuplet anchor.
TEST(SelectionTest, ValidateMarkingTupletAcceptsSecondRunStart) {
  Fixture    f;
  const auto ids = f.add_triplet_run();
  ASSERT_EQ(ids.size(), 3u);
  auto*          sv = f.mutable_sv();
  const Duration quintuplet =
      *Duration::create(NoteValue::kEighth, 0, TupletRatio::create(5, 4));
  VoiceEvent       next    = make_note(pitch(Letter::kD), quintuplet);
  NotationEntityId next_id = event_id(next);
  ASSERT_TRUE(sv->voice(Voice{}).append(std::move(next)).ok());

  auto s = MarkingSet::create({marking(f, MarkingKind::kTuplet, next_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(validate_selection(f.project, sel).empty());
}

// ============================================================
// validate_selection — marking anchor mismatch and lookup failure
// ============================================================

TEST(SelectionTest, ValidateMarkingDynamicRejectsEventAnchor) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  auto s = MarkingSet::create({marking(f, MarkingKind::kDynamic, note_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "anchor is not a dynamic marking");
}

TEST(SelectionTest, ValidateMarkingHairpinRejectsSlurAnchor) {
  Fixture          f;
  NotationEntityId first   = f.add_note();
  NotationEntityId second  = f.add_note();
  NotationEntityId slur_id = f.add_slur(first, second);
  auto s = MarkingSet::create({marking(f, MarkingKind::kHairpin, slur_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "anchor is not a hairpin");
}

TEST(SelectionTest, ValidateMarkingSlurRejectsHairpinAnchor) {
  Fixture          f;
  NotationEntityId first      = f.add_note();
  NotationEntityId second     = f.add_note();
  NotationEntityId hairpin_id = f.add_hairpin(first, second);
  auto s = MarkingSet::create({marking(f, MarkingKind::kSlur, hairpin_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kWrongEntityKind, 0));
}

TEST(SelectionTest, ValidateMarkingTieRejectsRestAnchor) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto s = MarkingSet::create({marking(f, MarkingKind::kTie, rest_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "anchor is not a note or notehead");
}

TEST(SelectionTest, ValidateMarkingTieRejectsChordAnchor) {
  Fixture f;
  auto    ids = f.add_chord();
  auto    s = MarkingSet::create({marking(f, MarkingKind::kTie, ids.chord_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "anchor is not a note or notehead");
}

TEST(SelectionTest, ValidateMarkingArticulationRejectsRestAnchor) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto             s       = MarkingSet::create(
      {marking(f, MarkingKind::kArticulation, rest_id, Articulation::kAccent)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message,
            "anchor is not a top-level note or chord carrying articulations");
}

// Articulations belong to the chord column, not to one notehead, so a
// ChordNote id is not an articulation anchor.
TEST(SelectionTest, ValidateMarkingArticulationRejectsChordNoteAnchor) {
  Fixture           f;
  auto              ids  = f.add_chord();
  const MarkingItem item = marking(f, MarkingKind::kArticulation, ids.note_id,
                                   Articulation::kAccent);
  auto              s    = MarkingSet::create({item});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kWrongEntityKind, 0));
}

TEST(SelectionTest, ValidateMarkingTupletRejectsChordNoteAnchor) {
  Fixture f;
  auto    ids = f.add_chord();
  auto s = MarkingSet::create({marking(f, MarkingKind::kTuplet, ids.note_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "anchor is not a top-level event");
}

TEST(SelectionTest, ValidateMarkingPedalSpanRejectsEventAnchor) {
  Fixture          f;
  NotationEntityId note_id = f.add_note();
  (void)f.add_pedal_span();
  auto s = MarkingSet::create({marking(f, MarkingKind::kPedalSpan, note_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 1u);
  EXPECT_EQ(diags[0].code, SelectionDiagnosticCode::kWrongEntityKind);
  EXPECT_EQ(diags[0].message, "anchor is not a pedal span");
}

TEST(SelectionTest, ValidateMarkingUnknownAnchorNotFound) {
  Fixture f;
  (void)f.add_note();
  auto s = MarkingSet::create(
      {marking(f, MarkingKind::kDynamic, NotationEntityId::generate()),
       marking(f, MarkingKind::kHairpin, NotationEntityId::generate()),
       marking(f, MarkingKind::kSlur, NotationEntityId::generate()),
       marking(f, MarkingKind::kTie, NotationEntityId::generate()),
       marking(f, MarkingKind::kTuplet, NotationEntityId::generate()),
       marking(f, MarkingKind::kArticulation, NotationEntityId::generate(),
               Articulation::kAccent),
       marking(f, MarkingKind::kPedalSpan, NotationEntityId::generate())});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  auto      diags = validate_selection(f.project, sel);
  ASSERT_EQ(diags.size(), 7u);
  for (std::size_t i = 0; i < diags.size(); ++i) {
    EXPECT_EQ(diags[i].code, SelectionDiagnosticCode::kEntityNotFound)
        << "item " << i;
  }
}

// A marking in voice 1 is not addressable through voice 2.
TEST(SelectionTest, ValidateMarkingWrongVoiceNotFound) {
  Fixture          f;
  NotationEntityId note_id    = f.add_note();
  NotationEntityId dynamic_id = f.add_dynamic(note_id);
  MarkingItem      item       = marking(f, MarkingKind::kDynamic, dynamic_id);
  item.voice                  = *Voice::create(2);
  auto s                      = MarkingSet::create({item});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kEntityNotFound, 0));
}

TEST(SelectionTest, ValidateMarkingPropagatesScopeDiagnostics) {
  Fixture          f;
  NotationEntityId pedal_id = f.add_pedal_span();
  MarkingItem      item     = marking(f, MarkingKind::kPedalSpan, pedal_id);
  item.stave                = StaveId::generate();
  auto s                    = MarkingSet::create({item});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kStaveNotFound, 0));
}

TEST(SelectionTest, ValidateMarkingArchivedTrack) {
  Fixture          f;
  NotationEntityId note_id    = f.add_note();
  NotationEntityId dynamic_id = f.add_dynamic(note_id);
  f.archive();
  auto s = MarkingSet::create({marking(f, MarkingKind::kDynamic, dynamic_id)});
  ASSERT_TRUE(s.has_value());
  Selection sel{*s};
  EXPECT_TRUE(has_diag(validate_selection(f.project, sel),
                       SelectionDiagnosticCode::kTrackArchived, 0));
}

// ============================================================
// Clipboard consumers reject the new arms loudly
// ============================================================

TEST(SelectionTest, ExtractFragmentRejectsRestSelection) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto             s       = RestSet::create(
      {RestItem{f.node_id, f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  const Selection sel{*s};
  ASSERT_TRUE(validate_selection(f.project, sel).empty());
  const auto extraction = graphscore::extract_fragment(f.project, sel);
  EXPECT_EQ(extraction.status.code(), graphscore::ResultCode::kInvalidArgument);
  EXPECT_FALSE(extraction.fragment.has_value());
}

TEST(SelectionTest, ExtractFragmentRejectsMarkingSelection) {
  Fixture          f;
  NotationEntityId note_id    = f.add_note();
  NotationEntityId dynamic_id = f.add_dynamic(note_id);
  auto s = MarkingSet::create({marking(f, MarkingKind::kDynamic, dynamic_id)});
  ASSERT_TRUE(s.has_value());
  const Selection sel{*s};
  ASSERT_TRUE(validate_selection(f.project, sel).empty());
  const auto extraction = graphscore::extract_fragment(f.project, sel);
  EXPECT_EQ(extraction.status.code(), graphscore::ResultCode::kInvalidArgument);
  EXPECT_FALSE(extraction.fragment.has_value());
}

TEST(SelectionTest, CutFragmentCommandRejectsRestSelection) {
  Fixture          f;
  NotationEntityId rest_id = f.add_rest();
  auto             s       = RestSet::create(
      {RestItem{f.node_id, f.track_id, f.stave_id, Voice{}, rest_id}});
  ASSERT_TRUE(s.has_value());
  graphscore::CutFragmentCommand command{Selection{*s}};
  EXPECT_EQ(command.execute(f.project).code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_FALSE(command.fragment().has_value());
}

TEST(SelectionTest, CutFragmentCommandRejectsMarkingSelection) {
  Fixture          f;
  NotationEntityId note_id    = f.add_note();
  NotationEntityId dynamic_id = f.add_dynamic(note_id);
  auto s = MarkingSet::create({marking(f, MarkingKind::kDynamic, dynamic_id)});
  ASSERT_TRUE(s.has_value());
  graphscore::CutFragmentCommand command{Selection{*s}};
  EXPECT_EQ(command.execute(f.project).code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_FALSE(command.fragment().has_value());
}
