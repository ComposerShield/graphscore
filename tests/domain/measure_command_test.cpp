// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

namespace graphscore {
namespace {

constexpr Voice kVoice1 = *Voice::create(1);

Rational rat(const std::int64_t numerator, const std::int64_t denominator) {
  return *Rational::create(numerator, denominator);
}

Measure measure(const std::uint8_t numerator, const std::uint16_t denominator) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 *KeySignature::create(0)};
}

TempoPoint point(const Rational position, const std::int64_t bpm) {
  return TempoPoint{position,
                    *Tempo::create(Rational(bpm), NoteValue::kQuarter)};
}

Duration whole() {
  return *Duration::create(NoteValue::kWhole, 0);
}

Duration half() {
  return *Duration::create(NoteValue::kHalf, 0);
}

Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

Duration triplet_quarter() {
  return *Duration::create(NoteValue::kQuarter, 0, TupletRatio::create(3, 2));
}

SpelledPitch pitch(const Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

void fill_stave(TrackLane* lane, const StaveId stave_id,
                const Rational length) {
  lane->ensure_stave(stave_id);
  StaveVoices* stave = lane->stave(stave_id);
  ASSERT_NE(stave, nullptr);
  for (std::uint8_t value = Voice::kMin; value <= Voice::kMax; ++value) {
    const std::optional<Voice> voice = Voice::create(value);
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(stave->voice(*voice).normalize(length).ok());
  }
}

VoiceContent tuplet_voice(const Rational prefix, const std::size_t event_count,
                          const bool chords, const Rational total_length) {
  VoiceContent content;
  if (prefix > Rational(0)) {
    const std::optional<std::vector<Rest>> rests = decompose_rest(prefix);
    assert(rests.has_value());
    for (const Rest& rest : *rests)
      assert(content.append(rest).ok());
  }
  for (std::size_t index = 0; index < event_count; ++index) {
    if (chords) {
      assert(content
                 .append(make_chord(
                     triplet_quarter(),
                     {{NotationEntityId{}, pitch(Letter::kC), false},
                      {NotationEntityId{}, pitch(Letter::kE), false}}))
                 .ok());
    } else {
      assert(
          content.append(make_note(pitch(Letter::kC), triplet_quarter())).ok());
    }
  }
  assert(content.normalize(total_length).ok());
  return content;
}

struct CascadeProject {
  Project project{ProjectId::generate(), "Measures"};
  NodeId  node_id;
  TrackId active_track;
  TrackId archived_track;
  StaveId active_stave;
  StaveId active_lower_stave;
  StaveId archived_stave;

  CascadeProject() {
    const StaffLayout active_layout = StaffLayout::grand_staff();
    active_stave                    = active_layout.staves()[0].id;
    active_lower_stave              = active_layout.staves()[1].id;
    active_track =
        *project.add_track("Active", active_layout, *MidiChannel::create(0));
    const StaffLayout archived_layout = StaffLayout::single_staff(Clef::kBass);
    archived_stave                    = archived_layout.staves()[0].id;
    archived_track = *project.add_track("Archived", archived_layout,
                                        *MidiChannel::create(1));
    node_id        = project.add_node("Node");

    Node* node = project.find_node(node_id);
    assert(node != nullptr);
    auto timeline =
        NodeTimeline::create({measure(4, 4), measure(4, 4), measure(4, 4)},
                             {{active_stave, Clef::kTreble},
                              {active_lower_stave, Clef::kBass},
                              {archived_stave, Clef::kBass}});
    assert(timeline.has_value());
    assert(timeline->set_pickdown(rat(1, 4)).ok());
    assert(timeline
               ->set_tempo({point(Rational(0), 100), point(Rational(1), 110),
                            point(Rational(2), 120), point(Rational(3), 130)})
               .ok());
    assert(
        timeline->add_clef_change(active_stave, Rational(1), Clef::kAlto).ok());
    assert(timeline->add_clef_change(active_stave, Rational(2), Clef::kTenor)
               .ok());
    node->set_timeline(std::move(*timeline));

    fill_stave(node->lane(active_track), active_stave, rat(13, 4));
    fill_stave(node->lane(active_track), active_lower_stave, rat(13, 4));
    fill_stave(node->lane(archived_track), archived_stave, rat(13, 4));
    assert(project.archive_track(archived_track).ok());
  }

  [[nodiscard]] Node* node() { return project.find_node(node_id); }
};

class CascadeFixture : public ::testing::Test {
 private:
  CascadeProject setup_;

 protected:
  Project& project        = setup_.project;
  NodeId&  node_id        = setup_.node_id;
  TrackId& active_track   = setup_.active_track;
  TrackId& archived_track = setup_.archived_track;
  StaveId& active_stave   = setup_.active_stave;
  StaveId& archived_stave = setup_.archived_stave;

  [[nodiscard]] Node* node() { return setup_.node(); }
};

TEST(NodeTimelineMeasureEditTest, InsertAndDeleteShiftWholeAbsoluteLanes) {
  const StaveId stave    = StaveId::generate();
  auto          timeline = NodeTimeline::create({measure(4, 4), measure(4, 4)},
                                                {{stave, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->add_clef_change(stave, Rational(0), Clef::kBass).ok());
  ASSERT_TRUE(timeline->add_clef_change(stave, Rational(1), Clef::kAlto).ok());
  ASSERT_TRUE(
      timeline->set_tempo({point(Rational(0), 100), point(Rational(1), 120)})
          .ok());

  const NodeTimeline original = *timeline;
  ASSERT_TRUE(timeline->insert_measure(0).ok());
  EXPECT_EQ(timeline->measures().measure_count(), 3u);
  EXPECT_EQ(timeline->clef_lane(stave)->changes()[0].position, Rational(1));
  ASSERT_NE(timeline->tempo(), nullptr);
  ASSERT_EQ(timeline->tempo()->points().size(), 3u);
  EXPECT_EQ(timeline->tempo()->points()[0].position, Rational(0));
  EXPECT_EQ(timeline->tempo()->points()[1].position, Rational(1));
  EXPECT_EQ(timeline->tempo()->points()[2].position, Rational(2));

  ASSERT_TRUE(timeline->remove_measure(0).ok());
  EXPECT_EQ(*timeline, original);
}

TEST(NodeTimelineMeasureEditTest, FirstDeleteReplacesDeletedOriginTempo) {
  auto timeline = NodeTimeline::create({measure(4, 4), measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(
      timeline->set_tempo({point(Rational(0), 100), point(Rational(1), 120)})
          .ok());

  ASSERT_TRUE(timeline->remove_measure(0).ok());
  ASSERT_NE(timeline->tempo(), nullptr);
  ASSERT_EQ(timeline->tempo()->points().size(), 1u);
  EXPECT_EQ(timeline->tempo()->points()[0], point(Rational(0), 120));
}

TEST(NodeTimelineMeasureEditTest, FirstDeleteKeepsMandatoryOriginAsFallback) {
  auto timeline = NodeTimeline::create({measure(4, 4), measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_tempo({point(Rational(0), 100)}).ok());

  ASSERT_TRUE(timeline->remove_measure(0).ok());
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->points(),
            (std::vector<TempoPoint>{point(Rational(0), 100)}));
}

TEST(NodeTimelineMeasureEditTest,
     DeleteUsesLaterTempoAndClefAtExactBoundaryAsReplacement) {
  const StaveId stave    = StaveId::generate();
  auto          timeline = NodeTimeline::create(
      {measure(4, 4), measure(4, 4), measure(4, 4)}, {{stave, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->add_clef_change(stave, Rational(0), Clef::kBass).ok());
  ASSERT_TRUE(timeline->add_clef_change(stave, Rational(1), Clef::kAlto).ok());
  ASSERT_TRUE(
      timeline->set_tempo({point(Rational(0), 100), point(Rational(1), 120)})
          .ok());

  ASSERT_TRUE(timeline->remove_measure(0).ok());
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->points(),
            (std::vector<TempoPoint>{point(Rational(0), 120)}));
  ASSERT_NE(timeline->clef_lane(stave), nullptr);
  EXPECT_EQ(timeline->clef_lane(stave)->changes(),
            (std::vector<ClefChange>{{Rational(0), Clef::kAlto}}));
}

TEST(NodeTimelineMeasureEditTest, RemovalDelegatesSoleMeasureGuard) {
  auto timeline = NodeTimeline::create({measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  const NodeTimeline original = *timeline;
  EXPECT_EQ(timeline->remove_measure(0).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*timeline, original);
}

TEST(NodeTimelineMeasureEditTest, ExplicitInsertRejectsOutOfRangeIndex) {
  auto timeline = NodeTimeline::create({measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  const NodeTimeline original = *timeline;
  EXPECT_EQ(timeline->insert_measure(2, measure(3, 4)).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(*timeline, original);
}

TEST(NodeTimelineMeasureEditTest, SameLengthSignatureChangesOnlyMeter) {
  auto timeline = NodeTimeline::create({measure(3, 4), measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_tempo({point(Rational(0), 100)}).ok());
  ASSERT_TRUE(
      timeline->set_measure_time_signature(0, *TimeSignature::create(6, 8))
          .ok());
  EXPECT_EQ(timeline->measures().measure(0).time_signature,
            *TimeSignature::create(6, 8));
  EXPECT_EQ(timeline->boundary_position(), rat(7, 4));
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->end(), rat(7, 4));
}

TEST_F(CascadeFixture, InsertMiddleCascadesActiveAndArchivedAllVoices) {
  const Node           original = *node();
  InsertMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());

  const NodeTimeline* timeline = node()->timeline();
  ASSERT_NE(timeline, nullptr);
  EXPECT_EQ(timeline->measures().measure_count(), 4u);
  EXPECT_EQ(timeline->node_end(), rat(17, 4));
  EXPECT_EQ(timeline->clef_lane(active_stave)->changes()[0].position,
            Rational(2));
  ASSERT_NE(timeline->tempo(), nullptr);
  EXPECT_EQ(timeline->tempo()->points()[1].position, Rational(2));

  for (const TrackId track : {active_track, archived_track}) {
    const TrackLane* lane = node()->lane(track);
    ASSERT_NE(lane, nullptr);
    for (const StaveId stave_id : lane->stave_ids()) {
      for (std::uint8_t value = Voice::kMin; value <= Voice::kMax; ++value) {
        const Voice voice = *Voice::create(value);
        EXPECT_TRUE(lane->stave(stave_id)
                        ->voice(voice)
                        .check_complete(rat(17, 4))
                        .ok());
      }
    }
  }

  const Node inserted = *node();
  ASSERT_TRUE(command.undo(project).ok());
  EXPECT_EQ(*node(), original);
  ASSERT_TRUE(command.redo(project).ok());
  EXPECT_EQ(*node(), inserted);
}

TEST_F(CascadeFixture, ExplicitAppendUsesSuppliedMeasure) {
  InsertMeasureCommand command(node_id, 3, measure(3, 4));
  ASSERT_TRUE(command.execute(project).ok());
  EXPECT_EQ(node()->timeline()->measures().measure(3), measure(3, 4));
  EXPECT_EQ(node()->timeline()->node_end(), Rational(4));
}

TEST_F(CascadeFixture, DeleteMiddleClosesGapAndRestoresExactly) {
  const Node           original = *node();
  DeleteMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  EXPECT_EQ(node()->timeline()->measures().measure_count(), 2u);
  EXPECT_EQ(node()->timeline()->node_end(), rat(9, 4));
  ASSERT_TRUE(command.undo(project).ok());
  EXPECT_EQ(*node(), original);
  ASSERT_TRUE(command.redo(project).ok());
  EXPECT_EQ(node()->timeline()->measures().measure_count(), 2u);
}

TEST_F(CascadeFixture, FirstAndLastDeleteAreSupported) {
  DeleteMeasureCommand first(node_id, 0);
  ASSERT_TRUE(first.execute(project).ok());
  EXPECT_EQ(node()->timeline()->measures().measure_count(), 2u);
  ASSERT_TRUE(first.undo(project).ok());

  DeleteMeasureCommand last(node_id, 2);
  ASSERT_TRUE(last.execute(project).ok());
  EXPECT_EQ(node()->timeline()->measures().measure_count(), 2u);
  EXPECT_EQ(node()->timeline()->pickdown_duration(), rat(1, 4));
}

TEST_F(CascadeFixture, CrossingNoteIsClippedWithoutInventingAttack) {
  VoiceContent crossing;
  ASSERT_TRUE(crossing.append(make_rest(half())).ok());
  const Note source_note = make_note(pitch(Letter::kC), whole(), true);
  ASSERT_TRUE(crossing.append(source_note).ok());
  ASSERT_TRUE(crossing.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = crossing;

  InsertMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  const VoiceContent& result =
      node()->lane(active_track)->stave(active_stave)->voice(kVoice1);
  EXPECT_EQ(result.position_of_event(source_note.id), rat(1, 2));
  const std::size_t note_index = *result.find_event_index_at(rat(1, 2));
  const Note&       clipped    = std::get<Note>(result.events()[note_index]);
  EXPECT_EQ(clipped.duration, half());
  EXPECT_FALSE(clipped.tied_to_next);
  EXPECT_FALSE(result.find_event_index_at(rat(3, 2)).has_value() &&
               std::holds_alternative<Note>(
                   result.events()[*result.find_event_index_at(rat(3, 2))]));
}

TEST_F(CascadeFixture, CrossingChordIsClippedAndAffectedTiesAreSevered) {
  VoiceContent crossing;
  ASSERT_TRUE(crossing.append(make_rest(half())).ok());
  const Chord source_chord =
      make_chord(whole(), {{NotationEntityId{}, pitch(Letter::kE), true},
                           {NotationEntityId{}, pitch(Letter::kG), true}});
  ASSERT_TRUE(crossing.append(source_chord).ok());
  ASSERT_TRUE(crossing.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = crossing;

  InsertMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  const VoiceContent& result =
      node()->lane(active_track)->stave(active_stave)->voice(kVoice1);
  const std::size_t chord_index = *result.find_event_index_at(rat(1, 2));
  const Chord&      clipped     = std::get<Chord>(result.events()[chord_index]);
  EXPECT_EQ(clipped.id, source_chord.id);
  EXPECT_EQ(clipped.duration, half());
  ASSERT_EQ(clipped.notes.size(), source_chord.notes.size());
  for (std::size_t i = 0; i < clipped.notes.size(); ++i) {
    EXPECT_EQ(clipped.notes[i].id, source_chord.notes[i].id);
    EXPECT_FALSE(clipped.notes[i].tied_to_next);
  }
  EXPECT_FALSE(result.find_event_index_at(rat(3, 2)).has_value() &&
               std::holds_alternative<Chord>(
                   result.events()[*result.find_event_index_at(rat(3, 2))]));
}

TEST_F(CascadeFixture, InsertionSeversTieAtBoundaryWithoutRejectingEdit) {
  VoiceContent content;
  ASSERT_TRUE(content.append(make_rest(half())).ok());
  const Note tied = make_note(pitch(Letter::kC), half(), true);
  const Note next = make_note(pitch(Letter::kC), half());
  ASSERT_TRUE(content.append(tied).ok());
  ASSERT_TRUE(content.append(next).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = content;

  InsertMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  const VoiceContent& result =
      node()->lane(active_track)->stave(active_stave)->voice(kVoice1);
  EXPECT_FALSE(std::get<Note>(result.events()[1]).tied_to_next);
  EXPECT_EQ(result.position_of_event(next.id), Rational(2));
  EXPECT_TRUE(validate_voice_references(result).empty());
}

TEST_F(CascadeFixture, IdReferencedMarkingsSurviveWhenEndpointsSurvive) {
  VoiceContent content;
  const Note   first   = make_note(pitch(Letter::kC), whole(), true);
  const Note   deleted = make_note(pitch(Letter::kC), whole());
  const Chord  last =
      make_chord(whole(), {{NotationEntityId{}, pitch(Letter::kE), false},
                           {NotationEntityId{}, pitch(Letter::kG), false}});
  ASSERT_TRUE(content.append(first).ok());
  ASSERT_TRUE(content.append(deleted).ok());
  ASSERT_TRUE(content.append(last).ok());
  ASSERT_TRUE(
      content.add_dynamic(make_dynamic_marking(first.id, Dynamic::kF)).ok());
  const Slur slur = make_slur(first.id, last.id);
  ASSERT_TRUE(content.add_slur(slur).ok());
  const GraceGroup grace =
      make_grace_group(deleted.id, {{NotationEntityId{}, pitch(Letter::kD),
                                     *Duration::create(NoteValue::kEighth, 0),
                                     GraceNoteType::kAppoggiatura, false}});
  ASSERT_TRUE(content.add_grace_group(grace).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = content;

  DeleteMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  const VoiceContent& result =
      node()->lane(active_track)->stave(active_stave)->voice(kVoice1);
  EXPECT_EQ(result.position_of_event(first.id), Rational(0));
  EXPECT_EQ(result.position_of_event(last.id), Rational(1));
  EXPECT_FALSE(std::get<Note>(result.events()[0]).tied_to_next);
  EXPECT_EQ(result.dynamics(), content.dynamics());
  EXPECT_EQ(result.slurs(), (std::vector<Slur>{slur}));
  EXPECT_TRUE(result.grace_groups().empty());
}

TEST_F(CascadeFixture, EveryIdReferencedFamilySurvivesAUniformLaterShift) {
  VoiceContent      content;
  std::vector<Note> notes;
  notes.reserve(16);
  for (std::size_t i = 0; i < 16; ++i) {
    Note note = make_note(pitch(Letter::kC), eighth(), i == 8);
    notes.push_back(note);
    ASSERT_TRUE(content.append(std::move(note)).ok());
  }
  const DynamicMarking dynamic = make_dynamic_marking(notes[8].id, Dynamic::kF);
  const Hairpin        hairpin =
      make_hairpin(notes[8].id, notes[10].id, HairpinDirection::kCrescendo);
  const Slur         slur = make_slur(notes[8].id, notes[10].id);
  const BeamOverride beam =
      make_beam_override(BeamOverride::Kind::kJoin, {notes[8].id, notes[9].id});
  const GraceGroup grace = make_grace_group(
      notes[10].id, {{NotationEntityId{}, pitch(Letter::kD), eighth(),
                      GraceNoteType::kAppoggiatura, false}});
  ASSERT_TRUE(content.add_dynamic(dynamic).ok());
  ASSERT_TRUE(content.add_hairpin(hairpin).ok());
  ASSERT_TRUE(content.add_slur(slur).ok());
  ASSERT_TRUE(content.add_beam_override(beam).ok());
  ASSERT_TRUE(content.add_grace_group(grace).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  ASSERT_TRUE(validate_voice_references(content).empty());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = content;

  InsertMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  const VoiceContent& result =
      node()->lane(active_track)->stave(active_stave)->voice(kVoice1);
  EXPECT_EQ(result.position_of_event(notes[8].id), Rational(2));
  EXPECT_TRUE(
      std::get<Note>(result.events()[*result.find_event_index_at(Rational(2))])
          .tied_to_next);
  EXPECT_EQ(result.dynamics(), (std::vector<DynamicMarking>{dynamic}));
  EXPECT_EQ(result.hairpins(), (std::vector<Hairpin>{hairpin}));
  EXPECT_EQ(result.slurs(), (std::vector<Slur>{slur}));
  EXPECT_EQ(result.beam_overrides(), (std::vector<BeamOverride>{beam}));
  EXPECT_EQ(result.grace_groups(), (std::vector<GraceGroup>{grace}));
  EXPECT_TRUE(validate_voice_references(result).empty());
}

TEST_F(CascadeFixture,
       InsertChronologicallySplitsReverseBeamWithStableIdsAndPreservesOthers) {
  VoiceContent      content;
  std::vector<Note> notes;
  notes.reserve(26);
  for (std::size_t index = 0; index < 26; ++index) {
    notes.push_back(make_note(pitch(Letter::kC), eighth()));
    ASSERT_TRUE(content.append(notes.back()).ok());
  }
  const BeamOverride unaffected = make_beam_override(
      BeamOverride::Kind::kBreak, {notes[0].id, notes[1].id});
  const BeamOverride split =
      make_beam_override(BeamOverride::Kind::kJoin,
                         {notes[9].id, notes[8].id, notes[7].id, notes[6].id});
  const BeamOverride remnant =
      make_beam_override(BeamOverride::Kind::kBreak,
                         {notes[5].id, notes[6].id, notes[7].id, notes[8].id});
  ASSERT_TRUE(content.add_beam_override(unaffected).ok());
  ASSERT_TRUE(content.add_beam_override(split).ok());
  ASSERT_TRUE(content.add_beam_override(remnant).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = content;

  const Node           original = *node();
  InsertMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  const VoiceContent& result =
      node()->lane(active_track)->stave(active_stave)->voice(kVoice1);
  ASSERT_EQ(result.beam_overrides().size(), 4u);
  EXPECT_EQ(result.beam_overrides()[0], unaffected);
  EXPECT_EQ(result.beam_overrides()[1],
            (BeamOverride{split.id, split.kind, {notes[6].id, notes[7].id}}));
  const NotationEntityId split_second_id = result.beam_overrides()[2].id;
  EXPECT_NE(split_second_id, split.id);
  EXPECT_EQ(result.beam_overrides()[2].events,
            (std::vector<NotationEntityId>{notes[8].id, notes[9].id}));
  EXPECT_EQ(
      result.beam_overrides()[3],
      (BeamOverride{
          remnant.id, remnant.kind, {notes[5].id, notes[6].id, notes[7].id}}));
  EXPECT_TRUE(validate_voice_references(result).empty());

  const Node inserted = *node();
  ASSERT_TRUE(command.undo(project).ok());
  EXPECT_EQ(*node(), original);
  ASSERT_TRUE(command.redo(project).ok());
  EXPECT_EQ(*node(), inserted);
  EXPECT_EQ(node()
                ->lane(active_track)
                ->stave(active_stave)
                ->voice(kVoice1)
                .beam_overrides()[2]
                .id,
            split_second_id);
}

TEST_F(CascadeFixture, DeleteRegroupsBeamSurvivorsAndDropsShortRuns) {
  VoiceContent      content;
  std::vector<Note> notes;
  const Duration    dotted_eighth = *Duration::create(NoteValue::kEighth, 1);
  for (std::size_t index = 0; index < 17; ++index) {
    notes.push_back(make_note(pitch(Letter::kC), dotted_eighth));
    ASSERT_TRUE(content.append(notes.back()).ok());
  }
  const BeamOverride split = make_beam_override(
      BeamOverride::Kind::kJoin,
      {notes[4].id, notes[5].id, notes[6].id, notes[7].id, notes[8].id,
       notes[9].id, notes[10].id, notes[11].id, notes[12].id});
  ASSERT_TRUE(content.add_beam_override(split).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = content;

  DeleteMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  const auto& overrides = node()
                              ->lane(active_track)
                              ->stave(active_stave)
                              ->voice(kVoice1)
                              .beam_overrides();
  ASSERT_EQ(overrides.size(), 2u);
  EXPECT_EQ(overrides[0].id, split.id);
  EXPECT_EQ(overrides[0].events,
            (std::vector<NotationEntityId>{notes[4].id, notes[5].id}));
  EXPECT_EQ(overrides[1].events,
            (std::vector<NotationEntityId>{notes[11].id, notes[12].id}));
}

TEST(SetMeasureTimeSignatureCommandTest,
     ExpansionSplitsBeamAndContractionRepairsSurvivors) {
  CascadeProject    fixture;
  VoiceContent      content;
  std::vector<Note> notes;
  for (std::size_t index = 0; index < 26; ++index) {
    notes.push_back(make_note(pitch(Letter::kC), eighth()));
    ASSERT_TRUE(content.append(notes.back()).ok());
  }
  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {notes[17].id, notes[16].id, notes[15].id, notes[14].id});
  ASSERT_TRUE(content.add_beam_override(beam).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  fixture.node()
      ->lane(fixture.active_track)
      ->stave(fixture.active_stave)
      ->voice(kVoice1) = content;

  SetMeasureTimeSignatureCommand grow(fixture.node_id, 1,
                                      *TimeSignature::create(6, 4));
  ASSERT_TRUE(grow.execute(fixture.project).ok());
  const auto& expanded = fixture.node()
                             ->lane(fixture.active_track)
                             ->stave(fixture.active_stave)
                             ->voice(kVoice1)
                             .beam_overrides();
  ASSERT_EQ(expanded.size(), 2u);
  EXPECT_EQ(expanded[0].id, beam.id);
  EXPECT_EQ(expanded[0].events,
            (std::vector<NotationEntityId>{notes[14].id, notes[15].id}));
  EXPECT_NE(expanded[1].id, beam.id);
  EXPECT_EQ(expanded[1].events,
            (std::vector<NotationEntityId>{notes[16].id, notes[17].id}));
  const auto expanded_snapshot = expanded;
  ASSERT_TRUE(grow.undo(fixture.project).ok());
  ASSERT_TRUE(grow.redo(fixture.project).ok());
  EXPECT_EQ(fixture.node()
                ->lane(fixture.active_track)
                ->stave(fixture.active_stave)
                ->voice(kVoice1)
                .beam_overrides(),
            expanded_snapshot);
}

TEST(SetMeasureTimeSignatureCommandTest,
     ContractionSplitsBeamSurvivorsAndDropsCutEvents) {
  CascadeProject    fixture;
  VoiceContent      content;
  std::vector<Note> notes;
  const Duration    dotted_eighth = *Duration::create(NoteValue::kEighth, 1);
  for (std::size_t index = 0; index < 17; ++index) {
    notes.push_back(make_note(pitch(Letter::kC), dotted_eighth));
    ASSERT_TRUE(content.append(notes.back()).ok());
  }
  const BeamOverride beam =
      make_beam_override(BeamOverride::Kind::kJoin,
                         {notes[1].id, notes[2].id, notes[3].id, notes[4].id,
                          notes[5].id, notes[6].id, notes[7].id});
  ASSERT_TRUE(content.add_beam_override(beam).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  fixture.node()
      ->lane(fixture.active_track)
      ->stave(fixture.active_stave)
      ->voice(kVoice1) = content;

  SetMeasureTimeSignatureCommand shrink(fixture.node_id, 0,
                                        *TimeSignature::create(2, 4));
  ASSERT_TRUE(shrink.execute(fixture.project).ok());
  const auto& repaired = fixture.node()
                             ->lane(fixture.active_track)
                             ->stave(fixture.active_stave)
                             ->voice(kVoice1)
                             .beam_overrides();
  ASSERT_EQ(repaired.size(), 2u);
  EXPECT_EQ(repaired[0],
            (BeamOverride{beam.id, beam.kind, {notes[1].id, notes[2].id}}));
  EXPECT_EQ(repaired[1].events,
            (std::vector<NotationEntityId>{notes[6].id, notes[7].id}));
}

TEST_F(CascadeFixture, PedalEndpointsFollowInsertAndDeleteWithStableIds) {
  TrackLane*      lane     = node()->lane(active_track);
  const PedalSpan before   = make_pedal_span(rat(1, 4), rat(3, 4));
  const PedalSpan crossing = make_pedal_span(rat(3, 4), rat(5, 4));
  const PedalSpan after    = make_pedal_span(rat(5, 4), rat(9, 4));
  ASSERT_TRUE(lane->add_pedal_span(active_stave, before).ok());
  ASSERT_TRUE(lane->add_pedal_span(active_stave, crossing).ok());
  ASSERT_TRUE(lane->add_pedal_span(active_stave, after).ok());

  InsertMeasureCommand insert(node_id, 1);
  ASSERT_TRUE(insert.execute(project).ok());
  const std::vector<PedalSpan>& inserted =
      *node()->lane(active_track)->pedal_spans(active_stave);
  EXPECT_EQ(inserted[0], before);
  EXPECT_EQ(inserted[1], (PedalSpan{crossing.id, crossing.start, rat(9, 4)}));
  EXPECT_EQ(inserted[2], (PedalSpan{after.id, rat(9, 4), rat(13, 4)}));

  DeleteMeasureCommand remove(node_id, 1);
  ASSERT_TRUE(remove.execute(project).ok());
  EXPECT_EQ(*node()->lane(active_track)->pedal_spans(active_stave),
            (std::vector<PedalSpan>{before, crossing, after}));
}

TEST_F(CascadeFixture, DeletePedalSpanBoundaryMatrix) {
  TrackLane*                   lane = node()->lane(active_track);
  const std::vector<PedalSpan> spans{make_pedal_span(rat(1, 4), Rational(1)),
                                     make_pedal_span(rat(5, 4), rat(7, 4)),
                                     make_pedal_span(Rational(1), Rational(2)),
                                     make_pedal_span(Rational(2), rat(9, 4)),
                                     make_pedal_span(rat(3, 4), rat(5, 4)),
                                     make_pedal_span(rat(7, 4), rat(9, 4)),
                                     make_pedal_span(rat(3, 4), rat(9, 4))};
  for (const PedalSpan& span : spans)
    ASSERT_TRUE(lane->add_pedal_span(active_stave, span).ok());

  DeleteMeasureCommand command(node_id, 1);
  ASSERT_TRUE(command.execute(project).ok());
  EXPECT_EQ(*node()->lane(active_track)->pedal_spans(active_stave),
            (std::vector<PedalSpan>{spans[0],
                                    {spans[3].id, Rational(1), rat(5, 4)},
                                    {spans[4].id, rat(3, 4), Rational(1)},
                                    {spans[5].id, Rational(1), rat(5, 4)},
                                    {spans[6].id, rat(3, 4), rat(5, 4)}}));
}

TEST(SetMeasureTimeSignatureCommandTest, LengthChangeMovesLaterContent) {
  CascadeProject                 fixture;
  SetMeasureTimeSignatureCommand command(fixture.node_id, 1,
                                         *TimeSignature::create(2, 4));
  ASSERT_TRUE(command.execute(fixture.project).ok());
  EXPECT_EQ(fixture.node()->timeline()->node_end(), rat(11, 4));
  EXPECT_EQ(fixture.node()
                ->timeline()
                ->clef_lane(fixture.active_stave)
                ->changes()[1]
                .position,
            rat(3, 2));
  ASSERT_TRUE(command.undo(fixture.project).ok());
  EXPECT_EQ(fixture.node()->timeline()->node_end(), rat(13, 4));
}

TEST(SetMeasureTimeSignatureCommandTest, SameLengthRoundTripsExactly) {
  CascadeProject                 fixture;
  const Node                     original = *fixture.node();
  SetMeasureTimeSignatureCommand command(fixture.node_id, 1,
                                         *TimeSignature::create(8, 8));
  ASSERT_TRUE(command.execute(fixture.project).ok());
  const Node changed = *fixture.node();
  EXPECT_EQ(fixture.node()->timeline()->measures().measure(1).time_signature,
            *TimeSignature::create(8, 8));
  ASSERT_TRUE(command.undo(fixture.project).ok());
  EXPECT_EQ(*fixture.node(), original);
  ASSERT_TRUE(command.redo(fixture.project).ok());
  EXPECT_EQ(*fixture.node(), changed);
}

TEST(SetMeasureTimeSignatureCommandTest, ExpansionInsertsRestTime) {
  CascadeProject                 fixture;
  SetMeasureTimeSignatureCommand command(fixture.node_id, 1,
                                         *TimeSignature::create(6, 4));
  ASSERT_TRUE(command.execute(fixture.project).ok());
  EXPECT_EQ(fixture.node()->timeline()->node_end(), rat(15, 4));
  for (const TrackId track : {fixture.active_track, fixture.archived_track}) {
    const TrackLane* lane = fixture.node()->lane(track);
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->total_length(), rat(15, 4));
  }
}

TEST(SetMeasureTimeSignatureCommandTest, InvalidatedPickdownRejectsAtomically) {
  CascadeProject                 fixture;
  const Node                     original = *fixture.node();
  SetMeasureTimeSignatureCommand command(fixture.node_id, 2,
                                         *TimeSignature::create(1, 8));
  EXPECT_EQ(command.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(*fixture.node(), original);
}

TEST_F(CascadeFixture, InsertRejectsTupletNoteCrossingEventBoundaryAtomically) {
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) =
      tuplet_voice(rat(1, 8), 6, false, rat(13, 4));
  const Node           original = *node();
  InsertMeasureCommand command(node_id, 1);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*node(), original);

  VoiceContent repaired;
  ASSERT_TRUE(repaired.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = repaired;
  EXPECT_TRUE(command.execute(project).ok());
}

TEST_F(CascadeFixture,
       InsertRejectsTupletChordCrossingEventBoundaryAtomically) {
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) =
      tuplet_voice(rat(1, 8), 6, true, rat(13, 4));
  const Node           original = *node();
  InsertMeasureCommand command(node_id, 1);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*node(), original);
}

TEST_F(CascadeFixture, InsertRejectsBoundaryBetweenEventsInsideTupletGroup) {
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) =
      tuplet_voice(Rational(0), 9, false, rat(13, 4));
  const Node           original = *node();
  InsertMeasureCommand command(node_id, 1);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*node(), original);
}

TEST_F(CascadeFixture, InsertAllowsCompleteTupletGroupStartAndEndBoundaries) {
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) =
      tuplet_voice(Rational(0), 6, false, rat(13, 4));
  InsertMeasureCommand at_end(node_id, 1);
  ASSERT_TRUE(at_end.execute(project).ok());
  ASSERT_TRUE(at_end.undo(project).ok());

  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) =
      tuplet_voice(Rational(1), 6, false, rat(13, 4));
  InsertMeasureCommand at_start(node_id, 1);
  EXPECT_TRUE(at_start.execute(project).ok());
}

TEST_F(CascadeFixture, DeleteRejectsTupletGroupsCutAtEitherBoundary) {
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) =
      tuplet_voice(rat(1, 8), 6, false, rat(13, 4));
  const Node           start_original = *node();
  DeleteMeasureCommand cuts_start(node_id, 1);
  EXPECT_EQ(cuts_start.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*node(), start_original);

  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) =
      tuplet_voice(rat(9, 8), 6, false, rat(13, 4));
  const Node           end_original = *node();
  DeleteMeasureCommand cuts_end(node_id, 1);
  EXPECT_EQ(cuts_end.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*node(), end_original);
}

TEST_F(CascadeFixture, DeleteAllowsCompleteTupletGroupAtBothBoundaries) {
  VoiceContent content = tuplet_voice(Rational(0), 6, false, Rational(1));
  ASSERT_TRUE(content.append(make_rest(whole())).ok());
  VoiceContent tail = tuplet_voice(Rational(0), 6, false, Rational(1));
  for (const VoiceEvent& event : tail.events())
    ASSERT_TRUE(content.append(event).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  node()->lane(active_track)->stave(active_stave)->voice(kVoice1) = content;

  DeleteMeasureCommand command(node_id, 1);
  EXPECT_TRUE(command.execute(project).ok());
}

TEST(SetMeasureTimeSignatureCommandTest,
     GrowAndShrinkRejectTupletGroupCutsAtomically) {
  CascadeProject grow_fixture;
  grow_fixture.node()
      ->lane(grow_fixture.active_track)
      ->stave(grow_fixture.active_stave)
      ->voice(kVoice1) = tuplet_voice(rat(9, 8), 6, false, rat(13, 4));
  const Node                     grow_original = *grow_fixture.node();
  SetMeasureTimeSignatureCommand grow(grow_fixture.node_id, 1,
                                      *TimeSignature::create(6, 4));
  EXPECT_EQ(grow.execute(grow_fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(*grow_fixture.node(), grow_original);

  CascadeProject shrink_fixture;
  shrink_fixture.node()
      ->lane(shrink_fixture.active_track)
      ->stave(shrink_fixture.active_stave)
      ->voice(kVoice1) = tuplet_voice(rat(1, 8), 6, false, rat(13, 4));
  const Node                     shrink_original = *shrink_fixture.node();
  SetMeasureTimeSignatureCommand shrink(shrink_fixture.node_id, 0,
                                        *TimeSignature::create(2, 4));
  EXPECT_EQ(shrink.execute(shrink_fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(*shrink_fixture.node(), shrink_original);
}

TEST(SetMeasureTimeSignatureCommandTest,
     GrowAndShrinkAllowCompleteTupletGroupBoundaries) {
  CascadeProject grow_fixture;
  grow_fixture.node()
      ->lane(grow_fixture.active_track)
      ->stave(grow_fixture.active_stave)
      ->voice(kVoice1) = tuplet_voice(Rational(1), 6, false, rat(13, 4));
  SetMeasureTimeSignatureCommand grow(grow_fixture.node_id, 1,
                                      *TimeSignature::create(6, 4));
  EXPECT_TRUE(grow.execute(grow_fixture.project).ok());

  CascadeProject shrink_fixture;
  VoiceContent   content = tuplet_voice(Rational(0), 3, false, rat(1, 2));
  ASSERT_TRUE(content.append(make_rest(half())).ok());
  VoiceContent tail = tuplet_voice(Rational(0), 3, false, rat(1, 2));
  for (const VoiceEvent& event : tail.events())
    ASSERT_TRUE(content.append(event).ok());
  ASSERT_TRUE(content.normalize(rat(13, 4)).ok());
  shrink_fixture.node()
      ->lane(shrink_fixture.active_track)
      ->stave(shrink_fixture.active_stave)
      ->voice(kVoice1) = content;
  SetMeasureTimeSignatureCommand shrink(shrink_fixture.node_id, 0,
                                        *TimeSignature::create(2, 4));
  EXPECT_TRUE(shrink.execute(shrink_fixture.project).ok());
}

TEST(DeleteMeasureCommandTest, InvalidatedPickdownRejectsAtomically) {
  Project      project(ProjectId::generate());
  const NodeId node_id = project.add_node();
  Node*        node    = project.find_node(node_id);
  auto timeline = NodeTimeline::create({measure(2, 4), measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(rat(3, 4)).ok());
  node->set_timeline(*timeline);
  const Node original = *node;

  DeleteMeasureCommand command(node_id, 1);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*node, original);
  EXPECT_EQ(command.undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(DeleteMeasureCommandTest, FailedExecuteCanBeRetriedAfterContextRepair) {
  Project      project(ProjectId::generate());
  const NodeId node_id = project.add_node();
  Node*        node    = project.find_node(node_id);
  auto timeline = NodeTimeline::create({measure(2, 4), measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(rat(3, 4)).ok());
  node->set_timeline(*timeline);

  DeleteMeasureCommand command(node_id, 1);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(node->timeline()->set_pickdown(rat(1, 4)).ok());
  EXPECT_TRUE(command.execute(project).ok());
  EXPECT_EQ(node->timeline()->measures().measure_count(), 1u);
}

TEST(MeasureCommandLifecycleTest, InvalidIndexesAndSoleDeleteDoNotMutate) {
  CascadeProject                 fixture;
  const Node                     original = *fixture.node();
  InsertMeasureCommand           insert(fixture.node_id, 4);
  DeleteMeasureCommand           remove(fixture.node_id, 3);
  SetMeasureTimeSignatureCommand signature(fixture.node_id, 3,
                                           *TimeSignature::create(3, 4));
  EXPECT_EQ(insert.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(remove.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(signature.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(*fixture.node(), original);

  Project      one(ProjectId::generate());
  const NodeId one_id = one.add_node();
  one.find_node(one_id)->set_timeline(
      *NodeTimeline::create({measure(4, 4)}, {}));
  DeleteMeasureCommand sole(one_id, 0);
  EXPECT_EQ(sole.execute(one).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(one.find_node(one_id)->timeline()->measures().measure_count(), 1u);
}

TEST(MeasureCommandLifecycleTest, MissingNodeRetryAndStaleContextAreAtomic) {
  CascadeProject       fixture;
  InsertMeasureCommand retry(NodeId::generate(), 0);
  EXPECT_EQ(retry.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(retry.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);

  InsertMeasureCommand command(fixture.node_id, 0);
  ASSERT_TRUE(command.execute(fixture.project).ok());
  fixture.node()->set_color(0x12345678);
  EXPECT_EQ(command.undo(fixture.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fixture.node()->color(), 0x12345678u);
  EXPECT_EQ(command.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
}

TEST(MeasureCommandLifecycleTest, MissingNodeDuringUndoAndRedoIsRetryable) {
  CascadeProject       fixture;
  InsertMeasureCommand command(fixture.node_id, 1);
  ASSERT_TRUE(command.execute(fixture.project).ok());
  const Node post = *fixture.node();

  ASSERT_TRUE(fixture.project.remove_node(fixture.node_id).ok());
  EXPECT_EQ(command.undo(fixture.project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(fixture.project.restore_node(post).ok());
  ASSERT_TRUE(command.undo(fixture.project).ok());
  const Node pre = *fixture.node();

  ASSERT_TRUE(fixture.project.remove_node(fixture.node_id).ok());
  EXPECT_EQ(command.redo(fixture.project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(fixture.project.restore_node(pre).ok());
  ASSERT_TRUE(command.redo(fixture.project).ok());
  EXPECT_EQ(*fixture.node(), post);
}

TEST(MeasureCommandLifecycleTest, MultiLaneValidationFailureIsAtomic) {
  CascadeProject fixture;
  ASSERT_TRUE(fixture.node()
                  ->lane(fixture.archived_track)
                  ->add_pedal_span(fixture.archived_stave,
                                   make_pedal_span(rat(1, 4), rat(1, 8)))
                  .ok());
  const Node original = *fixture.node();

  InsertMeasureCommand command(fixture.node_id, 1);
  EXPECT_EQ(command.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(*fixture.node(), original);
}

TEST(MeasureCommandLifecycleTest, UndoRedoReplayIsDeterministicForAllCommands) {
  CascadeProject                        fixture;
  std::vector<std::unique_ptr<Command>> commands;
  commands.push_back(
      std::make_unique<InsertMeasureCommand>(fixture.node_id, 1));
  commands.push_back(std::make_unique<SetMeasureTimeSignatureCommand>(
      fixture.node_id, 1, *TimeSignature::create(2, 4)));
  commands.push_back(
      std::make_unique<DeleteMeasureCommand>(fixture.node_id, 2));

  for (const std::unique_ptr<Command>& command : commands) {
    ASSERT_TRUE(command->execute(fixture.project).ok());
    const Node post = *fixture.node();
    ASSERT_TRUE(command->undo(fixture.project).ok());
    ASSERT_TRUE(command->redo(fixture.project).ok());
    EXPECT_EQ(*fixture.node(), post);
    ASSERT_TRUE(command->undo(fixture.project).ok());
    ASSERT_TRUE(command->redo(fixture.project).ok());
    EXPECT_EQ(*fixture.node(), post);
  }
}

TEST(MeasureCommandLifecycleTest, NoexceptProtocol) {
  static_assert(noexcept(
      std::declval<InsertMeasureCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<DeleteMeasureCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(std::declval<SetMeasureTimeSignatureCommand&>().redo(
      std::declval<Project&>())));
}

TEST(MeasureCommandIntegrationTest, HistoryAndTransactionRollback) {
  CascadeProject fixture;
  const Node     original = *fixture.node();
  CommandHistory history;
  ASSERT_TRUE(history
                  .execute_new(std::make_unique<InsertMeasureCommand>(
                                   fixture.node_id, 1),
                               fixture.project)
                  .ok());
  const Node inserted = *fixture.node();
  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_EQ(*fixture.node(), original);
  ASSERT_TRUE(history.redo(fixture.project).ok());
  EXPECT_EQ(*fixture.node(), inserted);

  ASSERT_TRUE(history.undo(fixture.project).ok());
  CommandTransaction transaction;
  ASSERT_TRUE(transaction
                  .add_command(std::make_unique<InsertMeasureCommand>(
                      fixture.node_id, 0))
                  .ok());
  ASSERT_TRUE(transaction
                  .add_command(std::make_unique<DeleteMeasureCommand>(
                      fixture.node_id, 99))
                  .ok());
  EXPECT_EQ(transaction.execute(fixture.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(*fixture.node(), original);
}

TEST(MeasureCommandIntegrationTest,
     SelectionIndexesAreNotMutatedAndRevalidateAfterDelete) {
  CascadeProject fixture;
  const auto     selection = FullMeasureSet::create(
      {{fixture.node_id, fixture.active_track, fixture.active_stave, 2}});
  ASSERT_TRUE(selection.has_value());
  const FullMeasureSet original = *selection;
  EXPECT_TRUE(
      validate_selection(fixture.project, Selection(*selection)).empty());

  DeleteMeasureCommand command(fixture.node_id, 0);
  ASSERT_TRUE(command.execute(fixture.project).ok());
  EXPECT_EQ(*selection, original);
  EXPECT_FALSE(
      validate_selection(fixture.project, Selection(*selection)).empty());
}

TEST(MeasureCommandIntegrationTest, CompatibilityIsALiveQuery) {
  CascadeProject fixture;
  const NodeId   other_id = fixture.project.add_node("Other");
  Node*          other    = fixture.project.find_node(other_id);
  ASSERT_NE(other, nullptr);
  auto timeline =
      NodeTimeline::create({measure(4, 4), measure(4, 4), measure(4, 4)}, {});
  ASSERT_TRUE(timeline.has_value());
  other->set_timeline(*timeline);
  ASSERT_TRUE(vertical_regions_compatible(*fixture.node()->timeline(),
                                          *other->timeline()));

  InsertMeasureCommand command(fixture.node_id, 1);
  ASSERT_TRUE(command.execute(fixture.project).ok());
  EXPECT_FALSE(vertical_regions_compatible(*fixture.node()->timeline(),
                                           *other->timeline()));
  ASSERT_TRUE(command.undo(fixture.project).ok());
  EXPECT_TRUE(vertical_regions_compatible(*fixture.node()->timeline(),
                                          *other->timeline()));
}

TEST(MeasureCommandIntegrationTest,
     SixtyFourthMeterCanBeInsertedAndRestFilled) {
  Project           project(ProjectId::generate());
  const StaffLayout layout = StaffLayout::single_staff(Clef::kTreble);
  const TrackId     track =
      *project.add_track("Track", layout, *MidiChannel::create(0));
  const NodeId node_id = project.add_node("Node");
  Node*        node    = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  node->set_timeline(*NodeTimeline::create(
      {measure(4, 4)}, {{layout.staves()[0].id, Clef::kTreble}}));
  fill_stave(node->lane(track), layout.staves()[0].id, Rational(1));

  InsertMeasureCommand command(node_id, 1, measure(1, 64));
  ASSERT_TRUE(command.execute(project).ok());
  EXPECT_EQ(node->timeline()->node_end(), rat(65, 64));
  const TrackLane* lane = node->lane(track);
  ASSERT_NE(lane, nullptr);
  for (std::uint8_t value = Voice::kMin; value <= Voice::kMax; ++value) {
    EXPECT_TRUE(lane->stave(layout.staves()[0].id)
                    ->voice(*Voice::create(value))
                    .check_complete(rat(65, 64))
                    .ok());
  }
}

TEST(MeasureCommandScaleTest,
     SixtyFourTracksAndMeasuresCascadeExactUndoRedoAcrossEveryVoice) {
  Project              project(ProjectId::generate(), "Scale");
  std::vector<TrackId> track_ids;
  std::vector<StaveId> stave_ids;
  track_ids.reserve(64);
  stave_ids.reserve(64);
  for (std::size_t index = 0; index < 64; ++index) {
    const StaffLayout layout = StaffLayout::single_staff(Clef::kTreble);
    stave_ids.push_back(layout.staves()[0].id);
    const std::optional<TrackId> track = project.add_track(
        "Track", layout,
        *MidiChannel::create(static_cast<std::uint8_t>(index % 16)));
    ASSERT_TRUE(track.has_value());
    track_ids.push_back(*track);
  }
  const NodeId node_id = project.add_node("Scale Node");
  Node*        node    = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  std::vector<Measure>         measures(64, measure(4, 4));
  std::vector<StaveDefinition> clefs;
  clefs.reserve(64);
  for (const StaveId stave_id : stave_ids)
    clefs.emplace_back(stave_id, Clef::kTreble);
  node->set_timeline(*NodeTimeline::create(std::move(measures), clefs));
  for (std::size_t index = 0; index < track_ids.size(); ++index)
    fill_stave(node->lane(track_ids[index]), stave_ids[index], Rational(64));

  const Node           original = *node;
  InsertMeasureCommand command(node_id, 32);
  ASSERT_TRUE(command.execute(project).ok());
  ASSERT_EQ(node->timeline()->measures().measure_count(), 65u);
  EXPECT_EQ(node->timeline()->node_end(), Rational(65));
  EXPECT_EQ(node->lane_count(), 64u);
  for (std::size_t index = 0; index < track_ids.size(); ++index) {
    const TrackLane* lane = node->lane(track_ids[index]);
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->total_length(), Rational(65));
    const StaveVoices* stave = lane->stave(stave_ids[index]);
    ASSERT_NE(stave, nullptr);
    for (std::uint8_t value = Voice::kMin; value <= Voice::kMax; ++value) {
      EXPECT_TRUE(stave->voice(*Voice::create(value))
                      .check_complete(Rational(65))
                      .ok());
    }
  }

  const Node inserted = *node;
  ASSERT_TRUE(command.undo(project).ok());
  EXPECT_EQ(*node, original);
  ASSERT_TRUE(command.redo(project).ok());
  EXPECT_EQ(*node, inserted);
}

}  // namespace
}  // namespace graphscore
