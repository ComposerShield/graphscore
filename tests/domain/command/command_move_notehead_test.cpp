// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// MoveNoteheadCommand (M5-phase-20)
// =========================================================================

TEST(CommandTest, MoveNoteheadSingleNoteUpDownRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       note    = make_note(pitch_c4(), quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto up = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(up->execute(fx.project).ok());
  {
    const Note* n = std::get_if<Note>(&voice->events()[0]);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->pitch, pitch_d4());
    EXPECT_EQ(n->id, note_id);
    EXPECT_EQ(n->duration.resolved(), quarter().resolved());
  }

  ASSERT_TRUE(up->undo(fx.project).ok());
  {
    const Note* n = std::get_if<Note>(&voice->events()[0]);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->pitch, pitch_c4());
    EXPECT_EQ(n->id, note_id);
  }

  ASSERT_TRUE(up->redo(fx.project).ok());
  {
    const Note* n = std::get_if<Note>(&voice->events()[0]);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->pitch, pitch_d4());
    EXPECT_EQ(n->id, note_id);
  }

  auto down = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      NoteheadStepDirection::kDown);
  ASSERT_TRUE(down->execute(fx.project).ok());
  {
    const Note* n = std::get_if<Note>(&voice->events()[0]);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->pitch, pitch_c4());
    EXPECT_EQ(n->id, note_id);
  }
}

TEST(CommandTest, MoveNoteheadChordNoteheadPreservesChordFields) {
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

  auto up = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), first_cn_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(up->execute(fx.project).ok());
  {
    const Chord* c = std::get_if<Chord>(&voice->events()[0]);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->id, chord_id);
    ASSERT_EQ(c->notes.size(), 2u);
    EXPECT_EQ(c->notes[0].id, first_cn_id);
    EXPECT_EQ(c->notes[0].pitch, pitch_d4());
    EXPECT_EQ(c->notes[1].id, second_cn_id);
    EXPECT_EQ(c->notes[1].pitch, pitch_e4());
    ASSERT_EQ(c->articulations.size(), 1u);
    EXPECT_EQ(c->articulations[0], graphscore::Articulation::kAccent);
  }

  ASSERT_TRUE(up->undo(fx.project).ok());
  {
    const Chord* c = std::get_if<Chord>(&voice->events()[0]);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->notes[0].id, first_cn_id);
    EXPECT_EQ(c->notes[0].pitch, pitch_c4());
  }
}

TEST(CommandTest, MoveNoteheadOctaveWrapUpAndDown) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const SpelledPitch b4 = *SpelledPitch::create(Letter::kB, 4);
  const SpelledPitch c5 = *SpelledPitch::create(Letter::kC, 5);
  const VoiceEvent   note =
      make_note(b4, whole());  // whole note fills the 4/4 measure exactly
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto up = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(up->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, c5);

  auto down = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      NoteheadStepDirection::kDown);
  ASSERT_TRUE(down->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, b4);
}

TEST(CommandTest, MoveNoteheadAccidentalPreserved) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const SpelledPitch cs4 =
      *SpelledPitch::create(Letter::kC, 4, Accidental::kSharp);
  const SpelledPitch ds4 =
      *SpelledPitch::create(Letter::kD, 4, Accidental::kSharp);
  const VoiceEvent       note    = make_note(cs4, quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto up = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(up->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, ds4);
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch.accidental(),
            Accidental::kSharp);

  auto down = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      NoteheadStepDirection::kDown);
  ASSERT_TRUE(down->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, cs4);
}

TEST(CommandTest, MoveNoteheadGraceNotePreservesGroup) {
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

  auto up = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), grace_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(up->execute(fx.project).ok());
  {
    ASSERT_EQ(voice->grace_groups().size(), 1u);
    const GraceGroup& g = voice->grace_groups()[0];
    EXPECT_EQ(g.id, group.id);
    EXPECT_EQ(g.principal_event, principal);
    ASSERT_EQ(g.notes.size(), 1u);
    EXPECT_EQ(g.notes[0].id, grace_id);
    EXPECT_EQ(g.notes[0].pitch, pitch_e4());
    EXPECT_TRUE(g.notes[0].slashed);
  }

  ASSERT_TRUE(up->undo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups()[0].notes[0].pitch, pitch_d4());
}

TEST(CommandTest, MoveNoteheadBoundaryFailsAtomically) {
  // G9 up would spell A9 (MIDI 129) -- beyond the sounding range.
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

    auto up = std::make_unique<MoveNoteheadCommand>(
        fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
        NoteheadStepDirection::kUp);
    EXPECT_FALSE(up->execute(fx.project).ok());
    EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, g9);
  }

  // C-1 down would spell B-2 -- below the octave range.
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

    auto down = std::make_unique<MoveNoteheadCommand>(
        fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
        NoteheadStepDirection::kDown);
    EXPECT_FALSE(down->execute(fx.project).ok());
    EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, c_m1);
  }

  // F##9 up would spell G##9 (MIDI 129) -- the double-sharp pushes past 127
  // even though the octave is in range, and the accidental must never be
  // silently changed to make it fit.
  {
    auto          fx   = make_notation_setup();
    Node*         node = fx.project.find_node(fx.node_id);
    VoiceContent* voice =
        &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

    const SpelledPitch fds9 =
        *SpelledPitch::create(Letter::kF, 9, Accidental::kDoubleSharp);
    const VoiceEvent       note    = make_note(fds9, whole());
    const NotationEntityId note_id = graphscore::event_id(note);
    ASSERT_TRUE(voice->append(note).ok());
    ASSERT_TRUE(voice->normalize(fx.node_end).ok());

    auto up = std::make_unique<MoveNoteheadCommand>(
        fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
        NoteheadStepDirection::kUp);
    EXPECT_FALSE(up->execute(fx.project).ok());
    EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, fds9);
  }
}

TEST(CommandTest, MoveNoteheadStaleIdentityFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent before = *voice;

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      NotationEntityId::generate(), NoteheadStepDirection::kUp);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(*voice, before);
}

TEST(CommandTest, MoveNoteheadRestIdFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       rest    = make_rest(quarter());
  const NotationEntityId rest_id = graphscore::event_id(rest);
  ASSERT_TRUE(voice->append(rest).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent before = *voice;

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), rest_id,
      NoteheadStepDirection::kUp);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(*voice, before);
}

TEST(CommandTest, MoveNoteheadTiedOutgoingChainMovesWholeChain) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // C4 tied to the following C4. Moving the first notehead up must move both
  // by the same diatonic step, preserving the tie rather than rejecting it.
  const VoiceEvent       tied      = make_note(pitch_c4(), quarter(), true);
  const NotationEntityId tied_id   = graphscore::event_id(tied);
  const VoiceEvent       second    = make_note(pitch_c4(), quarter());
  const NotationEntityId second_id = graphscore::event_id(second);
  ASSERT_TRUE(voice->append(tied).ok());
  ASSERT_TRUE(voice->append(second).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    const Note& first = std::get<Note>(voice->events()[0]);
    const Note& next  = std::get<Note>(voice->events()[1]);
    EXPECT_EQ(first.pitch, pitch_d4());
    EXPECT_EQ(next.pitch, pitch_d4());
    EXPECT_TRUE(first.tied_to_next);
    EXPECT_EQ(first.id, tied_id);
    EXPECT_EQ(next.id, second_id);
  }

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_c4());
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch, pitch_c4());
  EXPECT_TRUE(std::get<Note>(voice->events()[0]).tied_to_next);
}

TEST(CommandTest, MoveNoteheadTiedIncomingChainMovesWholeChain) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // C4 tied to the following C4. Selecting the *second* (incoming) notehead
  // must still move the whole chain, because both noteheads are one tied
  // logical unit.
  const VoiceEvent first = make_note(pitch_c4(), quarter(), true);
  ASSERT_TRUE(voice->append(first).ok());
  const VoiceEvent       second    = make_note(pitch_c4(), quarter());
  const NotationEntityId second_id = graphscore::event_id(second);
  ASSERT_TRUE(voice->append(second).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), second_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_d4());
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch, pitch_d4());
  EXPECT_TRUE(std::get<Note>(voice->events()[0]).tied_to_next);
}

TEST(CommandTest, MoveNoteheadCrossMeasureTieMovesWholeChain) {
  // Two 4/4 measures: C4 (tied) as the last note of measure 1, C4 as the
  // first note of measure 2. The tie crosses the barline; moving either end
  // must step both noteheads.
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Fill measure 1: three quarter rests, then a tied C4 quarter.
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

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[3]).pitch, pitch_d4());
  EXPECT_EQ(std::get<Note>(voice->events()[4]).pitch, pitch_d4());
  EXPECT_TRUE(std::get<Note>(voice->events()[3]).tied_to_next);
  EXPECT_EQ(std::get<Note>(voice->events()[3]).id, tied_id);
  EXPECT_EQ(std::get<Note>(voice->events()[4]).id, next_id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(std::get<Note>(voice->events()[3]).pitch, pitch_c4());
  EXPECT_EQ(std::get<Note>(voice->events()[4]).pitch, pitch_c4());
}

TEST(CommandTest, MoveNoteheadChordTieChainMovesBothChordNoteheads) {
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

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_cn_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    const Chord& c1 = std::get<Chord>(voice->events()[0]);
    const Chord& c2 = std::get<Chord>(voice->events()[1]);
    EXPECT_EQ(c1.notes[0].pitch, pitch_d4());
    EXPECT_EQ(c1.notes[1].pitch, pitch_e4());
    EXPECT_EQ(c2.notes[0].pitch, pitch_d4());
    EXPECT_EQ(c2.notes[1].pitch, pitch_e4());
    EXPECT_TRUE(c1.notes[0].tied_to_next);
    EXPECT_EQ(c1.notes[0].id, tied_cn_id);
    EXPECT_EQ(c2.notes[0].id, next_cn_id);
  }
}

TEST(CommandTest, MoveNoteheadTiedChainBoundaryFailsAtomically) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // B8 tied to B8. Moving the chain up would push both to C9, whose sounding
  // pitch is MIDI 120 -- in range, but stepping again is not; this test uses
  // the G9 case to make the boundary unambiguous: G9 up is A9 (MIDI 129),
  // out of sounding range, so the whole chain must fail atomically.
  const SpelledPitch     g9      = *SpelledPitch::create(Letter::kG, 9);
  const VoiceEvent       tied    = make_note(g9, quarter(), true);
  const NotationEntityId tied_id = graphscore::event_id(tied);
  ASSERT_TRUE(voice->append(tied).ok());
  ASSERT_TRUE(voice->append(make_note(g9, quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent before = *voice;

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_id,
      NoteheadStepDirection::kUp);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(*voice, before);
}

TEST(CommandTest, MoveNoteheadIncompleteVoicePreservesRhythm) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // A single quarter note in a 4/4 measure, deliberately NOT normalized:
  // the voice is rhythmically incomplete. Moving the notehead must change
  // only its pitch and must not materialize unrelated trailing rests.
  const VoiceEvent       note    = make_note(pitch_c4(), quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), note_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    ASSERT_EQ(voice->events().size(), 1u);
    const Note& moved = std::get<Note>(voice->events()[0]);
    EXPECT_EQ(moved.pitch, pitch_d4());
    EXPECT_EQ(moved.id, note_id);
    EXPECT_EQ(moved.duration.resolved(), quarter().resolved());
  }

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_EQ(voice->events().size(), 1u);
  EXPECT_EQ(std::get<Note>(voice->events()[0]).pitch, pitch_c4());
}

TEST(CommandTest, MoveNoteheadGraceGroupOrderPreserved) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       principal_event = make_note(pitch_c4(), quarter());
  const NotationEntityId principal = graphscore::event_id(principal_event);
  ASSERT_TRUE(voice->append(principal_event).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Two grace groups on the same principal; the first is the one edited.
  const GraceGroup first_group = make_grace_group(
      principal, {GraceNote{NotationEntityId::generate(), pitch_d4(), eighth(),
                            GraceNoteType::kAcciaccatura, true}});
  const NotationEntityId first_grace_id = first_group.notes[0].id;
  const GraceGroup       second_group   = make_grace_group(
      principal, {GraceNote{NotationEntityId::generate(), pitch_g4(), eighth(),
                            GraceNoteType::kAcciaccatura, true}});
  ASSERT_TRUE(voice->add_grace_group(first_group).ok());
  ASSERT_TRUE(voice->add_grace_group(second_group).ok());

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), first_grace_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    ASSERT_EQ(voice->grace_groups().size(), 2u);
    // Order preserved: the edited group is still first, not re-appended.
    EXPECT_EQ(voice->grace_groups()[0].id, first_group.id);
    EXPECT_EQ(voice->grace_groups()[1].id, second_group.id);
    EXPECT_EQ(voice->grace_groups()[0].notes[0].pitch, pitch_e4());
    EXPECT_EQ(voice->grace_groups()[1].notes[0].pitch, pitch_g4());
  }

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_EQ(voice->grace_groups().size(), 2u);
  EXPECT_EQ(voice->grace_groups()[0].id, first_group.id);
  EXPECT_EQ(voice->grace_groups()[1].id, second_group.id);
  EXPECT_EQ(voice->grace_groups()[0].notes[0].pitch, pitch_d4());
}

TEST(CommandTest, NoteheadMoveScopeIsSingleMeasureForALocalNotehead) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent       note    = make_note(pitch_c4(), quarter());
  const NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NoteheadItem item{fx.node_id, fx.track_id, fx.stave_id,
                          *Voice::create(1), note_id};
  const std::optional<NoteheadMoveScope> scope =
      notehead_move_scope(fx.project, item);
  ASSERT_TRUE(scope.has_value());
  EXPECT_EQ(scope->first_measure, 0u);
  EXPECT_EQ(scope->last_measure, 0u);
}

TEST(CommandTest, NoteheadMoveScopeSpansACrossMeasureTieChain) {
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
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());

  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)},
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto tl = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(tl.has_value());
  node->set_timeline(std::move(*tl));
  const Rational node_end = Rational(2);
  ASSERT_TRUE(voice->normalize(node_end).ok());

  // Selecting either chain endpoint yields the full two-measure scope.
  const NoteheadItem first{fx.node_id, fx.track_id, fx.stave_id,
                           *Voice::create(1), tied_id};
  const std::optional<NoteheadMoveScope> first_scope =
      notehead_move_scope(fx.project, first);
  ASSERT_TRUE(first_scope.has_value());
  EXPECT_EQ(first_scope->first_measure, 0u);
  EXPECT_EQ(first_scope->last_measure, 1u);
}

TEST(CommandTest, NoteheadMoveScopeStaleNoteheadIsNull) {
  auto               fx = make_notation_setup();
  const NoteheadItem item{fx.node_id, fx.track_id, fx.stave_id,
                          *Voice::create(1), NotationEntityId::generate()};
  EXPECT_FALSE(notehead_move_scope(fx.project, item).has_value());
}

// =========================================================================
// MoveNoteheadCommand — duplicate-pitch chord tie traversal (M5-phase-20)
// =========================================================================

TEST(CommandTest,
     MoveNoteheadIncomingDuplicatePitchChordTieMovesCarryingDuplicateOnly) {
  // A previous chord holds two same-pitch noteheads (C#4, C#4), but only the
  // SECOND duplicate carries the tie. Selecting the incoming (tied-to)
  // notehead in the following chord must step that carrying duplicate -- and
  // its continuation -- while leaving the untied duplicate in place, and must
  // preserve every accidental. Pitch-first lookup would pick the untied
  // first duplicate and sever the tie instead.
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const SpelledPitch cs4 =
      *SpelledPitch::create(Letter::kC, 4, Accidental::kSharp);
  const SpelledPitch ds4 =
      *SpelledPitch::create(Letter::kD, 4, Accidental::kSharp);

  const ChordNote        untied_dup{NotationEntityId::generate(), cs4, false};
  const ChordNote        tied_dup{NotationEntityId::generate(), cs4, true};
  const NotationEntityId tied_cn_id   = tied_dup.id;
  const NotationEntityId untied_cn_id = untied_dup.id;
  const Chord            first = make_chord(quarter(), {untied_dup, tied_dup});
  ASSERT_TRUE(voice->append(first).ok());

  const Note             continuation    = make_note(cs4, quarter());
  const NotationEntityId continuation_id = continuation.id;
  ASSERT_TRUE(voice->append(continuation).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), continuation_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    const Chord& c = std::get<Chord>(voice->events()[0]);
    const Note&  n = std::get<Note>(voice->events()[1]);
    EXPECT_EQ(c.notes[0].id, untied_cn_id);
    EXPECT_EQ(c.notes[0].pitch, cs4);  // untied duplicate did not move
    EXPECT_EQ(c.notes[1].id, tied_cn_id);
    EXPECT_EQ(c.notes[1].pitch, ds4);  // carrying duplicate stepped
    EXPECT_TRUE(c.notes[1].tied_to_next);
    EXPECT_EQ(n.id, continuation_id);
    EXPECT_EQ(n.pitch, ds4);  // continuation stepped with its tie partner
  }

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(std::get<Chord>(voice->events()[0]).notes[1].pitch, cs4);
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch, cs4);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(std::get<Chord>(voice->events()[0]).notes[1].pitch, ds4);
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch, ds4);
}

TEST(CommandTest, MoveNoteheadOutgoingDuplicatePitchChordTieMovesTiedPairOnly) {
  // A chord holds two same-pitch noteheads (C4, C4); only the FIRST carries
  // the tie into the following note. Selecting the carrying notehead must
  // step it and its continuation, while the untied duplicate stays in place.
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const ChordNote tied_dup{NotationEntityId::generate(), pitch_c4(), true};
  const NotationEntityId tied_cn_id = tied_dup.id;
  const ChordNote untied_dup{NotationEntityId::generate(), pitch_c4(), false};
  const NotationEntityId untied_cn_id = untied_dup.id;
  const Chord            first = make_chord(quarter(), {tied_dup, untied_dup});
  ASSERT_TRUE(voice->append(first).ok());

  const Note             continuation    = make_note(pitch_c4(), quarter());
  const NotationEntityId continuation_id = continuation.id;
  ASSERT_TRUE(voice->append(continuation).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), tied_cn_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  {
    const Chord& c = std::get<Chord>(voice->events()[0]);
    const Note&  n = std::get<Note>(voice->events()[1]);
    EXPECT_EQ(c.notes[0].id, tied_cn_id);
    EXPECT_EQ(c.notes[0].pitch, pitch_d4());  // carrying duplicate stepped
    EXPECT_TRUE(c.notes[0].tied_to_next);
    EXPECT_EQ(c.notes[1].id, untied_cn_id);
    EXPECT_EQ(c.notes[1].pitch, pitch_c4());  // untied duplicate untouched
    EXPECT_EQ(n.id, continuation_id);
    EXPECT_EQ(n.pitch, pitch_d4());  // continuation stepped
  }

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(std::get<Chord>(voice->events()[0]).notes[0].pitch, pitch_c4());
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch, pitch_c4());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(std::get<Chord>(voice->events()[0]).notes[0].pitch, pitch_d4());
  EXPECT_EQ(std::get<Note>(voice->events()[1]).pitch, pitch_d4());
}

TEST(CommandTest, NoteheadMoveScopeMatchesDuplicatePitchChordTraversal) {
  // Cross-measure duplicate-pitch chord tie: measure 0 ends with a two-C4
  // chord where only the SECOND duplicate ties into measure 1's C4. The
  // scope of the incoming notehead must span both measures, exactly matching
  // the noteheads MoveNoteheadCommand steps (the carrying duplicate and its
  // continuation, not the untied duplicate).
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  const ChordNote untied_dup{NotationEntityId::generate(), pitch_c4(), false};
  const ChordNote tied_dup{NotationEntityId::generate(), pitch_c4(), true};
  const NotationEntityId tied_cn_id = tied_dup.id;
  const Chord            chord = make_chord(quarter(), {untied_dup, tied_dup});
  ASSERT_TRUE(voice->append(chord).ok());
  const Note             continuation    = make_note(pitch_c4(), quarter());
  const NotationEntityId continuation_id = continuation.id;
  ASSERT_TRUE(voice->append(continuation).ok());

  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)},
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto tl = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(tl.has_value());
  node->set_timeline(std::move(*tl));
  ASSERT_TRUE(voice->normalize(Rational(2)).ok());

  // Selecting the incoming continuation notehead yields the full two-measure
  // scope (the carrying duplicate is in measure 0, the continuation in 1).
  const NoteheadItem incoming{fx.node_id, fx.track_id, fx.stave_id,
                              *Voice::create(1), continuation_id};
  const std::optional<NoteheadMoveScope> scope =
      notehead_move_scope(fx.project, incoming);
  ASSERT_TRUE(scope.has_value());
  EXPECT_EQ(scope->first_measure, 0u);
  EXPECT_EQ(scope->last_measure, 1u);

  // The command walks the same component: carrying duplicate + continuation
  // step, the untied duplicate stays put.
  auto cmd = std::make_unique<MoveNoteheadCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), continuation_id,
      NoteheadStepDirection::kUp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Chord& c = std::get<Chord>(voice->events()[3]);
  const Note&  n = std::get<Note>(voice->events()[4]);
  EXPECT_EQ(c.notes[0].pitch, pitch_c4());  // untied duplicate untouched
  EXPECT_EQ(c.notes[1].id, tied_cn_id);
  EXPECT_EQ(c.notes[1].pitch, pitch_d4());  // carrying duplicate stepped
  EXPECT_EQ(n.id, continuation_id);
  EXPECT_EQ(n.pitch, pitch_d4());
}
