// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8e-i — Duration expansion (consuming following rests)
// =========================================================================

TEST(CommandTest, SetEventDurationExpansionConsumesWholeRest) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, quarter rest at 1/4, filled to 1 whole.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));

  // Replace quarter note with half note -> consumes the following quarter
  // rest.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(Rational(1)).ok());
  // Should now be: half note + normalized rest covering remainder.
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(event_duration(voice->events()[0]).resolved(), half().resolved());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(event_duration(voice->events()[0]).resolved(),
            quarter().resolved());
}

TEST(CommandTest, SetEventDurationExpansionConsumesPartialRest) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, eighth rest at 1/4, filled.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());

  // Replace quarter with 3/8 note -> partial rest consumption
  // (needs an extra 1/8, the eighth rest covers it exactly).
  // A dotted quarter = 3/8.
  const Duration dotted_quarter = *Duration::create(NoteValue::kQuarter, 1);
  auto           cmd            = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), dotted_quarter));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(event_duration(voice->events()[0]).resolved(),
            dotted_quarter.resolved());
}

TEST(CommandTest,
     SetEventDurationExpansionRejectedWhenFollowingEventIsSounding) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, another quarter note at 1/4.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());
  const VoiceContent saved = *voice;

  // Replace first quarter with half — would need to consume D4 note.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

TEST(CommandTest,
     SetEventDurationExpansionRejectedWhenRestCoverageInsufficient) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, eighth rest at 1/4, then a sounding quarter note at 3/8.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  // Fill remainder from 5/8 to 1 whole.
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));
  const VoiceContent saved = *voice;

  // Replace quarter with half note — needs 1/4 extra, only 1/8 rest
  // available before the D4 note blocks further consumption.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

TEST(CommandTest, SetEventDurationExpansionConsumesMultipleFollowingRests) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, two eighth rests, filled.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());

  // Replace quarter with half — consumes both eighth rests.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(Rational(1)).ok());
}

// =========================================================================
// Phase 8e-i — Command rejection of dangling dynamic/grace references
// =========================================================================

TEST(CommandTest, SetEventRemapsReferenceWhenIdChanges) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId ev_id = graphscore::event_id(voice->events()[0]);
  ASSERT_TRUE(voice
                  ->add_dynamic(graphscore::make_dynamic_marking(
                      ev_id, graphscore::Dynamic::kMf))
                  .ok());

  const auto rest        = make_rest(quarter());
  const auto new_rest_id = event_id(rest);
  EXPECT_NE(new_rest_id, ev_id);

  auto cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), rest);

  EXPECT_TRUE(cmd->execute(fx.project).ok());
  // Dynamic reference remapped to the new Rest's id.
  EXPECT_EQ(voice->dynamics()[0].at_event, new_rest_id);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, ConvertEventToRestRemovesGraceGroupWhosePrincipalConverts) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId      ev_id = graphscore::event_id(voice->events()[0]);
  const graphscore::GraceNote grace_note{
      .pitch    = pitch_e4(),
      .duration = eighth(),
      .type     = graphscore::GraceNoteType::kAppoggiatura,
      .slashed  = false};
  const GraceGroup group = graphscore::make_grace_group(ev_id, {grace_note});
  ASSERT_TRUE(voice->add_grace_group(group).ok());

  // Convert the principal event to rest — the grace group is normalized
  // away rather than rejecting the conversion.
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
  EXPECT_TRUE(voice->grace_groups().empty());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  ASSERT_EQ(voice->grace_groups().size(), 1u);
  EXPECT_EQ(voice->grace_groups()[0], group);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
  EXPECT_TRUE(voice->grace_groups().empty());
}

TEST(CommandTest, ConvertEventToRestRemovesAttachedSlur) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId first_id  = graphscore::event_id(voice->events()[0]);
  const NotationEntityId second_id = graphscore::event_id(voice->events()[1]);
  const Slur             slur      = make_slur(first_id, second_id);
  ASSERT_TRUE(voice->add_slur(slur).ok());

  // Convert the slur's end event to rest — the slur is normalized away
  // rather than rejecting the conversion (validate_voice_references would
  // otherwise flag kSlurAttachedToRest).
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[1]));
  EXPECT_TRUE(voice->slurs().empty());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[1]));
  ASSERT_EQ(voice->slurs().size(), 1u);
  EXPECT_EQ(voice->slurs()[0], slur);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[1]));
  EXPECT_TRUE(voice->slurs().empty());
}

TEST(CommandTest, ConvertEventToRestReducesBeamOverride) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_e4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId first_id  = graphscore::event_id(voice->events()[0]);
  const NotationEntityId second_id = graphscore::event_id(voice->events()[1]);
  const NotationEntityId third_id  = graphscore::event_id(voice->events()[2]);
  const std::vector<NotationEntityId> run{first_id, second_id, third_id};
  const BeamOverride beam = make_beam_override(BeamOverride::Kind::kJoin, run);
  ASSERT_TRUE(voice->add_beam_override(beam).ok());

  // Converting the run's last event to rest normalizes the beam override
  // rather than rejecting the conversion (normalize_references_for_replaced_
  // event, shared with DeleteNoteheadCommand): the surviving {first_id,
  // second_id} run stays contiguous and beamable, so the override is
  // reduced, not dropped outright.
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[2]));
  ASSERT_EQ(voice->beam_overrides().size(), 1u);
  EXPECT_EQ(voice->beam_overrides()[0].events,
            (std::vector<NotationEntityId>{first_id, second_id}));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[2]));
  ASSERT_EQ(voice->beam_overrides().size(), 1u);
  EXPECT_EQ(voice->beam_overrides()[0], beam);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[2]));
  ASSERT_EQ(voice->beam_overrides().size(), 1u);
  EXPECT_EQ(voice->beam_overrides()[0].events,
            (std::vector<NotationEntityId>{first_id, second_id}));
}

TEST(CommandTest, ConvertChordEventToRestRoundTripPreservesIdAndDuration) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()},
               ChordNote{.pitch = pitch_g4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const NotationEntityId chord_id = chord.id;

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Rest* r = std::get_if<Rest>(&voice->events()[0]);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->id, chord_id);
  EXPECT_EQ(r->duration.resolved(), half().resolved());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));
  EXPECT_EQ(std::get<Chord>(voice->events()[0]), chord);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  ASSERT_NE(std::get_if<Rest>(&voice->events()[0]), nullptr);
  EXPECT_EQ(graphscore::event_id(voice->events()[0]), chord_id);
}

// =========================================================================
// Phase 8e-i — Stale-context undo/redo rejection
// =========================================================================

TEST(CommandTest, SetEventUndoRejectedWhenVoiceChanged) {
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

  // Manually change the voice — undo should reject.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventRedoRejectedWhenVoiceChanged) {
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
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Manually change the voice — redo should reject.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8e-i — Duplicate-pitch chord notehead index targeting
// =========================================================================

TEST(CommandTest, SetTieDuplicatePitchChordTargetsExactIndex) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Chord with two C4 noteheads (unison doublings are permitted).
  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_c4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  // Successor sounds C4 so the tie is valid.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie index 0 only.
  auto cmd0 = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(0), true);
  ASSERT_TRUE(cmd0->execute(fx.project).ok());
  const Chord* c0 = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c0, nullptr);
  EXPECT_TRUE(c0->notes[0].tied_to_next);
  EXPECT_FALSE(c0->notes[1].tied_to_next);

  // Undo, then tie index 1 only.
  ASSERT_TRUE(cmd0->undo(fx.project).ok());
  auto cmd1 = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(1), true);
  ASSERT_TRUE(cmd1->execute(fx.project).ok());
  const Chord* c1 = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c1, nullptr);
  EXPECT_FALSE(c1->notes[0].tied_to_next);
  EXPECT_TRUE(c1->notes[1].tied_to_next);
}

// =========================================================================
// Phase 8e-i — True deterministic replay with whole-VoiceContent equality
// =========================================================================

TEST(CommandTest, DeterministicReplaySetEventExactEqualityWithIds) {
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
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, VoiceContent{});

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Redo produces exactly the same voice as the original execute.
  EXPECT_EQ(*voice, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Second redo cycle produces the same result.
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, DeterministicReplaySetTieExactEqualityWithIds) {
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
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, VoiceContent{});

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}
