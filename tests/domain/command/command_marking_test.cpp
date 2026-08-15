// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =====================================================================
// Phase 8e-ii — SetEventCommand rejects marking-ID collisions
// =====================================================================

TEST(CommandTest, SetEventReplaceSelfIdPreservesDynamicAtEventReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Add a dynamic pointing at the note.
  const NotationEntityId note_id      = event_id(voice->events()[0]);
  VoiceContent           pre_markings = *voice;
  ASSERT_TRUE(
      voice->add_dynamic(make_dynamic_marking(note_id, Dynamic::kF)).ok());

  // Replace the note with a different note length, reusing the same
  // event id.  This must succeed: self-id is allowed, and the dynamic
  // references the same id which is still valid after replacement.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(Note{note_id,
                      pitch_d4(),
                      half(),
                      false,
                      {},
                      graphscore::StemDirection::kAuto}));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
}

TEST(CommandTest, SetEventWithMarkingCollidingIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Add a dynamic with some id.
  const NotationEntityId marking_id = NotationEntityId::generate();
  ASSERT_TRUE(voice
                  ->add_dynamic(DynamicMarking{
                      marking_id, event_id(voice->events()[0]), Dynamic::kF})
                  .ok());

  // Try to replace with an event whose id collides with the dynamic.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(Note{marking_id,
                      pitch_d4(),
                      half(),
                      false,
                      {},
                      graphscore::StemDirection::kAuto}));
  const size_t pre_size = voice->events().size();
  const Result r        = cmd->execute(fx.project);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);

  // Voice must be unchanged: same event count, 1 dynamic.
  EXPECT_EQ(voice->events().size(), pre_size);
  EXPECT_EQ(voice->dynamics().size(), 1u);
}

TEST(CommandTest, VoiceContentAppendRejectsMarkingIdCollision) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Add a hairpin with a known id.
  const NotationEntityId id = NotationEntityId::generate();
  ASSERT_TRUE(voice
                  .add_hairpin(Hairpin{id, id, NotationEntityId::generate(),
                                       HairpinDirection::kCrescendo})
                  .ok());

  // Try to append an event that reuses the hairpin's id.
  const Result r = voice.append(VoiceEvent(Note{
      id, pitch_c4(), quarter(), false, {}, graphscore::StemDirection::kAuto}));
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(voice.events().size(), 1u);
  EXPECT_EQ(voice.hairpins().size(), 1u);
}

TEST(CommandTest, VoiceContentInsertEventRejectsMarkingIdCollision) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.normalize(*Rational::create(1, 2)).ok());

  // Add a slur with a unique id referencing the two events.
  const NotationEntityId id      = event_id(voice.events()[0]);
  const NotationEntityId id2     = event_id(voice.events()[1]);
  const NotationEntityId slur_id = NotationEntityId::generate();
  ASSERT_TRUE(voice.add_slur(Slur{slur_id, id, id2}).ok());

  const NotationEntityId insert_id = NotationEntityId::generate();
  ASSERT_TRUE(
      voice.add_dynamic(DynamicMarking{insert_id, id, Dynamic::kFf}).ok());

  // Try to insert an event with the dynamic's id at position 1/4
  // (the rest boundary), consuming the quarter rest's duration.
  const Result r =
      voice.insert_event(*Rational::create(1, 4),
                         VoiceEvent(Note{insert_id,
                                         pitch_d4(),
                                         eighth(),
                                         false,
                                         {},
                                         graphscore::StemDirection::kAuto}),
                         Rational(1));
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  // Voice unchanged: 2 events, 1 slur, 1 dynamic.
  EXPECT_EQ(voice.events().size(), 2u);
  EXPECT_EQ(voice.slurs().size(), 1u);
  EXPECT_EQ(voice.dynamics().size(), 1u);
}

TEST(CommandTest, ConvertEventToRestRedoRejectedWhenTimelineExtended) {
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

  // Replace the node's timeline with a longer one (one 4/4 + one 3/4).
  std::vector<Measure> long_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)},
      Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}};
  auto long_tl = NodeTimeline::create(std::move(long_measures), {});
  ASSERT_TRUE(long_tl.has_value());
  node->set_timeline(std::move(*long_tl));

  // Redo must reject: post_snapshot fills 1 whole note, but node_end is 7/4.
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  // Restore the original timeline so redo succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = original;

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, SetTieUndoRejectedWhenTimelineChanged) {
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

  // Replace timeline with a shorter one.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and voice; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_state;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

// =========================================================================
// Phase 8e-ii — Dynamic marking add/remove
// =========================================================================

TEST(CommandTest, AddDynamicExecuteUndoRedoExactEquality) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid     = event_id(voice->events()[0]);
  const DynamicMarking   marking = make_dynamic_marking(eid, Dynamic::kFf);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, marking.id);
  EXPECT_EQ(voice->dynamics()[0].at_event, eid);
  EXPECT_EQ(voice->dynamics()[0].value, Dynamic::kFf);

  const VoiceContent post_state = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(*voice == post_state);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, AddDynamicDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid     = event_id(voice->events()[0]);
  const DynamicMarking   marking = make_dynamic_marking(eid, Dynamic::kFf);
  ASSERT_TRUE(voice->add_dynamic(marking).ok());

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddDynamicDanglingReferenceRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking = {NotationEntityId::generate(),
                                  NotationEntityId::generate(), Dynamic::kFf};

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, RemoveDynamicRoundTripPreservesOtherMarkings) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking dyn1 =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  const DynamicMarking dyn2 =
      make_dynamic_marking(event_id(voice->events()[1]), Dynamic::kP);
  ASSERT_TRUE(voice->add_dynamic(dyn1).ok());
  ASSERT_TRUE(voice->add_dynamic(dyn2).ok());
  EXPECT_EQ(voice->dynamics().size(), 2u);

  auto cmd = std::make_unique<RemoveDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), dyn1.id);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, dyn2.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 2u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, dyn2.id);
}

TEST(CommandTest, RemoveDynamicMissingIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<RemoveDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      NotationEntityId::generate());
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Hairpin add/remove
// =========================================================================

TEST(CommandTest, AddHairpinWithValidSpanRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hairpin =
      make_hairpin(event_id(voice->events()[0]), event_id(voice->events()[1]),
                   HairpinDirection::kCrescendo);

  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hairpin);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 1u);
}

TEST(CommandTest, AddHairpinEndBeforeStartRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hairpin =
      make_hairpin(event_id(voice->events()[1]), event_id(voice->events()[0]),
                   HairpinDirection::kCrescendo);

  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hairpin);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Slur add/remove
// =========================================================================

TEST(CommandTest, AddSlurWithValidSpanRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));

  auto cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 1u);
}

TEST(CommandTest, RemoveSlurPreservesHairpin) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId first_id  = event_id(voice->events()[0]);
  const NotationEntityId second_id = event_id(voice->events()[1]);
  const Slur             slur      = make_slur(first_id, second_id);
  const Hairpin          hairpin =
      make_hairpin(first_id, second_id, HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice->add_slur(slur).ok());
  ASSERT_TRUE(voice->add_hairpin(hairpin).ok());

  auto cmd = std::make_unique<RemoveSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);
  EXPECT_EQ(voice->hairpins().size(), 1u);
  EXPECT_EQ(voice->hairpins()[0].id, hairpin.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 1u);
  EXPECT_EQ(voice->hairpins().size(), 1u);
}

// =========================================================================
// Phase 8e-ii — Beam override add/remove
// =========================================================================

TEST(CommandTest, AddBeamOverrideValidRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent e1 = make_note(pitch_c4(), eighth());
  const VoiceEvent e2 = make_note(pitch_d4(), eighth());
  ASSERT_TRUE(voice->append(e1).ok());
  ASSERT_TRUE(voice->append(e2).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});

  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 1u);
}

TEST(CommandTest, AddBeamOverrideNonAdjacentRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_rest(half())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_e4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[2])});

  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);
}

TEST(CommandTest, RemoveBeamOverrideMissingIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<RemoveBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      NotationEntityId::generate());
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Grace group add/remove
// =========================================================================

TEST(CommandTest, AddGraceGroupValidRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice->events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAcciaccatura,
                                  .slashed  = true}});

  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 1u);
}

TEST(CommandTest, AddGraceGroupPrincipalIsRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice->events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAcciaccatura,
                                  .slashed  = true}});

  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
}

TEST(CommandTest, RemoveGraceGroupPreservesDynamics) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid = event_id(voice->events()[0]);
  const DynamicMarking   dyn = make_dynamic_marking(eid, Dynamic::kF);
  const GraceGroup       group =
      make_grace_group(eid, {GraceNote{.pitch    = pitch_d4(),
                                       .duration = eighth(),
                                       .type     = GraceNoteType::kAcciaccatura,
                                       .slashed  = true}});
  ASSERT_TRUE(voice->add_dynamic(dyn).ok());
  ASSERT_TRUE(voice->add_grace_group(group).ok());

  auto cmd = std::make_unique<RemoveGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, dyn.id);
}

// =========================================================================
// Phase 8e-ii — Pedal span add/remove
// =========================================================================

TEST(CommandTest, AddPedalSpanExecuteUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  ASSERT_TRUE(lane != nullptr);
  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));

  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  // Whole-lane undo restores exact pre-execute state with no pedal-spans key.
  if (spans != nullptr)
    EXPECT_EQ(spans->size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);
}

TEST(CommandTest, AddPedalSpanInvalidRangeRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(1), Rational(0));

  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  EXPECT_TRUE(spans == nullptr || spans->empty());
}

TEST(CommandTest, AddPedalSpanDuplicateIdRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());

  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
}

TEST(CommandTest, RemovePedalSpanRoundTrip) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span.id);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 0u);
}

TEST(CommandTest, RemovePedalSpanMissingIdRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  auto cmd = std::make_unique<RemovePedalSpanCommand>(
      fx.node_id, fx.track_id, fx.stave_id, NotationEntityId::generate());
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
}

TEST(CommandTest, PedalSpanMultiStaveIsolation) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  const StaveId stave_a = fx.stave_id;
  const StaveId stave_b = StaveId::generate();
  lane->ensure_stave(stave_a);
  lane->ensure_stave(stave_b);
  fill_all_voices(lane, stave_a, fx.node_end);
  fill_all_voices(lane, stave_b, fx.node_end);

  const PedalSpan span_a =
      make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(stave_a, span_a).ok());

  auto cmd = std::make_unique<AddPedalSpanCommand>(
      fx.node_id, fx.track_id, stave_b,
      make_pedal_span(Rational(0), *Rational::create(1, 4)));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // Stave A unchanged.
  const std::vector<PedalSpan>* spans_a = lane->pedal_spans(stave_a);
  ASSERT_NE(spans_a, nullptr);
  ASSERT_EQ(spans_a->size(), 1u);
  EXPECT_EQ((*spans_a)[0].id, span_a.id);

  // Stave B has one.
  const std::vector<PedalSpan>* spans_b = lane->pedal_spans(stave_b);
  ASSERT_NE(spans_b, nullptr);
  EXPECT_EQ(spans_b->size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans_a = lane->pedal_spans(stave_a);
  ASSERT_NE(spans_a, nullptr);
  EXPECT_EQ(spans_a->size(), 1u);
  // Whole-lane undo restores exact pre-execute state; stave_b had no spans.
  spans_b = lane->pedal_spans(stave_b);
  if (spans_b != nullptr)
    EXPECT_EQ(spans_b->size(), 0u);
}
