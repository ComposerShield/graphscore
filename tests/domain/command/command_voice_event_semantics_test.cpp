// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8e-i — Deterministic replay and ordering
// =========================================================================

TEST(CommandTest, DeterministicReplay8ei) {
  auto run_sequence =
      [](Project& project) -> std::pair<NotationEntityId, VoiceEvent> {
    CommandHistory history;

    const auto   t   = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
    const NodeId nid = project.add_node("Node");
    Node*        n   = project.find_node(nid);

    std::vector<Measure> measures = {
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
    auto tl = NodeTimeline::create(std::move(measures), {});
    n->set_timeline(std::move(*tl));

    StaveId sid;
    for (const graphscore::StaveDefinition& sd :
         project.find_active_track(*t)->layout().staves()) {
      n->lane(*t)->ensure_stave(sd.id);
      sid = sd.id;
    }

    VoiceContent* voice = &n->lane(*t)->stave(sid)->voice(*Voice::create(1));
    static_cast<void>(voice->append(make_note(pitch_c4(), quarter())));
    static_cast<void>(voice->normalize(Rational(1)));

    NotationEntityId first_id = graphscore::event_id(voice->events()[0]);
    const VoiceEvent rest     = make_rest(quarter());

    static_cast<void>(history.execute_new(
        std::make_unique<SetEventCommand>(nid, *t, sid, *Voice::create(1),
                                          Rational(0), rest),
        project));
    static_cast<void>(
        history.execute_new(std::make_unique<ConvertEventToRestCommand>(
                                nid, *t, sid, *Voice::create(1), Rational(0)),
                            project));

    return std::make_pair(first_id, voice->events()[0]);
  };

  Project first  = make_project();
  Project second = make_project();

  auto [fid, f_ev] = run_sequence(first);
  auto [sid, s_ev] = run_sequence(second);

  EXPECT_TRUE(std::holds_alternative<Rest>(f_ev));
  EXPECT_TRUE(std::holds_alternative<Rest>(s_ev));
  // Ids are independently generated, so should differ between projects.
  EXPECT_NE(fid, sid);
}

// =========================================================================
// Phase 8e-i — Tie validation (rejection of invalid ties)
// =========================================================================

TEST(CommandTest, SetTieLastEventRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // The only event is the last one — tying it has no successor.
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

TEST(CommandTest, SetTieMismatchedSuccessorRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie C4 → successor is D4 — pitch mismatch.
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

// =========================================================================
// Phase 8e-i — ConvertEventToRestCommand ID stability
// =========================================================================

TEST(CommandTest, ConvertNoteToRestPreservesIdOnRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId original_id = graphscore::event_id(voice->events()[0]);

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const NotationEntityId first_rest_id =
      graphscore::event_id(voice->events()[0]);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const NotationEntityId second_rest_id =
      graphscore::event_id(voice->events()[0]);

  // The rest preserves the original note's id.
  EXPECT_EQ(first_rest_id, original_id);
  // Redo produces the exact same id.
  EXPECT_EQ(second_rest_id, first_rest_id);
}

TEST(CommandTest, ConvertChordToRestPreservesIdOnRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const NotationEntityId original_id = graphscore::event_id(voice->events()[0]);

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const NotationEntityId first_id = graphscore::event_id(voice->events()[0]);
  EXPECT_EQ(first_id, original_id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(graphscore::event_id(voice->events()[0]), first_id);
}

// =========================================================================
// Phase 8e-i — Predecessor-tie preservation (no dangling references)
// =========================================================================

TEST(CommandTest, SetEventBreaksPredecessorTieRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent n1 = make_note(pitch_c4(), quarter(), true);
  const VoiceEvent n2 = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(n1).ok());
  ASSERT_TRUE(voice->append(n2).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Replace the second note (the tie target) with D4 — tie from C4 breaks.
  const VoiceEvent replacement = make_note(pitch_d4(), quarter());
  auto             cmd         = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4), replacement);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged: first note still tied_to_next, second note still C4.
  const Note* n1_after = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1_after, nullptr);
  EXPECT_TRUE(n1_after->tied_to_next);
  const Note* n2_after = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2_after, nullptr);
  EXPECT_EQ(n2_after->pitch, pitch_c4());
}

TEST(CommandTest, ConvertEventToRestBreaksPredecessorTieNormalized) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent n1 = make_note(pitch_c4(), quarter(), true);
  const VoiceEvent n2 = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(n1).ok());
  ASSERT_TRUE(voice->append(n2).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const NotationEntityId n2_id = graphscore::event_id(voice->events()[1]);

  // Convert the second note to rest — the predecessor's incoming tie is
  // normalized away rather than rejecting the conversion.
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Note* n1_after = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1_after, nullptr);
  EXPECT_FALSE(n1_after->tied_to_next);
  ASSERT_TRUE(std::holds_alternative<Rest>(voice->events()[1]));
  EXPECT_EQ(graphscore::event_id(voice->events()[1]), n2_id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Note* n1_undone = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1_undone, nullptr);
  EXPECT_TRUE(n1_undone->tied_to_next);
  ASSERT_TRUE(std::holds_alternative<Note>(voice->events()[1]));
  EXPECT_EQ(graphscore::event_id(voice->events()[1]), n2_id);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_FALSE(std::get<Note>(voice->events()[0]).tied_to_next);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[1]));
}

// =========================================================================
// Phase 8e-i — Overflow rejection (duration expansion cannot silently
//                clip later content)
// =========================================================================

TEST(CommandTest, SetEventDurationExpansionOverflowRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceContent saved = *voice;

  // Replace the quarter at 0 with a whole note — would exceed
  // target_length(1).
  const VoiceEvent whole_note = make_note(pitch_c4(), whole());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), whole_note);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

// =========================================================================
// Phase 8e-i — Full VoiceContent equality with IDs on execute→undo→redo
// =========================================================================

TEST(CommandTest, SetEventFullEqualityWithIdsOnUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent original = *voice;

  const VoiceEvent replacement = make_rest(quarter());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), replacement);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, original);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, original);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, ConvertEventToRestFullEqualityWithIdsOnUndoRedo) {
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
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, original);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, original);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, SetTieFullEqualityWithIdsOnUndoRedo) {
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
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, original);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, original);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

// =========================================================================
// VoiceContent positional-mutator tests
// =========================================================================

TEST(CommandTest, VoiceContentInsertIntoRestCoverageAtBeginning) {
  VoiceContent voice;
  // Rest coverage at position 0: R(q), N(q).
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  // Insert eighth note at position 0 consuming part of the quarter rest.
  ASSERT_TRUE(voice
                  .insert_event(Rational(0), make_note(pitch_c4(), eighth()),
                                Rational(1))
                  .ok());

  ASSERT_GE(voice.events().size(), 3u);
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentInsertAtEnd) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const Rational end_pos = voice.total_length();
  ASSERT_TRUE(
      voice.insert_event(end_pos, make_rest(eighth()), Rational(1)).ok());

  ASSERT_GE(voice.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[1]));
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentInsertAtEventBoundary) {
  VoiceContent voice;
  // N(q) at 0, R(h) at 1/4, N(q) at 3/4.  Total = 1.0.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));

  // Insert a quarter rest at 1/4, consuming into the half rest coverage.
  ASSERT_TRUE(voice
                  .insert_event(*Rational::create(1, 4), make_rest(quarter()),
                                Rational(2))
                  .ok());

  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[1]));
  // The D4 sounding onset at 3/4 must be preserved somewhere beyond the
  // inserted + remainder rests.
  bool found_d4 = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (const auto* n = std::get_if<Note>(&ev)) {
      if (n->pitch == pitch_d4())
        found_d4 = true;
    }
  }
  EXPECT_TRUE(found_d4);
  EXPECT_EQ(voice.total_length(), Rational(2));
}

TEST(CommandTest, VoiceContentInsertInvalidPositionFails) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  EXPECT_FALSE(voice
                   .insert_event(*Rational::create(1, 8), make_rest(eighth()),
                                 Rational(1))
                   .ok());
}

TEST(CommandTest, VoiceContentInsertOverflowRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));

  // Inserting anything into an already-full voice exceeds target_length=1.
  EXPECT_FALSE(
      voice.insert_event(Rational(0), make_rest(eighth()), Rational(1)).ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentRemoveEvent) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  ASSERT_TRUE(voice.remove_event(*Rational::create(1, 4), Rational(1)).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
}

TEST(CommandTest, VoiceContentRemoveInvalidPositionFails) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  EXPECT_FALSE(voice.remove_event(*Rational::create(1, 2), Rational(1)).ok());
}

TEST(CommandTest, VoiceContentReplaceEvent) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  ASSERT_TRUE(
      voice.replace_event(Rational(0), make_rest(quarter()), Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[0]));
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentReplaceInvalidPositionFails) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  EXPECT_FALSE(voice
                   .replace_event(*Rational::create(1, 2), make_rest(quarter()),
                                  Rational(1))
                   .ok());
}

TEST(CommandTest, VoiceContentReplaceOverflowRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));

  // Replacing a half note with a whole note overflows target_length=1.
  EXPECT_FALSE(voice
                   .replace_event(Rational(0), make_note(pitch_c4(), whole()),
                                  Rational(1))
                   .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentFindEventIndexEmptyVoice) {
  VoiceContent voice;
  EXPECT_FALSE(voice.find_event_index_at(Rational(0)).has_value());
}

TEST(CommandTest, VoiceContentFindEventIndexValid) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());

  auto idx0 = voice.find_event_index_at(Rational(0));
  ASSERT_TRUE(idx0.has_value());
  EXPECT_EQ(*idx0, 0u);

  auto idx1 = voice.find_event_index_at(*Rational::create(1, 4));
  ASSERT_TRUE(idx1.has_value());
  EXPECT_EQ(*idx1, 1u);

  // Position at total_length() has no event starting there.
  auto idx_end = voice.find_event_index_at(*Rational::create(3, 4));
  EXPECT_FALSE(idx_end.has_value());
}

TEST(CommandTest, VoiceContentInsertSingleNoteChordRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const Chord bad_chord =
      make_chord(eighth(), {ChordNote{.pitch = pitch_c4()}});
  EXPECT_FALSE(
      voice.insert_event(Rational(0), VoiceEvent(bad_chord), Rational(1)).ok());
  EXPECT_EQ(voice.events().size(), 1u);
}

// =========================================================================
// Cross-command order independence
// =========================================================================

TEST(CommandTest, SetEventAndSetTieInterleavedUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // 1. Set event: replace first note with rest.
  auto set_cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(set_cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // 2. Set tie on the second event (the note at 1/4), which has a
  //    matching successor at 1/2.
  auto tie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4), std::nullopt, true);
  ASSERT_TRUE(tie_cmd->execute(fx.project).ok());
  const Note* n2 = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2, nullptr);
  EXPECT_TRUE(n2->tied_to_next);

  // Undo tie first.
  ASSERT_TRUE(tie_cmd->undo(fx.project).ok());
  const Note* n2u = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2u, nullptr);
  EXPECT_FALSE(n2u->tied_to_next);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // Undo set.
  ASSERT_TRUE(set_cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  const Note* n2uu = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2uu, nullptr);
  EXPECT_FALSE(n2uu->tied_to_next);  // tie was already undone

  // Redo set.
  ASSERT_TRUE(set_cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // Redo tie.
  ASSERT_TRUE(tie_cmd->redo(fx.project).ok());
  const Note* n2r = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2r, nullptr);
  EXPECT_TRUE(n2r->tied_to_next);
}
