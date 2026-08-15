// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8e-i — Duration contraction fills with rests
// =========================================================================

TEST(CommandTest, SetEventDurationContractionFillsWithNormalizedRests) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Half note at 0.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  ASSERT_EQ(voice->events().size(), 2u);  // half note + half rest

  // Replace with quarter note — gap filled by a quarter rest.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());
  // Should have quarter note + quarter rest + half rest (from original
  // normalize which filled the remainder).
  ASSERT_GE(voice->events().size(), 2u);
}

// =========================================================================
// Phase 8e-i — Remove preserves later event onsets
// =========================================================================

TEST(CommandTest, RemoveEventPreservesLaterOnsets) {
  VoiceContent voice;
  // N(q, C4) at 0, N(q, D4) at 1/4, N(q, E4) at 1/2.  Total 3/4.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_e4(), quarter())).ok());

  // Remove the D4 at 1/4.  It should be replaced with rests of the same
  // duration at position 1/4, leaving the E4 at onset 1/2.
  ASSERT_TRUE(voice.remove_event(*Rational::create(1, 4), Rational(1)).ok());

  EXPECT_EQ(voice.total_length(), Rational(1));
  // C4 note still at position 0.
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  // E4 note preserved at its original onset.  Find it by scanning.
  bool     found_e4 = false;
  Rational cumulative(0);
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == *Rational::create(1, 2)) {
      if (const auto* n = std::get_if<Note>(&ev)) {
        if (n->pitch == pitch_e4())
          found_e4 = true;
      }
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_e4);
}

TEST(CommandTest, RemovePreservesExactRationalOnsets) {
  VoiceContent voice;
  // N(q, C4) at 0, N(e, D4) at 1/4, N(q, E4) at 3/8.  Total 5/8.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_e4(), quarter())).ok());

  // Remove C4 at 0.  D4 must remain at exact onset 1/4, E4 at 3/8.
  ASSERT_TRUE(voice.remove_event(Rational(0), Rational(1)).ok());
  EXPECT_EQ(voice.total_length(), Rational(1));

  Rational cumulative(0);
  bool     found_d4_at_1_4 = false;
  bool     found_e4_at_3_8 = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == *Rational::create(1, 4)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_d4_at_1_4 = (n->pitch == pitch_d4());
    }
    if (cumulative == *Rational::create(3, 8)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_e4_at_3_8 = (n->pitch == pitch_e4());
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_d4_at_1_4) << "D4 onset not preserved at 1/4";
  EXPECT_TRUE(found_e4_at_3_8) << "E4 onset not preserved at 3/8";
}

// =========================================================================
// Phase 8e-i — Contraction inserts rests at position (later onsets preserved)
// =========================================================================

TEST(CommandTest, ReplaceEventContractionPreservesLaterOnsets) {
  VoiceContent voice;
  // N(h, C4) at 0 (half note), N(q, D4) at 1/2.  Total 3/4.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  // Replace half note with quarter note -> gap of 1/4, must be filled
  // at position 1/4 with a rest, leaving D4 at onset 1/2.
  ASSERT_TRUE(voice
                  .replace_event(Rational(0), make_note(pitch_c4(), quarter()),
                                 Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));

  Rational cumulative(0);
  bool     found_d4_at_1_2 = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == *Rational::create(1, 2)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_d4_at_1_2 = (n->pitch == pitch_d4());
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_d4_at_1_2) << "D4 onset not preserved at 1/2";
}

// =========================================================================
// Phase 8e-i — Insert into rest coverage of a complete voice
// =========================================================================

TEST(CommandTest, InsertIntoRestCoverageOfCompleteVoice) {
  VoiceContent voice;
  // N(q, C4) at 0, R(q) at 1/4, R(q) at 1/2, R(q) at 3/4.  Total 1.0.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());

  // Insert a quarter note at 1/4, consuming one quarter rest.
  ASSERT_TRUE(voice
                  .insert_event(*Rational::create(1, 4),
                                make_note(pitch_d4(), quarter()), Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
  EXPECT_TRUE(voice.check_complete(Rational(1)).ok());

  // C4 at 0, D4 at 1/4, remainder rests at 1/2 and beyond.
  bool     found_c4_at_0   = false;
  bool     found_d4_at_1_4 = false;
  Rational cumulative(0);
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == Rational(0)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_c4_at_0 = (n->pitch == pitch_c4());
    }
    if (cumulative == *Rational::create(1, 4)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_d4_at_1_4 = (n->pitch == pitch_d4());
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_c4_at_0);
  EXPECT_TRUE(found_d4_at_1_4);
}

// =========================================================================
// Phase 8e-i — Partial final-Rest consumption
// =========================================================================

TEST(CommandTest, InsertPartialLastRestConsumption) {
  VoiceContent voice;
  // R(w) at 0 (whole rest).  Total 1.0.
  ASSERT_TRUE(voice.append(make_rest(whole())).ok());

  // Insert a quarter note at 0, consuming the first quarter of the whole
  // rest.  The remainder (3/4) is decomposed from the original rest's id.
  ASSERT_TRUE(voice
                  .insert_event(Rational(0), make_note(pitch_c4(), quarter()),
                                Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
  EXPECT_TRUE(voice.check_complete(Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
}

TEST(CommandTest, ReplaceEventPartialRestConsumption) {
  VoiceContent voice;
  // N(q, C4) at 0, R(h) at 1/4.  Total 3/4.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(half())).ok());

  const NotationEntityId rest_id = event_id(voice.events()[1]);

  // Replace quarter with dotted quarter (3/8) — needs 1/8 extra, consumes
  // part of the half rest.  The remainder rest should preserve the original
  // Rest id so markings referencing it stay valid.
  const Duration dotted_q = *Duration::create(NoteValue::kQuarter, 1);
  ASSERT_TRUE(voice
                  .replace_event(Rational(0), make_note(pitch_c4(), dotted_q),
                                 Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));

  // The remainder rest at the consumed position should carry the original id.
  bool found_remainder_with_id = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (std::holds_alternative<Rest>(ev)) {
      if (event_id(ev) == rest_id)
        found_remainder_with_id = true;
    }
  }
  EXPECT_TRUE(found_remainder_with_id)
      << "Partial rest remainder did not preserve the original Rest id";
}

// =========================================================================
// Phase 8e-i — Exact generated-ID redo (complete VoiceContent equality)
// =========================================================================

TEST(CommandTest, SetEventRedoExactEqualityWithContraction) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(h, C4) at 0, normalize fills remainder.  Total 1.0.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Redo produces exactly the same voice — every Rest id identical.
  EXPECT_EQ(*voice, after_execute);

  // Second redo cycle.
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, SetEventRedoExactEqualityWithExpansion) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(q, C4) at 0, R(q) at 1/4, normalize remainder.  Total 1.0.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, ConvertEventToRestRedoExactEquality) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

// =========================================================================
// Phase 8e-i — Duplicate supplied IDs
// =========================================================================

TEST(CommandTest, AppendDuplicateIdRejected) {
  VoiceContent     voice;
  const VoiceEvent n1 = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice.append(n1).ok());

  // Append the same event again — duplicate id.
  EXPECT_FALSE(voice.append(n1).ok());
  EXPECT_EQ(voice.events().size(), 1u);
}

TEST(CommandTest, InsertDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const VoiceEvent dup_rest = voice.events()[0];  // same Rest as at index 0
  EXPECT_FALSE(
      voice.insert_event(*Rational::create(1, 4), dup_rest, Rational(1)).ok());
  EXPECT_EQ(voice.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[1]));
}

TEST(CommandTest, ReplaceDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  const VoiceEvent dup = voice.events()[0];  // same id as C4
  // Try to replace D4 with a copy of C4 — id collision.
  EXPECT_FALSE(
      voice.replace_event(*Rational::create(1, 4), dup, Rational(1)).ok());
}

TEST(CommandTest, ReplaceSelfIdAllowed) {
  VoiceContent     voice;
  const VoiceEvent n = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice.append(n).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  // Replace the event with a rest sharing the same id (allowed).
  const NotationEntityId original_id = event_id(n);
  VoiceEvent             repl(Rest{original_id, quarter()});
  ASSERT_TRUE(voice.replace_event(Rational(0), repl, Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[0]));
  EXPECT_EQ(event_id(voice.events()[0]), original_id);
}

// =========================================================================
// Phase 8e-i — Consumed/split-rest marking references
// =========================================================================

TEST(CommandTest, SetEventExpansionRejectsDanglingRestReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(q, C4) at 0, R(q) at 1/4.  Attach a dynamic to the rest.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  const VoiceEvent rest_ev = make_rest(quarter());
  ASSERT_TRUE(voice->append(rest_ev).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId rest_id = event_id(voice->events()[1]);
  ASSERT_TRUE(voice
                  ->add_dynamic(graphscore::make_dynamic_marking(
                      rest_id, graphscore::Dynamic::kMf))
                  .ok());

  const VoiceContent saved = *voice;

  // Replace quarter with half — fully consumes the rest, which has a
  // dynamic marking.  validate_voice_references should flag the dangling ref.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

TEST(CommandTest, InsertConsumesRestRemapsMarkingReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // R(q) at 0, N(q) at 1/4.  Slur references the rest.
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  const VoiceEvent note_ev = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(note_ev).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId rest_id  = event_id(voice->events()[0]);
  const NotationEntityId note2_id = event_id(voice->events()[1]);
  ASSERT_TRUE(voice
                  ->add_slur(graphscore::Slur{NotationEntityId::generate(),
                                              rest_id, note2_id})
                  .ok());

  // Replace the rest at position 0 with a note — the slur start endpoint
  // is remapped to the new note's id.
  const auto new_note    = make_note(pitch_d4(), quarter());
  const auto new_note_id = event_id(new_note);
  EXPECT_NE(new_note_id, rest_id);

  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), new_note);

  EXPECT_TRUE(cmd->execute(fx.project).ok());
  // Slur start remapped to the new note's id.
  EXPECT_EQ(voice->slurs()[0].start_event, new_note_id);
  EXPECT_EQ(voice->slurs()[0].end_event, note2_id);
}

TEST(CommandTest, PartialRestConsumptionPreservesSurvivingReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(q, C4) at 0, R(h) at 1/4.  Dynamic marking references the rest.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId rest_id2 = event_id(voice->events()[1]);
  ASSERT_TRUE(voice
                  ->add_dynamic(graphscore::make_dynamic_marking(
                      rest_id2, graphscore::Dynamic::kMf))
                  .ok());

  // Replace quarter with 3/8 (dotted quarter) — consumes 1/8 from the
  // half rest, leaving 3/8 remainder that keeps the original rest id.
  const Duration dotted_q = *Duration::create(NoteValue::kQuarter, 1);
  auto           cmd      = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), dotted_q));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  // The dynamic marking's referenced id should still resolve — the remainder
  // rest carries the original id.
  const std::vector<graphscore::NotationDiagnostic> diags =
      graphscore::validate_voice_references(*voice);
  EXPECT_TRUE(diags.empty()) << "dynamic marking should still resolve after "
                                "partial rest consumption";
}

// =========================================================================
// Phase 8e-i — Stale-context retryability
// =========================================================================

TEST(CommandTest, SetEventUndoStaleContextRetryable) {
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

  const VoiceContent post_state = *voice;

  // Manually change voice — undo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  // Model was not corrupted by the rejected undo.
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(voice->events().size(), 2u);

  // Restore voice to exact post-snapshot — undo now succeeds and
  // restores the pre-edit state.
  *voice = post_state;
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, ConvertEventToRestRedoStaleContextRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceContent original = *voice;

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Change voice — redo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore voice to the exact pre-snapshot — redo succeeds.
  *voice = original;
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

// =========================================================================
// Phase 8e-i — Append after duplicate detection works normally
// =========================================================================

TEST(CommandTest, AppendWithUniqueIdSucceeds) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());
  EXPECT_EQ(voice.events().size(), 2u);
  EXPECT_NE(event_id(voice.events()[0]), event_id(voice.events()[1]));
}

// =========================================================================
// Phase 8e-i — Insertion at sounding-event boundary is rejected
// =========================================================================

TEST(CommandTest, InsertionBeforeSoundingEventRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Position 0 is the start of a Note — cannot insert before sounding
  // material.
  EXPECT_FALSE(
      voice.insert_event(Rational(0), make_rest(eighth()), Rational(1)).ok());
}

TEST(CommandTest, InsertionIntoMidSoundingBoundaryRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());

  // Position 1/2 is the start of the D4 Note — cannot insert there.
  EXPECT_FALSE(voice
                   .insert_event(*Rational::create(1, 2), make_rest(eighth()),
                                 Rational(2))
                   .ok());
}

// =========================================================================
// Phase 8e-i — Changed-timeline rejection and recovery
// =========================================================================

TEST(CommandTest, SetEventUndoRejectedWhenTimelineShortened) {
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

  const VoiceContent post_state = *voice;

  // Replace the node's timeline with a shorter 3/4 measure.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Undo must reject: pre_snapshot fills 1 whole note, but node_end is 3/4.
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // Restore the original timeline so undo can succeed.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_state;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}
