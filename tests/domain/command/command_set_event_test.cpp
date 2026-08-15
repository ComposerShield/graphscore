// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8e-i — SetEventCommand
// =========================================================================

TEST(CommandTest, SetEventNoteToRestRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent note = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent rest = make_rest(quarter());
  auto             cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), rest);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_GE(voice->events().size(),
            2u);  // quarter rest + dotted-half rest (3/4)
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, SetEventRestToNoteRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent note = make_note(pitch_c4(), quarter());
  auto             cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventNoteToChordRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  VoiceEvent  ve    = VoiceEvent(chord);
  auto        cmd   = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0), ve);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));
}

TEST(CommandTest, SetEventChordToNoteRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent note = make_note(pitch_c4(), half());
  auto             cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventDurationChangeRenormalizes) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Fill with a half note + a half rest → exactly 1 whole note.
  const VoiceEvent half_note = make_note(pitch_c4(), half());
  ASSERT_TRUE(voice->append(half_note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));

  // Replace half note with quarter note — creates a gap that normalise
  // fills with a quarter rest.
  const VoiceEvent quarter_note = make_note(pitch_c4(), quarter());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), quarter_note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());
  EXPECT_GE(voice->events().size(), 2u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_EQ(voice->events().size(), 2u);  // restored half note + half rest

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_GE(voice->events().size(), 2u);  // quarter note + rest fill
}

// Replace a rest with a whole note exactly filling an empty measure.
TEST(CommandTest, SetEventRestToWholeNoteFillsMeasure) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));
  ASSERT_GE(voice->events().size(), 1u);

  const VoiceEvent whole_note = make_note(pitch_c4(), whole());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), whole_note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  // Whole note fills the measure, but any remaining rests that overflow
  // will cause normalize to fail → the command rolls back.
  // With len=1 and a whole note at pos 0, the prev events (rests) get
  // pushed past node_end, so this should succeed only if there was
  // exactly 1 event and it was a rest of duration 1.
  // In practice, decompose_rest(1) gives [whole_rest], so this works.
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());
  ASSERT_GE(voice->events().size(), 1u);
  const graphscore::Note* n =
      std::get_if<graphscore::Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->duration.resolved(), whole().resolved());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
}

TEST(CommandTest, SetEventChordBuildAddNotehead) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord2 = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                              ChordNote{.pitch = pitch_d4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord2)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Chord chord3 = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                              ChordNote{.pitch = pitch_d4()},
                                              ChordNote{.pitch = pitch_e4()}});
  VoiceEvent  ve3    = VoiceEvent(chord3);
  auto        cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), ve3);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Chord* result = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->notes.size(), 3u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Chord* undone = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(undone, nullptr);
  EXPECT_EQ(undone->notes.size(), 2u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const Chord* redone = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(redone, nullptr);
  EXPECT_EQ(redone->notes.size(), 3u);
}

TEST(CommandTest, SetEventRejectsNilEmbeddedChordNoteId) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord original = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const auto orig_note0_id = original.notes[0].id;
  const auto orig_note1_id = original.notes[1].id;

  // Build a replacement chord with a nil ChordNote id.
  const Chord bad_chord =
      Chord{NotationEntityId::generate(),
            quarter(),
            {ChordNote{{}, pitch_c4(), false},
             ChordNote{NotationEntityId::generate(), pitch_e4(), false}},
            {},
            {}};
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(bad_chord));

  EXPECT_FALSE(cmd->execute(fx.project).ok());
  // Model unchanged.
  const Chord* result = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->notes[0].id, orig_note0_id);
  EXPECT_EQ(result->notes[1].id, orig_note1_id);
}

TEST(CommandTest, SetEventRejectsChordNoteIdEqualToParent) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord original = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const auto orig_note0_id = original.notes[0].id;

  // Build a chord where a ChordNote id equals the Chord's own id.
  const auto  parent_id = NotationEntityId::generate();
  const Chord bad_chord =
      Chord{parent_id,
            quarter(),
            {ChordNote{parent_id, pitch_c4(), false},
             ChordNote{NotationEntityId::generate(), pitch_e4(), false}},
            {},
            {}};
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(bad_chord));

  EXPECT_FALSE(cmd->execute(fx.project).ok());
  const Chord* result = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->notes[0].id, orig_note0_id);
}

TEST(CommandTest, SetEventDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventUndoWithoutExecuteRejected) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventRedoWithoutUndoRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventMissingNodeIdFails) {
  auto   fx      = make_notation_setup();
  NodeId missing = NodeId::generate();
  auto   cmd     = std::make_unique<SetEventCommand>(
      missing, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventMissingTrackIdFails) {
  auto    fx      = make_notation_setup();
  TrackId missing = TrackId::generate();
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, missing, fx.stave_id,
                                               *Voice::create(1), Rational(0),
                                               make_rest(quarter()));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventMissingStaveIdFails) {
  auto    fx      = make_notation_setup();
  StaveId missing = StaveId::generate();
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, missing,
                                               *Voice::create(1), Rational(0),
                                               make_rest(quarter()));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventNoTimelineFails) {
  Project project = make_project();
  auto    t       = project.add_track("T", StaffLayout::single_staff(),
                                      *MidiChannel::create(0));
  ASSERT_TRUE(t.has_value());
  NodeId nid = project.add_node("N");
  Node*  n   = project.find_node(nid);

  StaveId            sid;
  graphscore::Track* tr = project.find_active_track(*t);
  for (const graphscore::StaveDefinition& sd : tr->layout().staves()) {
    n->lane(*t)->ensure_stave(sd.id);
    sid = sd.id;
  }

  auto cmd = std::make_unique<SetEventCommand>(
      nid, *t, sid, *Voice::create(1), Rational(0), make_rest(quarter()));
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventInvalidPositionFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Position 1/8 is inside the first quarter note — not a boundary.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 8), make_rest(eighth()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice must be unchanged.
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventSingleNoteChordRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Chord bad_chord =
      make_chord(quarter(), {ChordNote{.pitch = pitch_c4()}});  // only 1 note
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(bad_chord));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventPreservesNotationEntityIdsOnRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent original = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(original).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent replacement = make_rest(quarter());
  NotationEntityId first_pass_id;

  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), replacement);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  first_pass_id = graphscore::event_id(voice->events()[0]);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());

  EXPECT_EQ(graphscore::event_id(voice->events()[0]), first_pass_id);
}

TEST(CommandTest, SetEventUnrelatedVoicePreserved) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* v1 =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));
  VoiceContent* v2 =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(2));

  ASSERT_TRUE(v1->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(v1->normalize(fx.node_end).ok());
  ASSERT_TRUE(v2->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(v2->normalize(fx.node_end).ok());

  const VoiceContent saved_v2 = *v2;

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  EXPECT_EQ(*v2, saved_v2);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*v2, saved_v2);
}

// =========================================================================
// Phase 8e-i — ConvertEventToRestCommand
// =========================================================================

TEST(CommandTest, ConvertNoteToRestRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  NotationEntityId note_id = graphscore::event_id(voice->events()[0]);

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
  EXPECT_EQ(voice->total_length(), fx.node_end);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(graphscore::event_id(voice->events()[0]), note_id);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, ConvertChordToRestRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, ConvertRestToRestIsIdempotent) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
  const Rest* r = std::get_if<Rest>(&voice->events()[0]);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->duration.resolved(), quarter().resolved());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, ConvertEventToRestDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConvertEventToRestMissingNodeFails) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      NodeId::generate(), fx.track_id, fx.stave_id, *Voice::create(1),
      Rational(0));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConvertEventToRestInvalidPositionFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 2));  // no event starts at 1/2
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConvertPreservesDuration) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), dotted_half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Rest* r = std::get_if<Rest>(&voice->events()[0]);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->duration.resolved(), dotted_half().resolved());
}
