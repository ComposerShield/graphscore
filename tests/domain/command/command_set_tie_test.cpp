// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8e-i — SetTieCommand
// =========================================================================

TEST(CommandTest, SetTieNoteTieThenUntieRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie the first note.
  auto tie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::nullopt, true);

  ASSERT_TRUE(tie_cmd->execute(fx.project).ok());
  const Note* n1 = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1, nullptr);
  EXPECT_TRUE(n1->tied_to_next);

  ASSERT_TRUE(tie_cmd->undo(fx.project).ok());
  const Note* n1u = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1u, nullptr);
  EXPECT_FALSE(n1u->tied_to_next);

  ASSERT_TRUE(tie_cmd->redo(fx.project).ok());
  const Note* n1r = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1r, nullptr);
  EXPECT_TRUE(n1r->tied_to_next);

  // Untie it.
  auto untie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::nullopt, false);

  ASSERT_TRUE(untie_cmd->execute(fx.project).ok());
  const Note* n1u2 = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1u2, nullptr);
  EXPECT_FALSE(n1u2->tied_to_next);
}

TEST(CommandTest, SetTieChordNoteheadTieThenUntieRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie notehead index 0 (C4) in the chord.
  auto tie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(0), true);

  ASSERT_TRUE(tie_cmd->execute(fx.project).ok());
  const Chord* c = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c, nullptr);
  EXPECT_TRUE(c->notes[0].tied_to_next);   // C4
  EXPECT_FALSE(c->notes[1].tied_to_next);  // E4

  ASSERT_TRUE(tie_cmd->undo(fx.project).ok());
  const Chord* cu = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(cu, nullptr);
  EXPECT_FALSE(cu->notes[0].tied_to_next);
  EXPECT_FALSE(cu->notes[1].tied_to_next);

  ASSERT_TRUE(tie_cmd->redo(fx.project).ok());
  const Chord* cr = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(cr, nullptr);
  EXPECT_TRUE(cr->notes[0].tied_to_next);
  EXPECT_FALSE(cr->notes[1].tied_to_next);
}

TEST(CommandTest, SetTieChordNoIndexRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  const Chord successor = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(successor)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Chord requires an explicit notehead index; nullopt is rejected.
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged — no tie flags set.
  const Chord* c = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->notes[0].tied_to_next);
  EXPECT_FALSE(c->notes[1].tied_to_next);
}

TEST(CommandTest, SetTieChordNoteheadMismatchedRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  // Successor has C4 but not E4 — tying E4 (index 1) is invalid.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(1), true);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged — no tie flags set.
  const Chord* c = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->notes[0].tied_to_next);
  EXPECT_FALSE(c->notes[1].tied_to_next);
}

TEST(CommandTest, SetTieRestFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged.
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, SetTieChordMissingNoteheadFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Index 2 is out of range (chord has only 2 notes).
  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(2), true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieNoteRejectsExplicitNoteheadIndex) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // A Note carries a single tie flag; supplying an explicit notehead index
  // is rejected.
  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(0), true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged.
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

TEST(CommandTest, SetTieDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieUndoWithoutExecuteRejected) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieRedoWithoutUndoRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieMissingNodeFails) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<SetTieCommand>(NodeId::generate(), fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieInvalidPositionFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 2), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTiePreservesOtherEventFields) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent note    = make_note(pitch_d4(), half(), false,
                                       {graphscore::Articulation::kStaccato});
  NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  // A valid tie needs a successor that sounds the same pitch.
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_TRUE(n->tied_to_next);
  EXPECT_EQ(n->pitch, pitch_d4());
  EXPECT_EQ(n->duration.resolved(), half().resolved());
  EXPECT_EQ(n->id, note_id);
  ASSERT_EQ(n->articulations.size(), 1u);
  EXPECT_EQ(n->articulations[0], graphscore::Articulation::kStaccato);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Note* nu = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(nu, nullptr);
  EXPECT_FALSE(nu->tied_to_next);
  EXPECT_EQ(nu->id, note_id);
}

// =========================================================================
// Phase 8e-i — SetTie stale-context retry
// =========================================================================

TEST(CommandTest, SetTieUndoStaleContextRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const VoiceContent post_state = *voice;

  // Manually change voice — undo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore voice to exact post-snapshot — undo succeeds.
  *voice = post_state;
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

TEST(CommandTest, SetTieRedoStaleContextRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceContent original = *voice;

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Manually change voice — redo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore voice to exact pre-snapshot — redo succeeds.
  *voice = original;
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_TRUE(n->tied_to_next);
}
