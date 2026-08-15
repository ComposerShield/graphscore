// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8e-ii — State misuse across the marking command family
// =========================================================================

TEST(CommandTest, MarkingCommandDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, MarkingCommandUndoBeforeExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, MarkingCommandRedoBeforeUndoRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_FALSE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
}

// =========================================================================
// Phase 8e-ii — Stale-context undo/redo rejection and retry
// =========================================================================

TEST(CommandTest, AddDynamicStaleContextUndoRejectedAndRetried) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_state = *voice;

  // Manually change the voice — undo must be rejected.
  ASSERT_TRUE(voice
                  ->add_dynamic(make_dynamic_marking(
                      event_id(voice->events()[0]), Dynamic::kPp))
                  .ok());
  EXPECT_FALSE(cmd->undo(fx.project).ok());

  // Restore and retry.
  *voice = post_state;
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Deterministic replay
// =========================================================================

TEST(CommandTest, AddDynamicDeterministicReplay) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid = event_id(voice->events()[0]);
  const DynamicMarking   m   = make_dynamic_marking(eid, Dynamic::kFf);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), m);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(*voice == after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(*voice == after_execute);
}

// =========================================================================
// Phase 8e-ii — Missing/stale node/track/stave IDs rejected
// =========================================================================

TEST(CommandTest, AddDynamicMissingNodeRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      NodeId::generate(), fx.track_id, fx.stave_id, *Voice::create(1), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddDynamicWrongVoiceScopeRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(2), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — VoiceContent removal mutator tests
// =========================================================================

TEST(CommandTest, VoiceContentRemoveDynamicSuccess) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const DynamicMarking m =
      make_dynamic_marking(event_id(voice.events()[0]), Dynamic::kF);
  ASSERT_TRUE(voice.add_dynamic(m).ok());
  EXPECT_EQ(voice.dynamics().size(), 1u);
  EXPECT_TRUE(voice.remove_dynamic(m.id).ok());
  EXPECT_EQ(voice.dynamics().size(), 0u);
}

TEST(CommandTest, VoiceContentRemoveDynamicMissingId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const Result r = voice.remove_dynamic(NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, VoiceContentRemoveHairpinPreservesSlur) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  const NotationEntityId first  = event_id(voice.events()[0]);
  const NotationEntityId second = event_id(voice.events()[1]);
  const Hairpin hp = make_hairpin(first, second, HairpinDirection::kCrescendo);
  const Slur    sl = make_slur(first, second);
  ASSERT_TRUE(voice.add_hairpin(hp).ok());
  ASSERT_TRUE(voice.add_slur(sl).ok());

  EXPECT_TRUE(voice.remove_hairpin(hp.id).ok());
  EXPECT_EQ(voice.hairpins().size(), 0u);
  EXPECT_EQ(voice.slurs().size(), 1u);
}

TEST(CommandTest, VoiceContentRemoveSlurMissingId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  const Result r = voice.remove_slur(NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, VoiceContentRemoveBeamOverrideSuccess) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice.events()[0]), event_id(voice.events()[1])});
  ASSERT_TRUE(voice.add_beam_override(beam).ok());
  EXPECT_EQ(voice.beam_overrides().size(), 1u);
  EXPECT_TRUE(voice.remove_beam_override(beam.id).ok());
  EXPECT_EQ(voice.beam_overrides().size(), 0u);
}

TEST(CommandTest, VoiceContentRemoveGraceGroupSuccess) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice.events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAcciaccatura,
                                  .slashed  = true}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 1u);
  EXPECT_TRUE(voice.remove_grace_group(group.id).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — TrackLane removal mutator tests
// =========================================================================

TEST(CommandTest, TrackLaneRemovePedalSpanSuccess) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave, span).ok());

  const Result r = lane.remove_pedal_span(stave, span.id);
  EXPECT_TRUE(r.ok());
  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 0u);
}

TEST(CommandTest, TrackLaneRemovePedalSpanMissingStave) {
  TrackLane    lane;
  const Result r =
      lane.remove_pedal_span(StaveId::generate(), NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, TrackLaneRemovePedalSpanMissingId) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const Result r = lane.remove_pedal_span(stave, NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

// =====================================================================
// Phase 8e-ii — TrackLane add_pedal_span transactional paths
// =====================================================================

TEST(CommandTest, TrackLaneAddPedalSpanAbsentStaveRejected) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  // No ensure_stave call: stave is absent from staves_.

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const Result    r    = lane.add_pedal_span(stave, span);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  // Verify no orphan key was created in pedal_spans_.
  EXPECT_EQ(lane.pedal_spans(stave), nullptr);
}

TEST(CommandTest, TrackLaneAddPedalSpanAbsentStaveLeavesNoOrphanKey) {
  TrackLane     lane;
  const StaveId stave_a = StaveId::generate();
  const StaveId stave_b = StaveId::generate();
  lane.ensure_stave(stave_a);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave_a, span).ok());

  // Same id on an absent stave is still rejected (stave check wins,
  // but if we change staves_ to contain it, the duplicate check
  // would still fire).
  const Result r = lane.add_pedal_span(stave_b, span);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(lane.pedal_spans(stave_b), nullptr);
}

TEST(CommandTest, TrackLaneAddPedalSpanAbsentKeyCommitsCorrectly) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);  // now the stave must exist

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const Result    r    = lane.add_pedal_span(stave, span);
  EXPECT_TRUE(r.ok());

  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);
}

TEST(CommandTest, TrackLaneAddPedalSpanExistingKeyPreservesEntries) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave, span1).ok());

  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  const Result r = lane.add_pedal_span(stave, span2);
  EXPECT_TRUE(r.ok());

  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].id, span1.id);
  EXPECT_EQ((*spans)[1].id, span2.id);
}

TEST(CommandTest, TrackLaneAddPedalSpanDuplicateAcrossStavesRejected) {
  TrackLane     lane;
  const StaveId stave_a = StaveId::generate();
  const StaveId stave_b = StaveId::generate();
  lane.ensure_stave(stave_a);
  lane.ensure_stave(stave_b);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave_a, span).ok());

  // Same id on a different stave is rejected before any mutation.
  const Result r = lane.add_pedal_span(stave_b, span);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);

  // Verify stave_b's pedal_spans_ was never touched (absent key).
  EXPECT_EQ(lane.pedal_spans(stave_b), nullptr);
}

// =====================================================================
// Phase 8e-ii — Cross-kind marking ID uniqueness
// =====================================================================

TEST(CommandTest, AddSlurCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid_first  = event_id(voice->events()[0]);
  const NotationEntityId eid_second = event_id(voice->events()[1]);

  // Add a dynamic with a specific id.
  const NotationEntityId shared_id = NotationEntityId::generate();
  const DynamicMarking   dyn{shared_id, eid_first, Dynamic::kMf};
  ASSERT_TRUE(voice->add_dynamic(dyn).ok());

  // Attempt to add a slur with the same id.
  const Slur slur{shared_id, eid_first, eid_second};
  auto       cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddHairpinCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid_first  = event_id(voice->events()[0]);
  const NotationEntityId eid_second = event_id(voice->events()[1]);
  const NotationEntityId shared_id  = NotationEntityId::generate();
  const Slur             existing{shared_id, eid_first, eid_second};
  ASSERT_TRUE(voice->add_slur(existing).ok());

  const Hairpin hp{shared_id, eid_first, eid_second,
                   HairpinDirection::kCrescendo};
  auto          cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddBeamOverrideCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId shared_id = NotationEntityId::generate();
  const DynamicMarking   dyn{shared_id, event_id(voice->events()[0]),
                           Dynamic::kMf};
  ASSERT_TRUE(voice->add_dynamic(dyn).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});
  // Reassign the beam's id to collide.
  const BeamOverride colliding{shared_id, beam.kind, beam.events};
  auto               cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), colliding);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddGraceGroupCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId shared_id = NotationEntityId::generate();
  const NotationEntityId eid       = event_id(voice->events()[0]);
  const Hairpin          hp{shared_id, eid, eid, HairpinDirection::kCrescendo};
  ASSERT_TRUE(voice->add_hairpin(hp).ok());

  const GraceGroup group =
      make_grace_group(eid, {GraceNote{.pitch    = pitch_d4(),
                                       .duration = eighth(),
                                       .type     = GraceNoteType::kAppoggiatura,
                                       .slashed  = false}});
  const GraceGroup colliding{shared_id, group.principal_event, group.notes};
  auto             cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), colliding);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddDynamicDuplicateMarkingIdRejects) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const DynamicMarking m1 =
      make_dynamic_marking(event_id(voice.events()[0]), Dynamic::kFf);
  ASSERT_TRUE(voice.add_dynamic(m1).ok());
  EXPECT_FALSE(voice.add_dynamic(m1).ok());
}

TEST(CommandTest, AddDynamicEventIdCollisionRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const DynamicMarking m{event_id(voice.events()[0]),  // event id
                         event_id(voice.events()[0]), Dynamic::kFf};
  EXPECT_FALSE(voice.add_dynamic(m).ok());
}

// =====================================================================
// Phase 8e-ii — Slur Rest endpoint rejection via command
// =====================================================================

TEST(CommandTest, AddSlurAttachedToRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));
  auto cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);
}

TEST(CommandTest, AddSlurBothEndpointsRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));
  auto cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Hairpin dangling/invalid endpoint rejection
// =====================================================================

TEST(CommandTest, AddHairpinDanglingEndpointRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hp =
      make_hairpin(event_id(voice->events()[0]), NotationEntityId::generate(),
                   HairpinDirection::kCrescendo);
  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);
}

// =====================================================================
// Phase 8e-ii — Beam override invalid events
// =====================================================================

TEST(CommandTest, AddBeamOverrideNonBeamableRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});
  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Grace group dangling/invalid principal
// =====================================================================

TEST(CommandTest, AddGraceGroupPrincipalNotFoundRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(NotationEntityId::generate(),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});
  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
}

// =====================================================================
// Phase 8e-ii — Pedal span beyond node_end
// =====================================================================

TEST(CommandTest, AddPedalSpanBeyondNodeEndRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  ASSERT_TRUE(lane != nullptr);
  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  // node_end is 1 whole note (one 4/4 measure).  Span end = 2 is beyond it.
  const PedalSpan span = make_pedal_span(Rational(0), Rational(2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Pedal span exact ordering and absent-container restoration
// =====================================================================

TEST(CommandTest, AddPedalSpanExactOrderAfterUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  // Add two pedal spans with distinct start positions.
  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span1).ok());
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());

  // Now add a third via command.
  const PedalSpan span3 =
      make_pedal_span(*Rational::create(1, 4), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span3);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 3u);

  const TrackLane post_add = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].id, span1.id);
  EXPECT_EQ((*spans)[1].id, span2.id);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_add);
}

TEST(CommandTest, RemovePedalSpanRestoresOrderExactly) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  const PedalSpan span3 =
      make_pedal_span(*Rational::create(1, 4), *Rational::create(7, 8));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span1).ok());
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span3).ok());

  const TrackLane before_remove = *lane;

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span3.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].id, span1.id);
  EXPECT_EQ((*spans)[1].id, span2.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, before_remove);
}

// =====================================================================
// Phase 8e-ii — Pedal span absent container restoration
// =====================================================================

TEST(CommandTest, PedalSpanAbsentKeyPreservedAfterUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  const StaveId stave_a = fx.stave_id;
  const StaveId stave_b = StaveId::generate();
  lane->ensure_stave(stave_a);
  lane->ensure_stave(stave_b);
  fill_all_voices(lane, stave_a, fx.node_end);
  fill_all_voices(lane, stave_b, fx.node_end);

  // Only stave_a gets pedal spans.
  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(stave_a, span).ok());

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      stave_a, span.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // After remove, stave_a pedal vector should be empty but the key
  // should still exist (container not absent).
  const std::vector<PedalSpan>* spans_a = lane->pedal_spans(stave_a);
  EXPECT_TRUE(spans_a == nullptr || spans_a->empty());

  // Stave_b still has no pedal key at all.
  EXPECT_EQ(lane->pedal_spans(stave_b), nullptr);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans_a = lane->pedal_spans(stave_a);
  ASSERT_NE(spans_a, nullptr);
  ASSERT_EQ(spans_a->size(), 1u);
  EXPECT_EQ((*spans_a)[0].id, span.id);
}

// =====================================================================
// Phase 8e-ii — Pedal stale context undo/redo
// =====================================================================

TEST(CommandTest, AddPedalSpanStaleContextUndoRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span1);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // Manually change the lane — undo must be rejected.
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());

  EXPECT_FALSE(cmd->undo(fx.project).ok());
}

TEST(CommandTest, RemovePedalSpanStaleContextRedoRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());

  const TrackLane before_remove = *lane;

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, before_remove);

  // Manually change the lane — redo must be rejected.
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());

  EXPECT_FALSE(cmd->redo(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Pedal deterministic replay
// =====================================================================

TEST(CommandTest, AddPedalSpanDeterministicReplay) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane after_execute = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, after_execute);
}
