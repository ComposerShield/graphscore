// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// StepAccidentalCommand (M5-phase-21)
// =========================================================================

namespace {

// The C4 rungs of the accidental ladder, spelled once for the tests below.
[[nodiscard]] SpelledPitch c4_with(Accidental accidental) {
  const std::optional<SpelledPitch> pitch =
      SpelledPitch::create(Letter::kC, 4, accidental);
  return pitch.value();
}

}  // namespace

TEST(CommandTest, StepAccidentalSingleNoteRaiseLowerRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       note    = make_note(pitch_c4(), quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto raise = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      AccidentalStepDirection::kRaise);
  ASSERT_TRUE(raise->execute(fx.project).ok());
  {
    const Note* n = std::get_if<Note>(&voice->events()[0]);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->pitch, c4_with(Accidental::kSharp));
    EXPECT_EQ(n->pitch.letter(), Letter::kC);
    EXPECT_EQ(n->pitch.octave(), 4);
    EXPECT_EQ(n->id, note_id);
    EXPECT_EQ(n->duration.resolved(), quarter().resolved());
  }

  ASSERT_TRUE(raise->undo(fx.project).ok());
  {
    const Note* n = std::get_if<Note>(&voice->events()[0]);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->pitch, pitch_c4());
    EXPECT_EQ(n->id, note_id);
  }

  ASSERT_TRUE(raise->redo(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch,
            c4_with(Accidental::kSharp));

  // Lowering walks back down the ladder rung by rung: sharp -> natural ->
  // flat, never skipping a rung and never wrapping.
  auto lower = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      AccidentalStepDirection::kLower);
  ASSERT_TRUE(lower->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_c4());

  auto lower_again = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      AccidentalStepDirection::kLower);
  ASSERT_TRUE(lower_again->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch,
            c4_with(Accidental::kFlat));
}

TEST(CommandTest, StepAccidentalWalksTheWholeLadder) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent note =
      make_note(c4_with(Accidental::kDoubleFlat), quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const std::array<Accidental, 4> kRungs = {
      Accidental::kFlat, Accidental::kNatural, Accidental::kSharp,
      Accidental::kDoubleSharp};
  for (const Accidental rung : kRungs) {
    auto raise = std::make_unique<StepAccidentalCommand>(
        fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
        AccidentalStepDirection::kRaise);
    ASSERT_TRUE(raise->execute(fx.project).ok());
    EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, c4_with(rung));
  }
}

TEST(CommandTest, StepAccidentalLadderEndsFailAtomically) {
  // Raising a double-sharp and lowering a double-flat are hard rejects: the
  // ladder never wraps and is never clamped, so no edit is applied at all.
  const std::array<std::pair<Accidental, AccidentalStepDirection>, 2> kEnds = {
      std::pair{Accidental::kDoubleSharp, AccidentalStepDirection::kRaise},
      std::pair{Accidental::kDoubleFlat, AccidentalStepDirection::kLower}};

  for (const auto& [accidental, direction] : kEnds) {
    auto          fx   = make_notation_setup();
    Node*         node = fx.project.find_node(fx.node_id);
    VoiceContent* voice =
        &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

    const SpelledPitch     end     = c4_with(accidental);
    const VoiceEvent       note    = make_note(end, quarter());
    const NotationEntityId note_id = graphscore::event_id(note);
    ASSERT_TRUE(voice->append(note).ok());
    ASSERT_TRUE(voice->normalize(fx.node_end).ok());
    const VoiceContent before = *voice;

    auto cmd = std::make_unique<StepAccidentalCommand>(
        fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
        direction);
    EXPECT_FALSE(cmd->execute(fx.project).ok());
    EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, end);
    EXPECT_EQ(*voice, before);
    // A failed execute records nothing, so undo is rejected too.
    EXPECT_FALSE(cmd->undo(fx.project).ok());
  }

  // step_notehead_accidental is the shared source of the same reject.
  EXPECT_FALSE(step_notehead_accidental(c4_with(Accidental::kDoubleSharp),
                                        AccidentalStepDirection::kRaise)
                   .has_value());
  EXPECT_FALSE(step_notehead_accidental(c4_with(Accidental::kDoubleFlat),
                                        AccidentalStepDirection::kLower)
                   .has_value());
}

TEST(CommandTest, StepAccidentalChordNoteheadPreservesChordFields) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}},
      {graphscore::Articulation::kAccent});
  const NotationEntityId chord_id     = chord.id;
  const NotationEntityId first_cn_id  = chord.notes[0].id;
  const NotationEntityId second_cn_id = chord.notes[1].id;
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto raise = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), first_cn_id,
      AccidentalStepDirection::kRaise);
  ASSERT_TRUE(raise->execute(fx.project).ok());
  {
    const Chord* c = std::get_if<Chord>(&voice->events()[0]);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->id, chord_id);
    ASSERT_EQ(c->notes.size(), 2u);
    EXPECT_EQ(c->notes[0].id, first_cn_id);
    EXPECT_EQ(c->notes[0].pitch, c4_with(Accidental::kSharp));
    EXPECT_EQ(c->notes[1].id, second_cn_id);
    EXPECT_EQ(c->notes[1].pitch, pitch_e4());
    ASSERT_EQ(c->articulations.size(), 1u);
    EXPECT_EQ(c->articulations[0], graphscore::Articulation::kAccent);
  }

  ASSERT_TRUE(raise->undo(fx.project).ok());
  {
    const Chord* c = std::get_if<Chord>(&voice->events()[0]);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->notes[0].id, first_cn_id);
    EXPECT_EQ(c->notes[0].pitch, pitch_c4());
  }
}

TEST(CommandTest, StepAccidentalGraceNotePreservesGroup) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const NotationEntityId principal = graphscore::event_id(voice->events()[0]);

  const GraceGroup group = make_grace_group(
      principal, {GraceNote{.pitch    = pitch_d4(),
                            .duration = eighth(),
                            .type     = GraceNoteType::kAcciaccatura,
                            .slashed  = true}});
  const NotationEntityId grace_id = group.notes[0].id;
  ASSERT_TRUE(voice->add_grace_group(group).ok());

  const std::optional<SpelledPitch> d_flat4 =
      SpelledPitch::create(Letter::kD, 4, Accidental::kFlat);
  ASSERT_TRUE(d_flat4.has_value());

  auto lower = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), grace_id,
      AccidentalStepDirection::kLower);
  ASSERT_TRUE(lower->execute(fx.project).ok());
  {
    ASSERT_EQ(voice->grace_groups().size(), 1u);
    const GraceGroup& g = voice->grace_groups()[0];
    EXPECT_EQ(g.id, group.id);
    EXPECT_EQ(g.principal_event, principal);
    ASSERT_EQ(g.notes.size(), 1u);
    EXPECT_EQ(g.notes[0].id, grace_id);
    EXPECT_EQ(g.notes[0].pitch, *d_flat4);
    EXPECT_TRUE(g.notes[0].slashed);
  }

  ASSERT_TRUE(lower->undo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups()[0].notes[0].pitch, pitch_d4());
}

TEST(CommandTest, StepAccidentalMidiBoundaryFailsAtomically) {
  // G9 sounds MIDI 127; raising it to G#9 would sound 128, which is not a
  // MIDI pitch even though the ladder itself allows the rung.
  {
    auto          fx   = make_notation_setup();
    Node*         node = fx.project.find_node(fx.node_id);
    VoiceContent* voice =
        &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

    const SpelledPitch     g9      = *SpelledPitch::create(Letter::kG, 9);
    const VoiceEvent       note    = make_note(g9, whole());
    const NotationEntityId note_id = graphscore::event_id(note);
    ASSERT_TRUE(voice->append(note).ok());
    ASSERT_TRUE(voice->normalize(fx.node_end).ok());

    auto raise = std::make_unique<StepAccidentalCommand>(
        fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
        AccidentalStepDirection::kRaise);
    EXPECT_FALSE(raise->execute(fx.project).ok());
    EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, g9);
  }

  // C-1 sounds MIDI 0; lowering it to Cb-1 would sound -1.
  {
    auto          fx   = make_notation_setup();
    Node*         node = fx.project.find_node(fx.node_id);
    VoiceContent* voice =
        &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

    const SpelledPitch     c_m1    = *SpelledPitch::create(Letter::kC, -1);
    const VoiceEvent       note    = make_note(c_m1, whole());
    const NotationEntityId note_id = graphscore::event_id(note);
    ASSERT_TRUE(voice->append(note).ok());
    ASSERT_TRUE(voice->normalize(fx.node_end).ok());

    auto lower = std::make_unique<StepAccidentalCommand>(
        fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
        AccidentalStepDirection::kLower);
    EXPECT_FALSE(lower->execute(fx.project).ok());
    EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, c_m1);
  }
}

TEST(CommandTest, StepAccidentalStaleIdentityFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent before = *voice;

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      NotationEntityId::generate(), AccidentalStepDirection::kRaise);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(*voice, before);
}

TEST(CommandTest, StepAccidentalRestIdFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       rest    = make_rest(quarter());
  const NotationEntityId rest_id = graphscore::event_id(rest);
  ASSERT_TRUE(voice->append(rest).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent before = *voice;

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), rest_id,
      AccidentalStepDirection::kRaise);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(*voice, before);
}

TEST(CommandTest, StepAccidentalTiedOutgoingChainStepsWholeChain) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // C4 tied to the following C4. SpelledPitch carries the accidental, so a
  // tied notehead whose spelling diverges from its successor's fails
  // validate_ties (kTiePitchMismatch); the whole chain must therefore step
  // together and the result must still validate.
  const VoiceEvent       tied      = make_note(pitch_c4(), quarter(), true);
  const NotationEntityId tied_id   = graphscore::event_id(tied);
  const VoiceEvent       second    = make_note(pitch_c4(), quarter());
  const NotationEntityId second_id = graphscore::event_id(second);
  ASSERT_TRUE(voice->append(tied).ok());
  ASSERT_TRUE(voice->append(second).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_id,
      AccidentalStepDirection::kRaise);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    const Note& first = std::get<Note>(voice->events()[0]);
    const Note& next  = std::get<Note>(voice->events()[1]);
    EXPECT_EQ(first.pitch, c4_with(Accidental::kSharp));
    EXPECT_EQ(next.pitch, c4_with(Accidental::kSharp));
    EXPECT_TRUE(first.tied_to_next);
    EXPECT_EQ(first.id, tied_id);
    EXPECT_EQ(next.id, second_id);
  }
  EXPECT_TRUE(voice->validate().ok());
  EXPECT_TRUE(validate_voice_references(*voice).empty());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_c4());
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch, pitch_c4());
  EXPECT_TRUE(std::get<Note>(voice->events()[0]).tied_to_next);
}

TEST(CommandTest, StepAccidentalTiedIncomingChainStepsWholeChain) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent first = make_note(pitch_c4(), quarter(), true);
  ASSERT_TRUE(voice->append(first).ok());
  const VoiceEvent       second    = make_note(pitch_c4(), quarter());
  const NotationEntityId second_id = graphscore::event_id(second);
  ASSERT_TRUE(voice->append(second).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), second_id,
      AccidentalStepDirection::kLower);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch,
            c4_with(Accidental::kFlat));
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch,
            c4_with(Accidental::kFlat));
  EXPECT_TRUE(std::get<Note>(voice->events()[0]).tied_to_next);
  EXPECT_TRUE(voice->validate().ok());
}

TEST(CommandTest, StepAccidentalCrossMeasureTieStepsWholeChain) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  const VoiceEvent       tied    = make_note(pitch_c4(), quarter(), true);
  const NotationEntityId tied_id = graphscore::event_id(tied);
  ASSERT_TRUE(voice->append(tied).ok());
  const VoiceEvent       next    = make_note(pitch_c4(), quarter());
  const NotationEntityId next_id = graphscore::event_id(next);
  ASSERT_TRUE(voice->append(next).ok());

  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)},
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto tl = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(tl.has_value());
  node->set_timeline(std::move(*tl));
  const Rational node_end = Rational(2);
  ASSERT_TRUE(voice->normalize(node_end).ok());

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_id,
      AccidentalStepDirection::kRaise);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[3]).pitch,
            c4_with(Accidental::kSharp));
  EXPECT_EQ(std::get<Note>(voice->events()[4]).pitch,
            c4_with(Accidental::kSharp));
  EXPECT_TRUE(std::get<Note>(voice->events()[3]).tied_to_next);
  EXPECT_EQ(std::get<Note>(voice->events()[3]).id, tied_id);
  EXPECT_EQ(std::get<Note>(voice->events()[4]).id, next_id);
  EXPECT_TRUE(voice->validate().ok());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[3]).pitch, pitch_c4());
  EXPECT_EQ(std::get<Note>(voice->events()[4]).pitch, pitch_c4());
}

TEST(CommandTest, StepAccidentalChordTieChainStepsBothChordNoteheads) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const ChordNote tied_note{NotationEntityId::generate(), pitch_c4(), true};
  const NotationEntityId tied_cn_id = tied_note.id;
  const Chord            first      = make_chord(
      quarter(),
      {tied_note, ChordNote{NotationEntityId::generate(), pitch_e4(), false}});
  const ChordNote next_note{NotationEntityId::generate(), pitch_c4(), false};
  const NotationEntityId next_cn_id = next_note.id;
  const Chord            second     = make_chord(
      quarter(),
      {next_note, ChordNote{NotationEntityId::generate(), pitch_e4(), false}});
  ASSERT_TRUE(voice->append(first).ok());
  ASSERT_TRUE(voice->append(second).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_cn_id,
      AccidentalStepDirection::kRaise);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    const Chord& c1 = std::get<Chord>(voice->events()[0]);
    const Chord& c2 = std::get<Chord>(voice->events()[1]);
    EXPECT_EQ(c1.notes[0].pitch, c4_with(Accidental::kSharp));
    EXPECT_EQ(c1.notes[1].pitch, pitch_e4());
    EXPECT_EQ(c2.notes[0].pitch, c4_with(Accidental::kSharp));
    EXPECT_EQ(c2.notes[1].pitch, pitch_e4());
    EXPECT_TRUE(c1.notes[0].tied_to_next);
    EXPECT_EQ(c1.notes[0].id, tied_cn_id);
    EXPECT_EQ(c2.notes[0].id, next_cn_id);
  }
  EXPECT_TRUE(voice->validate().ok());
}

TEST(CommandTest, StepAccidentalTiedChainBoundaryFailsAtomically) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // G9 tied to G9: raising the chain would push both to G#9 (MIDI 128), so
  // the whole chain must fail atomically rather than step one member.
  const SpelledPitch     g9      = *SpelledPitch::create(Letter::kG, 9);
  const VoiceEvent       tied    = make_note(g9, quarter(), true);
  const NotationEntityId tied_id = graphscore::event_id(tied);
  ASSERT_TRUE(voice->append(tied).ok());
  ASSERT_TRUE(voice->append(make_note(g9, quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent before = *voice;

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_id,
      AccidentalStepDirection::kRaise);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(*voice, before);
}

TEST(CommandTest, StepAccidentalIncompleteVoicePreservesRhythm) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // A single quarter note in a 4/4 measure, deliberately NOT normalized.
  const VoiceEvent       note    = make_note(pitch_c4(), quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      AccidentalStepDirection::kRaise);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    ASSERT_EQ(voice->events().size(), 1u);
    const Note& stepped = std::get<Note>(voice->events()[0]);
    EXPECT_EQ(stepped.pitch, c4_with(Accidental::kSharp));
    EXPECT_EQ(stepped.id, note_id);
    EXPECT_EQ(stepped.duration.resolved(), quarter().resolved());
  }

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_EQ(voice->events().size(), 1u);
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_c4());
}

TEST(CommandTest, StepAccidentalGraceGroupOrderPreserved) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       principal_event = make_note(pitch_c4(), quarter());
  const NotationEntityId principal = graphscore::event_id(principal_event);
  ASSERT_TRUE(voice->append(principal_event).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup first_group = make_grace_group(
      principal, {GraceNote{NotationEntityId::generate(), pitch_d4(), eighth(),
                            GraceNoteType::kAcciaccatura, true}});
  const NotationEntityId first_grace_id = first_group.notes[0].id;
  const GraceGroup       second_group   = make_grace_group(
      principal, {GraceNote{NotationEntityId::generate(), pitch_g4(), eighth(),
                            GraceNoteType::kAcciaccatura, true}});
  ASSERT_TRUE(voice->add_grace_group(first_group).ok());
  ASSERT_TRUE(voice->add_grace_group(second_group).ok());

  const std::optional<SpelledPitch> d_sharp4 =
      SpelledPitch::create(Letter::kD, 4, Accidental::kSharp);
  ASSERT_TRUE(d_sharp4.has_value());

  auto cmd = std::make_unique<StepAccidentalCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), first_grace_id,
      AccidentalStepDirection::kRaise);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    ASSERT_EQ(voice->grace_groups().size(), 2u);
    EXPECT_EQ(voice->grace_groups()[0].id, first_group.id);
    EXPECT_EQ(voice->grace_groups()[1].id, second_group.id);
    EXPECT_EQ(voice->grace_groups()[0].notes[0].pitch, *d_sharp4);
    EXPECT_EQ(voice->grace_groups()[1].notes[0].pitch, pitch_g4());
  }

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_EQ(voice->grace_groups().size(), 2u);
  EXPECT_EQ(voice->grace_groups()[0].id, first_group.id);
  EXPECT_EQ(voice->grace_groups()[1].id, second_group.id);
  EXPECT_EQ(voice->grace_groups()[0].notes[0].pitch, pitch_d4());
}
