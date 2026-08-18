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

// ---- Explicit voice-stream workflow: entry into a never-touched voice ----

// The composer's first click into an armed but never-touched voice must
// place the note and materialize the rest of the voice as normalized
// rests, in one undoable action -- the empty-voice path returns a
// CommandTransaction, never a bare SetEventCommand.
TEST(NoteEntryTest, FirstClickIntoAnEmptyVoiceCreatesTheStreamAndTheNote) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  ASSERT_TRUE(fixture.voice(2).events().empty());

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_NE(dynamic_cast<CommandTransaction*>(cmd.get()), nullptr);

  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  ASSERT_FALSE(fixture.voice(2).events().empty());
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice(2).events().front()));
  const Note& note = std::get<Note>(fixture.voice(2).events().front());
  EXPECT_EQ(note.pitch, pitch);
  EXPECT_EQ(note.duration.base(), NoteValue::kQuarter);
  EXPECT_EQ(fixture.voice(2).total_length(), fixture.node_end());
}

// One undo removes both the note and the whole voice: the voice returns to
// completely empty, exactly as if the composer had never clicked.
TEST(NoteEntryTest, UndoAfterVoiceCreationReturnsToCompletelyEmpty) {
  Fixture                    fixture;
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());
  ASSERT_FALSE(fixture.voice(2).events().empty());

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  EXPECT_EQ(fixture.voice(2), VoiceContent{});
}

// Redo reproduces the exact same fill and note, id for id.
TEST(NoteEntryTest, RedoAfterVoiceCreationIsIdForIdIdentical) {
  Fixture                    fixture;
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());
  const VoiceContent post_snapshot = fixture.voice(2);

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  ASSERT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(2), post_snapshot);

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  ASSERT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(2), post_snapshot);
}

// CommandHistory, not a bare execute()/undo()/redo() call, is the real
// caller in the app: one execute_new() through it must place the note and
// materialize the stream as a single undo/redo-stack entry, including the
// ordinary redo-stack-clearing interaction a fresh execute_new() triggers.
TEST(NoteEntryTest, VoiceCreationTransactionRoundTripsThroughCommandHistory) {
  Fixture                    fixture;
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(cmd), fixture.project).ok());
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  ASSERT_FALSE(fixture.voice(2).events().empty());
  const VoiceContent post_snapshot = fixture.voice(2);

  EXPECT_TRUE(history.undo(fixture.project).ok());
  EXPECT_FALSE(history.can_undo());
  EXPECT_TRUE(history.can_redo());
  EXPECT_TRUE(fixture.voice(2).events().empty());

  EXPECT_TRUE(history.redo(fixture.project).ok());
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  EXPECT_EQ(fixture.voice(2), post_snapshot);

  // A fresh execute_new() after an undo clears the redo stack, same as any
  // other command through CommandHistory.
  EXPECT_TRUE(history.undo(fixture.project).ok());
  ASSERT_TRUE(history.can_redo());
  auto second_cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(second_cmd, nullptr);
  EXPECT_TRUE(history.execute_new(std::move(second_cmd), fixture.project).ok());
  EXPECT_FALSE(history.can_redo());
  EXPECT_FALSE(fixture.voice(2).events().empty());
}

// Arming a rest duration and clicking an empty voice still materializes
// the stream: the result is a voice of normalized rests, not a no-op.
TEST(NoteEntryTest, RestEntryIntoAnEmptyVoiceStillMaterializesTheStream) {
  Fixture                    fixture;
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kRest, 2);
  auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                     fixture.track(), fixture.stave_id(),
                                     Rational(0), spec, std::nullopt);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  ASSERT_FALSE(fixture.voice(2).events().empty());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice(2).events().front()));
  EXPECT_EQ(std::get<Rest>(fixture.voice(2).events().front()).duration.base(),
            NoteValue::kHalf);
  EXPECT_EQ(fixture.voice(2).total_length(), fixture.node_end());
}

// A position that is not an onset of the empty voice's hypothetical fill
// is rejected outright, mutating nothing.
TEST(NoteEntryTest, ReturnsNullptrWhenPositionIsNotAFillOnsetInAnEmptyVoice) {
  Fixture                    fixture;
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  // node_end() == 2 (two 4/4 measures); the hypothetical fill is two whole
  // rests, at onsets 0 and 1 -- position 1/2 is neither.
  auto cmd = make_note_entry_command(
      fixture.project, fixture.node_id, fixture.track(), fixture.stave_id(),
      *Rational::create(1, 2), spec, *SpelledPitch::create(Letter::kC, 4));
  EXPECT_EQ(cmd, nullptr);
  EXPECT_TRUE(fixture.voice(2).events().empty());
}

// The documented "either succeeds completely or leaves the project
// untouched" claim is non-trivial exactly when CreateVoiceStreamCommand
// succeeds but the chained SetEventCommand then fails: node_end() == 2 (two
// 4/4 measures), so the hypothetical fill is two whole rests at onsets 0
// and 1. Arming a dotted whole (3/2) at position 1 (the second whole rest,
// the voice's last event) needs 1/2 beyond node_end() with no later event
// to cover the growth, so SetEventCommand rejects the expansion atomically.
// The whole transaction must roll back to a completely empty voice, never a
// "voice materialized but noteless" half-state.
TEST(NoteEntryTest,
     FailedChainedSetEventLeavesTheVoiceCompletelyEmptyNotHalfMaterialized) {
  Fixture fixture;
  ASSERT_TRUE(fixture.voice(2).events().empty());

  const NotePaletteState state = *NotePaletteState::create(
      NoteValue::kWhole, 1, NotePaletteEntryKind::kNote, *Voice::create(2));
  const NotePaletteEntrySpec spec  = state.next_entry_spec();
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);

  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(1), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  ASSERT_NE(dynamic_cast<CommandTransaction*>(cmd.get()), nullptr);

  EXPECT_FALSE(cmd->execute(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  EXPECT_EQ(fixture.voice(2), VoiceContent{});
}

// The non-empty path's own behavior is unchanged by this feature: it still
// returns a bare SetEventCommand, never a transaction.
TEST(NoteEntryTest, NonEmptyVoicePathStillReturnsABareSetEventCommand) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_NE(dynamic_cast<SetEventCommand*>(cmd.get()), nullptr);
  EXPECT_EQ(dynamic_cast<CommandTransaction*>(cmd.get()), nullptr);
}

// Equivalence: entering into an empty voice must be semantically identical
// to explicitly rest-filling that voice first (via the same shared
// decompose_measure_aligned_rests helper preview_note_entry itself uses)
// and then performing the ordinary non-empty entry. This is what
// guarantees the two paths can never drift apart.
TEST(NoteEntryTest,
     EmptyVoiceEntryIsSemanticallyEquivalentToExplicitFillThenEntry) {
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);

  // Path A: enter directly into the empty voice.
  Fixture fixture_a;

  auto cmd_a = make_note_entry_command(fixture_a.project, fixture_a.node_id,
                                       fixture_a.track(), fixture_a.stave_id(),
                                       Rational(0), spec, pitch);
  ASSERT_NE(cmd_a, nullptr);
  ASSERT_TRUE(cmd_a->execute(fixture_a.project).ok());

  // Path B: explicitly rest-fill the voice first with the same shared
  // helper, then perform the ordinary non-empty entry at the same onset.
  Fixture    fixture_b;
  const auto fill = decompose_measure_aligned_rests(
      *fixture_b.project.find_node(fixture_b.node_id)->timeline());
  ASSERT_TRUE(fill.has_value());
  for (const Rest& rest : *fill) {
    ASSERT_TRUE(fixture_b.voice(2).append(rest).ok());
  }
  auto cmd_b = make_note_entry_command(fixture_b.project, fixture_b.node_id,
                                       fixture_b.track(), fixture_b.stave_id(),
                                       Rational(0), spec, pitch);
  ASSERT_NE(cmd_b, nullptr);
  ASSERT_TRUE(cmd_b->execute(fixture_b.project).ok());

  // Both voices carry the same shape: the same event count, kind and
  // duration at every position. IDs necessarily differ -- each command
  // minted its own fresh ids for both the fill and the note.
  const auto& events_a = fixture_a.voice(2).events();
  const auto& events_b = fixture_b.voice(2).events();
  ASSERT_EQ(events_a.size(), events_b.size());
  for (std::size_t i = 0; i < events_a.size(); ++i) {
    EXPECT_EQ(events_a[i].index(), events_b[i].index()) << "event " << i;
    EXPECT_EQ(event_duration(events_a[i]), event_duration(events_b[i]))
        << "event " << i;
  }
  ASSERT_TRUE(std::holds_alternative<Note>(events_a.front()));
  ASSERT_TRUE(std::holds_alternative<Note>(events_b.front()));
  EXPECT_EQ(std::get<Note>(events_a.front()).pitch,
            std::get<Note>(events_b.front()).pitch);
}

// The equivalence above alone is not meaningful proof of the shared
// measure-aligned tiling: on 4/4 the measure-aligned fill and a naive
// decompose_rest(node_end()) fill are identical (two whole rests), so that
// test only proves the two paths call *a* shared helper. Re-run on three
// 3/4 measures (node_end() == 9/4), where the two fills genuinely differ
// (ThreeThreeQuarterMeasuresRespectEveryBarline, voice_content_test.cpp),
// to prove they still agree once barline alignment actually matters.
TEST(NoteEntryTest,
     EmptyVoiceEntryMatchesExplicitFillThenEntryOnThreeThreeQuarterMeasures) {
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  const std::vector<Measure> measures = {measure(3, 4), measure(3, 4),
                                         measure(3, 4)};

  Fixture      fixture_a(measures);
  const NodeId node_a = fixture_a.node_id;
  const auto   cmd_a =
      make_note_entry_command(fixture_a.project, node_a, fixture_a.track(),
                              fixture_a.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd_a, nullptr);
  ASSERT_TRUE(cmd_a->execute(fixture_a.project).ok());

  Fixture    fixture_b(measures);
  const auto fill = decompose_measure_aligned_rests(
      *fixture_b.project.find_node(fixture_b.node_id)->timeline());
  ASSERT_TRUE(fill.has_value());
  for (const Rest& rest : *fill) {
    ASSERT_TRUE(fixture_b.voice(2).append(rest).ok());
  }
  auto cmd_b = make_note_entry_command(fixture_b.project, fixture_b.node_id,
                                       fixture_b.track(), fixture_b.stave_id(),
                                       Rational(0), spec, pitch);
  ASSERT_NE(cmd_b, nullptr);
  ASSERT_TRUE(cmd_b->execute(fixture_b.project).ok());

  const auto& events_a = fixture_a.voice(2).events();
  const auto& events_b = fixture_b.voice(2).events();
  ASSERT_EQ(events_a.size(), events_b.size());
  for (std::size_t i = 0; i < events_a.size(); ++i) {
    EXPECT_EQ(events_a[i].index(), events_b[i].index()) << "event " << i;
    EXPECT_EQ(event_duration(events_a[i]), event_duration(events_b[i]))
        << "event " << i;
  }
  EXPECT_EQ(fixture_a.voice(2).total_length(), fixture_a.node_end());
}

// The composer's first click into an empty voice on a meter where the
// measure-aligned fill genuinely differs from a naive whole-span
// decomposition: three 3/4 measures (node_end() == 9/4) tile as three
// dotted-half rests when decomposed per-measure, never the
// dotted-whole-plus-dotted-half a naive decompose_rest(node_end()) call
// would produce, which would straddle the first barline. Proven through the
// full make_note_entry_command path, not just at the
// decompose_measure_aligned_rests unit level
// (ThreeThreeQuarterMeasuresRespectEveryBarline, voice_content_test.cpp).
TEST(NoteEntryTest, FirstClickOnThreeThreeQuarterMeasuresRespectsEveryBarline) {
  Fixture            fixture({measure(3, 4), measure(3, 4), measure(3, 4)});
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);

  // Arm a dotted half (3/4), exactly the first rest's own duration, so the
  // replacement is same-duration and the three-event shape is preserved
  // unchanged -- isolating the barline-alignment property from any
  // additional gap-filling machinery.
  const NotePaletteState state = *NotePaletteState::create(
      NoteValue::kHalf, 1, NotePaletteEntryKind::kNote, *Voice::create(2));
  const NotePaletteEntrySpec spec = state.next_entry_spec();
  auto                       cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  const auto& events = fixture.voice(2).events();
  ASSERT_EQ(events.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<Note>(events[0]));
  EXPECT_EQ(std::get<Note>(events[0]).duration,
            *Duration::create(NoteValue::kHalf, 1));

  // No event crosses a measure boundary: each of the three events covers
  // exactly one 3/4 measure, so cumulative duration lands on every barline.
  Rational cumulative(0);
  for (const auto& ev : events) {
    EXPECT_EQ(event_duration(ev).resolved(), *Rational::create(3, 4));
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_EQ(cumulative, fixture.node_end());
}

// ---- Isolation extended to the voice-creation path ----

// Creating voice 2 leaves voices 1, 3, and 4 -- already-populated or still
// untouched -- byte-for-byte unchanged. Extends OtherVoicesUnaffected to
// the creation path.
TEST(NoteEntryTest, VoiceCreationLeavesOtherVoicesUnaffected) {
  Fixture            fixture;
  const SpelledPitch c               = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d               = *SpelledPitch::create(Letter::kD, 4);
  const Note         voice1_original = append_quarter_note(fixture, c, 1);
  fixture.normalize_voice(1);
  const VoiceContent voice3_before = fixture.voice(3);
  const VoiceContent voice4_before = fixture.voice(4);
  ASSERT_TRUE(fixture.voice(2).events().empty());

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, d);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice(1).events().front()));
  EXPECT_EQ(std::get<Note>(fixture.voice(1).events().front()), voice1_original);
  EXPECT_EQ(fixture.voice(3), voice3_before);
  EXPECT_EQ(fixture.voice(4), voice4_before);
  EXPECT_FALSE(fixture.voice(2).events().empty());

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice(1).events().front()));
  EXPECT_EQ(std::get<Note>(fixture.voice(1).events().front()), voice1_original);
  EXPECT_EQ(fixture.voice(3), voice3_before);
  EXPECT_EQ(fixture.voice(4), voice4_before);
}

// The armed voice is the sole write target for the creation path too:
// arming voice 2 while voice 3 also happens to be empty leaves voice 3
// empty -- creating one voice never creates or disturbs a sibling voice
// the composer did not explicitly select. Extends ArmedVoiceIsOnlyChangedVoice.
TEST(NoteEntryTest, VoiceCreationNeverMaterializesASiblingVoice) {
  Fixture fixture;
  ASSERT_TRUE(fixture.voice(2).events().empty());
  ASSERT_TRUE(fixture.voice(3).events().empty());

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd = make_note_entry_command(
      fixture.project, fixture.node_id, fixture.track(), fixture.stave_id(),
      Rational(0), spec, *SpelledPitch::create(Letter::kC, 4));
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  EXPECT_FALSE(fixture.voice(2).events().empty());
  EXPECT_TRUE(fixture.voice(3).events().empty());
}

}  // namespace
