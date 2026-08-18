// SPDX-License-Identifier: Apache-2.0

#include "note_entry_test_support.hpp"

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

namespace {
using note_entry_test::append_quarter_note;
using note_entry_test::append_whole_rest;
using note_entry_test::armed;
using note_entry_test::Fixture;
using note_entry_test::grace_note;
using note_entry_test::measure;

// ---- make_move_notehead_command (M5-phase-20) ------------------------------

TEST(NoteEntryTest, MoveNoteheadCommandValidSingleNotehead) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};

  auto cmd = make_move_notehead_command(fixture.project, item,
                                        NoteheadStepDirection::kUp);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const Note& moved = std::get<Note>(fixture.voice().events().front());
  EXPECT_EQ(moved.id, original.id);
  EXPECT_EQ(moved.pitch, *SpelledPitch::create(Letter::kD, 4));
}

TEST(NoteEntryTest, MoveNoteheadCommandStaleNoteheadReturnsNull) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), NotationEntityId::generate()};
  EXPECT_EQ(make_move_notehead_command(fixture.project, item,
                                       NoteheadStepDirection::kUp),
            nullptr);
}

TEST(NoteEntryTest, MoveNoteheadCommandWrongKindReturnsNull) {
  Fixture    fixture;
  const Rest original = append_whole_rest(fixture);
  // A top-level Rest id is not a notehead identity.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  EXPECT_EQ(make_move_notehead_command(fixture.project, item,
                                       NoteheadStepDirection::kUp),
            nullptr);
}

// ---- make_step_accidental_command (M5-phase-21) ----------------------------

TEST(NoteEntryTest, StepAccidentalCommandValidSingleNotehead) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};

  auto cmd = make_step_accidental_command(fixture.project, item,
                                          AccidentalStepDirection::kRaise);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const Note& stepped = std::get<Note>(fixture.voice().events().front());
  EXPECT_EQ(stepped.id, original.id);
  EXPECT_EQ(stepped.pitch,
            *SpelledPitch::create(Letter::kC, 4, Accidental::kSharp));
}

TEST(NoteEntryTest, StepAccidentalCommandStaleNoteheadReturnsNull) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), NotationEntityId::generate()};
  EXPECT_EQ(make_step_accidental_command(fixture.project, item,
                                         AccidentalStepDirection::kRaise),
            nullptr);
}

TEST(NoteEntryTest, StepAccidentalCommandWrongKindReturnsNull) {
  Fixture    fixture;
  const Rest original = append_whole_rest(fixture);
  // A top-level Rest id is not a notehead identity.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  EXPECT_EQ(make_step_accidental_command(fixture.project, item,
                                         AccidentalStepDirection::kLower),
            nullptr);
}

// ---- make_delete_notehead_command (M5-phase-22) -----------------------------

TEST(NoteEntryTest, DeleteNoteheadReplacesTheEventWithANormalizedRest) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
  const Rest& deleted = std::get<Rest>(fixture.voice().events().front());
  EXPECT_EQ(deleted.id, original.id);
  EXPECT_EQ(deleted.duration, original.duration);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()), original);
  ASSERT_TRUE(command->redo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
}

TEST(NoteEntryTest, DeleteChordNoteheadContractsTwoNoteChordToANote) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  append_whole_rest(fixture);
  const Chord original =
      make_chord(*Duration::create(NoteValue::kQuarter, 0),
                 {{NotationEntityId::generate(), c4, false},
                  {NotationEntityId::generate(), e4, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.notes[0].id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice().events()[1]));
  const Note& remaining = std::get<Note>(fixture.voice().events()[1]);
  EXPECT_EQ(remaining.id, original.notes[1].id);
  EXPECT_EQ(remaining.pitch, e4);
  ASSERT_TRUE(command->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events()[1]), original);
  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice().events()[1]));
  EXPECT_EQ(std::get<Note>(fixture.voice().events()[1]).id,
            original.notes[1].id);
}

TEST(NoteEntryTest, DeleteSelectionUsesPriorOnsetOrCaretAtFirstOnset) {
  Fixture    fixture;
  const Note first =
      append_quarter_note(fixture, *SpelledPitch::create(Letter::kC, 4));
  const Note second =
      append_quarter_note(fixture, *SpelledPitch::create(Letter::kD, 4));
  fixture.normalize_voice();
  const NoteheadItem second_item{fixture.node_id, fixture.track(),
                                 fixture.stave_id(), *Voice::create(1),
                                 second.id};
  const auto         prior =
      selection_after_notehead_delete(fixture.project, second_item);
  ASSERT_TRUE(prior.has_value());
  const auto* prior_set = std::get_if<graphscore::NoteheadSet>(&*prior);
  ASSERT_NE(prior_set, nullptr);
  ASSERT_EQ(prior_set->items().size(), 1u);
  EXPECT_EQ(prior_set->items().front().entity, first.id);

  const NoteheadItem first_item{fixture.node_id, fixture.track(),
                                fixture.stave_id(), *Voice::create(1),
                                first.id};
  const auto         caret =
      selection_after_notehead_delete(fixture.project, first_item);
  ASSERT_TRUE(caret.has_value());
  const auto* caret_set = std::get_if<graphscore::InsertionCaretSet>(&*caret);
  ASSERT_NE(caret_set, nullptr);
  ASSERT_EQ(caret_set->items().size(), 1u);
  EXPECT_EQ(caret_set->items().front().position, Rational(0));
}

TEST(NoteEntryTest, DeleteChordNoteheadFromThreeNoteChordKeepsTheChord) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g4 = *SpelledPitch::create(Letter::kG, 4);
  append_whole_rest(fixture);
  const Chord original =
      make_chord(*Duration::create(NoteValue::kQuarter, 0),
                 {{NotationEntityId::generate(), c4, false},
                  {NotationEntityId::generate(), e4, false},
                  {NotationEntityId::generate(), g4, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.notes[0].id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Chord>(fixture.voice().events()[1]));
  const Chord& remaining = std::get<Chord>(fixture.voice().events()[1]);
  ASSERT_EQ(remaining.notes.size(), 2u);
  EXPECT_EQ(remaining.id, original.id);
  EXPECT_EQ(remaining.notes[0].id, original.notes[1].id);
  EXPECT_EQ(remaining.notes[0].pitch, e4);
  EXPECT_EQ(remaining.notes[1].id, original.notes[2].id);
  EXPECT_EQ(remaining.notes[1].pitch, g4);
  ASSERT_TRUE(command->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events()[1]), original);
}

TEST(NoteEntryTest, DeleteNoteheadPriorOnsetStaysInTheSameVoice) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4 = *SpelledPitch::create(Letter::kD, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  // Voice 1: C4 then D4. Voice 2: E4, which must never be picked as the
  // prior onset for a voice-1 deletion.
  const Note first       = append_quarter_note(fixture, c4, 1);
  const Note second      = append_quarter_note(fixture, d4, 1);
  const Note voice2_note = append_quarter_note(fixture, e4, 2);
  fixture.normalize_voice(1);
  fixture.normalize_voice(2);

  const NoteheadItem second_item{fixture.node_id, fixture.track(),
                                 fixture.stave_id(), *Voice::create(1),
                                 second.id};
  const auto         prior =
      selection_after_notehead_delete(fixture.project, second_item);
  ASSERT_TRUE(prior.has_value());
  const auto* prior_set = std::get_if<graphscore::NoteheadSet>(&*prior);
  ASSERT_NE(prior_set, nullptr);
  ASSERT_EQ(prior_set->items().size(), 1u);
  EXPECT_EQ(prior_set->items().front().entity, first.id);
  EXPECT_EQ(prior_set->items().front().stave, fixture.stave_id());
  EXPECT_EQ(prior_set->items().front().voice, *Voice::create(1));

  auto command = make_delete_notehead_command(fixture.project, second_item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(fixture.voice(1).events()[1]));
  // Voice 2 is untouched by the delete.
  EXPECT_EQ(std::get<Note>(fixture.voice(2).events().front()), voice2_note);
}

TEST(NoteEntryTest, DeleteTiedTargetClearsTheIncomingTie) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(c4, *Duration::create(NoteValue::kQuarter, 0), true);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const Note second = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), second.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events()[1]));
  // The prior note's tie now points at a Rest, so the incoming tie must be
  // cleared for the voice to stay valid.
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice().events().front()));
  EXPECT_FALSE(std::get<Note>(fixture.voice().events().front()).tied_to_next);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  EXPECT_TRUE(std::get<Note>(fixture.voice().events().front()).tied_to_next);
  EXPECT_EQ(std::get<Note>(fixture.voice().events()[1]).id, second.id);
}

TEST(NoteEntryTest, DeleteGraceNoteheadRemovesItFromItsGroup) {
  Fixture            fixture;
  const SpelledPitch c4        = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4        = *SpelledPitch::create(Letter::kD, 4);
  const Note         principal = append_quarter_note(fixture, c4);
  append_quarter_note(fixture, d4);
  const GraceGroup group = make_grace_group(
      principal.id, {GraceNote{NotationEntityId::generate(), d4,
                               *Duration::create(NoteValue::kEighth, 0),
                               GraceNoteType::kAcciaccatura, true}});
  const NotationEntityId grace_id = group.notes[0].id;
  ASSERT_TRUE(fixture.voice().add_grace_group(group).ok());
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), grace_id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());
  // Deleting the group's only note removes the group entirely.
  EXPECT_TRUE(fixture.voice().grace_groups().empty());

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().grace_groups().size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0].notes[0].id, grace_id);
}

// ---- M5-phase-22 review fix regressions --------------------------------
//
// Reference normalization when a sounding event becomes a Rest, in-place
// grace-note removal preserving group order/identity, and the selection
// recovery branches for every preceding/deleted event variant.

TEST(NoteEntryTest, DeleteNoteheadRemovesSlursTouchingTheDeletedEvent) {
  Fixture            fixture;
  const SpelledPitch c4     = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4     = *SpelledPitch::create(Letter::kD, 4);
  const SpelledPitch e4     = *SpelledPitch::create(Letter::kE, 4);
  const Note         first  = append_quarter_note(fixture, c4);
  const Note         second = append_quarter_note(fixture, d4);
  const Note         third  = append_quarter_note(fixture, e4);
  fixture.normalize_voice();

  const Slur unaffected = make_slur(first.id, third.id);
  const Slur affected   = make_slur(second.id, third.id);
  ASSERT_TRUE(fixture.voice().add_slur(unaffected).ok());
  ASSERT_TRUE(fixture.voice().add_slur(affected).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), second.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  // Only the slur touching the deleted event is removed; the other keeps its
  // id and order.
  ASSERT_EQ(fixture.voice().slurs().size(), 1u);
  EXPECT_EQ(fixture.voice().slurs()[0], unaffected);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().slurs().size(), 2u);
  EXPECT_EQ(fixture.voice().slurs()[0], unaffected);
  EXPECT_EQ(fixture.voice().slurs()[1], affected);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().slurs().size(), 1u);
  EXPECT_EQ(fixture.voice().slurs()[0], unaffected);
}

TEST(NoteEntryTest, DeleteNoteheadKeepsContiguousBeamOverrideAfterEdgeDelete) {
  Fixture        fixture;
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const Note     a = make_note(*SpelledPitch::create(Letter::kC, 4), eighth);
  const Note     b = make_note(*SpelledPitch::create(Letter::kD, 4), eighth);
  const Note     c = make_note(*SpelledPitch::create(Letter::kE, 4), eighth);
  ASSERT_TRUE(fixture.voice().append(a).ok());
  ASSERT_TRUE(fixture.voice().append(b).ok());
  ASSERT_TRUE(fixture.voice().append(c).ok());
  fixture.normalize_voice();

  const BeamOverride override =
      make_beam_override(BeamOverride::Kind::kJoin, {a.id, b.id, c.id});
  ASSERT_TRUE(fixture.voice().add_beam_override(override).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), c.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  // Deleting the run's final event keeps the still-contiguous reduced run
  // with the same override id.
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].id, override.id);
  ASSERT_EQ(fixture.voice().beam_overrides()[0].events.size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[0], a.id);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[1], b.id);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], override);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].id, override.id);
  ASSERT_EQ(fixture.voice().beam_overrides()[0].events.size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[0], a.id);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[1], b.id);
}

TEST(NoteEntryTest, DeleteNoteheadRemovesBrokenBeamOverridePreservingOrder) {
  Fixture        fixture;
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const Note     a = make_note(*SpelledPitch::create(Letter::kC, 4), eighth);
  const Note     b = make_note(*SpelledPitch::create(Letter::kD, 4), eighth);
  const Note     c = make_note(*SpelledPitch::create(Letter::kE, 4), eighth);
  const Note     d = make_note(*SpelledPitch::create(Letter::kF, 4), eighth);
  const Note     e = make_note(*SpelledPitch::create(Letter::kG, 4), eighth);
  ASSERT_TRUE(fixture.voice().append(a).ok());
  ASSERT_TRUE(fixture.voice().append(b).ok());
  ASSERT_TRUE(fixture.voice().append(c).ok());
  ASSERT_TRUE(fixture.voice().append(d).ok());
  ASSERT_TRUE(fixture.voice().append(e).ok());
  fixture.normalize_voice();

  const BeamOverride first_override =
      make_beam_override(BeamOverride::Kind::kJoin, {a.id, b.id});
  const BeamOverride second_override =
      make_beam_override(BeamOverride::Kind::kBreak, {c.id, d.id, e.id});
  ASSERT_TRUE(fixture.voice().add_beam_override(first_override).ok());
  ASSERT_TRUE(fixture.voice().add_beam_override(second_override).ok());

  // Delete d (the middle of the second run): the surviving {c, e} are no
  // longer adjacent, so the second override is removed while the first keeps
  // its id and position.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), d.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], first_override);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], first_override);
  EXPECT_EQ(fixture.voice().beam_overrides()[1], second_override);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], first_override);
}

TEST(NoteEntryTest, DeleteNoteheadKeepsReplacedOverrideIdentityAndPrecedence) {
  Fixture        fixture;
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const Note     a = make_note(*SpelledPitch::create(Letter::kC, 4), eighth);
  const Note     b = make_note(*SpelledPitch::create(Letter::kD, 4), eighth);
  const Note     c = make_note(*SpelledPitch::create(Letter::kE, 4), eighth);
  ASSERT_TRUE(fixture.voice().append(a).ok());
  ASSERT_TRUE(fixture.voice().append(b).ok());
  ASSERT_TRUE(fixture.voice().append(c).ok());
  fixture.normalize_voice();

  // Overlapping overrides: the join (added first) covers a-b-c, the break
  // (added second) covers b-c, so the break wins on b-c. Deleting a reduces
  // the join's run to {b, c} in place — it must keep its id and stay ahead
  // of the break through execute/undo/redo.
  const BeamOverride join =
      make_beam_override(BeamOverride::Kind::kJoin, {a.id, b.id, c.id});
  const BeamOverride brk =
      make_beam_override(BeamOverride::Kind::kBreak, {b.id, c.id});
  ASSERT_TRUE(fixture.voice().add_beam_override(join).ok());
  ASSERT_TRUE(fixture.voice().add_beam_override(brk).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), a.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  ASSERT_EQ(fixture.voice().beam_overrides().size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].id, join.id);
  ASSERT_EQ(fixture.voice().beam_overrides()[0].events.size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[0], b.id);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[1], c.id);
  EXPECT_EQ(fixture.voice().beam_overrides()[1], brk);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], join);
  EXPECT_EQ(fixture.voice().beam_overrides()[1], brk);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].id, join.id);
  ASSERT_EQ(fixture.voice().beam_overrides()[0].events.size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[0], b.id);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[1], c.id);
  EXPECT_EQ(fixture.voice().beam_overrides()[1], brk);
}

TEST(NoteEntryTest, DeleteNoteheadRemovesGraceGroupWhosePrincipalIsDeleted) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4 = *SpelledPitch::create(Letter::kD, 4);
  const Note         c  = append_quarter_note(fixture, c4);
  const Note         d  = append_quarter_note(fixture, d4);
  fixture.normalize_voice();

  const GraceGroup g1 = make_grace_group(c.id, {grace_note(c4)});
  const GraceGroup g2 = make_grace_group(d.id, {grace_note(d4)});
  ASSERT_TRUE(fixture.voice().add_grace_group(g1).ok());
  ASSERT_TRUE(fixture.voice().add_grace_group(g2).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), c.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  ASSERT_EQ(fixture.voice().grace_groups().size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], g2);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().grace_groups().size(), 2u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], g1);
  EXPECT_EQ(fixture.voice().grace_groups()[1], g2);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().grace_groups().size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], g2);
}

TEST(NoteEntryTest, DeleteNoteheadNormalizesSlurBeamAndGraceGroupTogether) {
  Fixture        fixture;
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const Note     a = make_note(*SpelledPitch::create(Letter::kC, 4), eighth);
  const Note     b = make_note(*SpelledPitch::create(Letter::kD, 4), eighth);
  const Note     c = make_note(*SpelledPitch::create(Letter::kE, 4), eighth);
  const Note     d = make_note(*SpelledPitch::create(Letter::kF, 4), eighth);
  const Note     e = make_note(*SpelledPitch::create(Letter::kG, 4), eighth);
  ASSERT_TRUE(fixture.voice().append(a).ok());
  ASSERT_TRUE(fixture.voice().append(b).ok());
  ASSERT_TRUE(fixture.voice().append(c).ok());
  ASSERT_TRUE(fixture.voice().append(d).ok());
  ASSERT_TRUE(fixture.voice().append(e).ok());
  fixture.normalize_voice();

  // b carries a slur endpoint, a beam membership, and a grace-group principal
  // at once; unaffected records of each family are interleaved with them.
  const SpelledPitch a3              = *SpelledPitch::create(Letter::kA, 3);
  const SpelledPitch b3              = *SpelledPitch::create(Letter::kB, 3);
  const Slur         affected_slur   = make_slur(a.id, b.id);
  const Slur         unaffected_slur = make_slur(d.id, e.id);
  const BeamOverride affected_beam =
      make_beam_override(BeamOverride::Kind::kJoin, {a.id, b.id, c.id});
  const BeamOverride unaffected_beam =
      make_beam_override(BeamOverride::Kind::kBreak, {d.id, e.id});
  const GraceGroup unaffected_group = make_grace_group(a.id, {grace_note(b3)});
  const GraceGroup affected_group   = make_grace_group(b.id, {grace_note(a3)});

  ASSERT_TRUE(fixture.voice().add_slur(affected_slur).ok());
  ASSERT_TRUE(fixture.voice().add_slur(unaffected_slur).ok());
  ASSERT_TRUE(fixture.voice().add_beam_override(affected_beam).ok());
  ASSERT_TRUE(fixture.voice().add_beam_override(unaffected_beam).ok());
  ASSERT_TRUE(fixture.voice().add_grace_group(unaffected_group).ok());
  ASSERT_TRUE(fixture.voice().add_grace_group(affected_group).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), b.id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  // Every affected record is gone; every unaffected record keeps its id and
  // relative order.
  ASSERT_EQ(fixture.voice().slurs().size(), 1u);
  EXPECT_EQ(fixture.voice().slurs()[0], unaffected_slur);
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], unaffected_beam);
  ASSERT_EQ(fixture.voice().grace_groups().size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], unaffected_group);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().slurs().size(), 2u);
  EXPECT_EQ(fixture.voice().slurs()[0], affected_slur);
  EXPECT_EQ(fixture.voice().slurs()[1], unaffected_slur);
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], affected_beam);
  EXPECT_EQ(fixture.voice().beam_overrides()[1], unaffected_beam);
  ASSERT_EQ(fixture.voice().grace_groups().size(), 2u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], unaffected_group);
  EXPECT_EQ(fixture.voice().grace_groups()[1], affected_group);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().slurs().size(), 1u);
  EXPECT_EQ(fixture.voice().slurs()[0], unaffected_slur);
  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0], unaffected_beam);
  ASSERT_EQ(fixture.voice().grace_groups().size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], unaffected_group);
}

TEST(NoteEntryTest, DeleteGraceNotePreservesGroupOrderAndIdentity) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4 = *SpelledPitch::create(Letter::kD, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch f4 = *SpelledPitch::create(Letter::kF, 4);
  const SpelledPitch g4 = *SpelledPitch::create(Letter::kG, 4);
  const Note         c  = append_quarter_note(fixture, c4);
  const Note         d  = append_quarter_note(fixture, d4);
  const Note         e  = append_quarter_note(fixture, e4);
  fixture.normalize_voice();

  const GraceGroup g1 =
      make_grace_group(c.id, {grace_note(f4), grace_note(g4)});
  const GraceGroup       g2         = make_grace_group(d.id, {grace_note(c4)});
  const GraceGroup       g3         = make_grace_group(e.id, {grace_note(d4)});
  const NotationEntityId removed_id = g1.notes[1].id;
  ASSERT_TRUE(fixture.voice().add_grace_group(g1).ok());
  ASSERT_TRUE(fixture.voice().add_grace_group(g2).ok());
  ASSERT_TRUE(fixture.voice().add_grace_group(g3).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), removed_id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  // g1 survives in place with its remaining note; g2 and g3 are untouched and
  // keep their positions.
  ASSERT_EQ(fixture.voice().grace_groups().size(), 3u);
  EXPECT_EQ(fixture.voice().grace_groups()[0].id, g1.id);
  ASSERT_EQ(fixture.voice().grace_groups()[0].notes.size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0].notes[0].id, g1.notes[0].id);
  EXPECT_EQ(fixture.voice().grace_groups()[0].principal_event, c.id);
  EXPECT_EQ(fixture.voice().grace_groups()[1], g2);
  EXPECT_EQ(fixture.voice().grace_groups()[2], g3);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().grace_groups().size(), 3u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], g1);
  EXPECT_EQ(fixture.voice().grace_groups()[1], g2);
  EXPECT_EQ(fixture.voice().grace_groups()[2], g3);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().grace_groups().size(), 3u);
  EXPECT_EQ(fixture.voice().grace_groups()[0].id, g1.id);
  ASSERT_EQ(fixture.voice().grace_groups()[0].notes.size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0].notes[0].id, g1.notes[0].id);
}

TEST(NoteEntryTest, DeleteGraceNoteRemovesSoleNoteGroupPreservingOrder) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4 = *SpelledPitch::create(Letter::kD, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch f4 = *SpelledPitch::create(Letter::kF, 4);
  const SpelledPitch g4 = *SpelledPitch::create(Letter::kG, 4);
  const Note         c  = append_quarter_note(fixture, c4);
  const Note         d  = append_quarter_note(fixture, d4);
  const Note         e  = append_quarter_note(fixture, e4);
  fixture.normalize_voice();

  const GraceGroup g1 = make_grace_group(c.id, {grace_note(f4)});
  const GraceGroup g2 = make_grace_group(d.id, {grace_note(g4)});
  const GraceGroup g3 = make_grace_group(e.id, {grace_note(d4)});
  ASSERT_TRUE(fixture.voice().add_grace_group(g1).ok());
  ASSERT_TRUE(fixture.voice().add_grace_group(g2).ok());
  ASSERT_TRUE(fixture.voice().add_grace_group(g3).ok());

  // Removing the only note of the middle group removes the group in place;
  // the remaining groups keep their ids and relative order.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), g2.notes[0].id};
  auto command = make_delete_notehead_command(fixture.project, item);
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->execute(fixture.project).ok());

  ASSERT_EQ(fixture.voice().grace_groups().size(), 2u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], g1);
  EXPECT_EQ(fixture.voice().grace_groups()[1], g3);

  ASSERT_TRUE(command->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().grace_groups().size(), 3u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], g1);
  EXPECT_EQ(fixture.voice().grace_groups()[1], g2);
  EXPECT_EQ(fixture.voice().grace_groups()[2], g3);

  ASSERT_TRUE(command->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().grace_groups().size(), 2u);
  EXPECT_EQ(fixture.voice().grace_groups()[0], g1);
  EXPECT_EQ(fixture.voice().grace_groups()[1], g3);
}

TEST(NoteEntryTest, DeleteSelectionPrecedingRestReturnsRestSet) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  append_quarter_note(fixture, c4);
  const Rest middle = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(middle).ok());
  const Note last = append_quarter_note(fixture, e4);
  fixture.normalize_voice();

  const NoteheadItem last_item{fixture.node_id, fixture.track(),
                               fixture.stave_id(), *Voice::create(1), last.id};
  const auto         prior =
      selection_after_notehead_delete(fixture.project, last_item);
  ASSERT_TRUE(prior.has_value());
  const auto* rest_set = std::get_if<graphscore::RestSet>(&*prior);
  ASSERT_NE(rest_set, nullptr);
  ASSERT_EQ(rest_set->items().size(), 1u);
  EXPECT_EQ(rest_set->items().front().entity, middle.id);
  EXPECT_EQ(rest_set->items().front().node, fixture.node_id);
  EXPECT_EQ(rest_set->items().front().track, fixture.track());
  EXPECT_EQ(rest_set->items().front().stave, fixture.stave_id());
  EXPECT_EQ(rest_set->items().front().voice, *Voice::create(1));
}

TEST(NoteEntryTest, DeleteSelectionPrecedingChordReturnsChordSet) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);
  const Chord original = make_chord(*Duration::create(NoteValue::kQuarter, 0),
                                    {{NotationEntityId::generate(), c, false},
                                     {NotationEntityId::generate(), e, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  const Note after = append_quarter_note(fixture, g);
  fixture.normalize_voice();

  const NoteheadItem after_item{fixture.node_id, fixture.track(),
                                fixture.stave_id(), *Voice::create(1),
                                after.id};
  const auto         prior =
      selection_after_notehead_delete(fixture.project, after_item);
  ASSERT_TRUE(prior.has_value());
  const auto* chord_set = std::get_if<graphscore::ChordSet>(&*prior);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items().front().entity, original.id);
  EXPECT_EQ(chord_set->items().front().node, fixture.node_id);
  EXPECT_EQ(chord_set->items().front().track, fixture.track());
  EXPECT_EQ(chord_set->items().front().stave, fixture.stave_id());
  EXPECT_EQ(chord_set->items().front().voice, *Voice::create(1));
}

TEST(NoteEntryTest, DeleteSelectionGraceNoteAtLaterOnsetSelectsPriorEvent) {
  Fixture            fixture;
  const SpelledPitch c4        = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4        = *SpelledPitch::create(Letter::kD, 4);
  const Note         first     = append_quarter_note(fixture, c4);
  const Note         principal = append_quarter_note(fixture, d4);
  fixture.normalize_voice();
  const GraceGroup group = make_grace_group(principal.id, {grace_note(d4)});
  ASSERT_TRUE(fixture.voice().add_grace_group(group).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), group.notes[0].id};
  const auto prior = selection_after_notehead_delete(fixture.project, item);
  ASSERT_TRUE(prior.has_value());
  const auto* note_set = std::get_if<graphscore::NoteheadSet>(&*prior);
  ASSERT_NE(note_set, nullptr);
  ASSERT_EQ(note_set->items().size(), 1u);
  EXPECT_EQ(note_set->items().front().entity, first.id);
  EXPECT_EQ(note_set->items().front().stave, fixture.stave_id());
  EXPECT_EQ(note_set->items().front().voice, *Voice::create(1));
}

TEST(NoteEntryTest, DeleteSelectionGraceNoteAtFirstOnsetReturnsCaret) {
  Fixture            fixture;
  const SpelledPitch c4        = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4        = *SpelledPitch::create(Letter::kD, 4);
  const Note         principal = append_quarter_note(fixture, c4);
  append_quarter_note(fixture, d4);
  fixture.normalize_voice();
  const GraceGroup group = make_grace_group(principal.id, {grace_note(c4)});
  ASSERT_TRUE(fixture.voice().add_grace_group(group).ok());

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), group.notes[0].id};
  const auto prior = selection_after_notehead_delete(fixture.project, item);
  ASSERT_TRUE(prior.has_value());
  const auto* caret_set = std::get_if<graphscore::InsertionCaretSet>(&*prior);
  ASSERT_NE(caret_set, nullptr);
  ASSERT_EQ(caret_set->items().size(), 1u);
  EXPECT_EQ(caret_set->items().front().position, Rational(0));
  EXPECT_EQ(caret_set->items().front().stave, fixture.stave_id());
  EXPECT_EQ(caret_set->items().front().voice, *Voice::create(1));
}

}  // namespace
