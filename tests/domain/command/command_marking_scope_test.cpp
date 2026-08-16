// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =====================================================================
// Phase 8e-ii — Timeline change rejects stale undo/redo
// =====================================================================

TEST(CommandTest, AddDynamicTimelineChangeUndoRejectedAndRetryable) {
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
  const VoiceContent post_exec = *voice;

  // Replace timeline: 2/4 measure shortens node_end to 0.5.
  // The voice was complete for node_end 1.0; undo's snapshot validation
  // against the new node_end fails because the voice is now over-full.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Undo must reject atomically; voice unchanged, command still kDone.
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore the exact original timeline and voice.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_exec;

  // Retry succeeds.
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, AddDynamicTimelineChangeRedoRejectedAndRetryable) {
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
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
  const VoiceContent post_undo = *voice;

  // Replace timeline: shorten node_end.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Redo must reject atomically; voice unchanged, command still kUndone.
  EXPECT_FALSE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_undo);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and voice; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_undo;

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, AddPedalSpanTimelineChangeUndoRejectedAndRetryable) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane post_exec = *lane;

  // Shorten timeline.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Undo must reject atomically; lane unchanged.
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, post_exec);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and lane; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *lane = post_exec;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(lane->pedal_spans(fx.stave_id), nullptr);
}

TEST(CommandTest, AddPedalSpanTimelineChangeRedoRejectedAndRetryable) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane post_exec = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const TrackLane post_undo = *lane;

  // Shorten timeline.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Redo must reject atomically.
  EXPECT_FALSE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_undo);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and lane; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *lane = post_undo;

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_exec);
}

// =====================================================================
// Phase 8e-ii — Non-target voice incompleteness blocks pedal commands
// =====================================================================

TEST(CommandTest, AddPedalSpanIncompleteNonTargetVoiceRejectedAndRetryable) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  // Only fill voice 1 (target stave). Leave voices 2-4 empty/incomplete.
  {
    VoiceContent* vc = &lane->stave(fx.stave_id)->voice(*Voice::create(1));
    ASSERT_NE(vc, nullptr);
    ASSERT_TRUE(vc->append(make_note(pitch_c4(), quarter())).ok());
    ASSERT_TRUE(vc->normalize(fx.node_end).ok());
  }

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);

  // Execute must reject because voice 2 is incomplete.
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);

  // Fill voices 2-4; retry succeeds.
  for (int v = 2; v <= 4; ++v) {
    VoiceContent* vc =
        &lane->stave(fx.stave_id)
             ->voice(*Voice::create(static_cast<std::uint8_t>(v)));
    ASSERT_NE(vc, nullptr);
    ASSERT_TRUE(vc->append(make_note(pitch_c4(), quarter())).ok());
    ASSERT_TRUE(vc->normalize(fx.node_end).ok());
  }

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
}

TEST(CommandTest, AddPedalSpanUndoNonTargetVoiceBrokenAfterExecuteRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane post_exec = *lane;

  // After execute, break a non-target voice by clearing it.
  VoiceContent*      vc2 = &lane->stave(fx.stave_id)->voice(*Voice::create(2));
  const VoiceContent saved_vc2 = *vc2;
  vc2->clear();
  const TrackLane broken = *lane;

  // Undo must reject atomically: lane unchanged from broken state.
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, broken);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore the broken voice from our saved copy; retry succeeds.
  *vc2 = saved_vc2;
  EXPECT_EQ(*lane, post_exec);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(lane->pedal_spans(fx.stave_id), nullptr);
}

// =====================================================================
// Phase 8e-ii — Pedal command ignores invalid spans on non-target stave
// =====================================================================

TEST(CommandTest, AddPedalSpanInvalidSpanOnNonTargetStaveDoesNotGateEdit) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  const StaveId stave_a = fx.stave_id;
  const StaveId stave_b = StaveId::generate();
  lane->ensure_stave(stave_a);
  lane->ensure_stave(stave_b);
  fill_all_voices(lane, stave_a, fx.node_end);
  fill_all_voices(lane, stave_b, fx.node_end);

  // Add an invalid pedal span (end > node_end) directly on stave_b.
  const PedalSpan bad_span =
      make_pedal_span(Rational(0), Rational(2));  // beyond node_end=1
  ASSERT_TRUE(lane->add_pedal_span(stave_b, bad_span).ok());

  // A valid edit on stave_a validates only stave_a.
  const PedalSpan good_span =
      make_pedal_span(Rational(0), *Rational::create(1, 4));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   stave_a, good_span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<PedalSpan>* spans_a = lane->pedal_spans(stave_a);
  ASSERT_NE(spans_a, nullptr);
  ASSERT_EQ(spans_a->size(), 1u);
  EXPECT_EQ(spans_a->front(), good_span);
}

// =====================================================================
// Phase 8e-ii — Public add API cross-kind identity scope
// =====================================================================

TEST(CommandTest, VoiceContentPublicAddSlurDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Slur             slur{NotationEntityId::generate(), eid, eid};
  ASSERT_TRUE(voice.add_slur(slur).ok());
  EXPECT_FALSE(voice.add_slur(slur).ok());
}

TEST(CommandTest, VoiceContentPublicAddHairpinDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Hairpin          hp{NotationEntityId::generate(), eid, eid,
                   HairpinDirection::kCrescendo};
  ASSERT_TRUE(voice.add_hairpin(hp).ok());
  EXPECT_FALSE(voice.add_hairpin(hp).ok());
}

TEST(CommandTest, VoiceContentPublicAddSlurCrossKindRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid       = event_id(voice.events()[0]);
  const NotationEntityId shared_id = NotationEntityId::generate();

  const DynamicMarking dyn{shared_id, eid, Dynamic::kFf};
  ASSERT_TRUE(voice.add_dynamic(dyn).ok());

  const Slur slur{shared_id, eid, eid};
  EXPECT_FALSE(voice.add_slur(slur).ok());
}

TEST(CommandTest, VoiceContentPublicAddGraceGroupDuplicateRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice.events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());
  EXPECT_FALSE(voice.add_grace_group(group).ok());
}

// Phase 8f-i follow-up — principal_event self-collision rejection

TEST(CommandTest, AddGraceGroupRejectsPrincipalEventEqualToGroupId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Construct a group whose own id doubles as the principal_event.
  const NotationEntityId shared_id = NotationEntityId::generate();
  const GraceGroup       bad_group =
      GraceGroup{shared_id,
                 shared_id,
                 {GraceNote{NotationEntityId::generate(), pitch_d4(), eighth(),
                            GraceNoteType::kAppoggiatura, false}}};
  EXPECT_FALSE(voice.add_grace_group(bad_group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

TEST(CommandTest, AddGraceGroupRejectsPrincipalEventEqualToGraceNoteId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Construct a group where a GraceNote.id equals the principal_event.
  const NotationEntityId shared_id = NotationEntityId::generate();
  const GraceGroup       bad_group =
      GraceGroup{NotationEntityId::generate(),
                 shared_id,
                 {GraceNote{shared_id, pitch_d4(), eighth(),
                            GraceNoteType::kAppoggiatura, false}}};
  EXPECT_FALSE(voice.add_grace_group(bad_group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddBeamOverrideDuplicateRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice.events()[0]), event_id(voice.events()[1])});
  ASSERT_TRUE(voice.add_beam_override(beam).ok());
  EXPECT_FALSE(voice.add_beam_override(beam).ok());
}

// Phase 8f-i — nil-ID rejection for every public marking insertion API

TEST(CommandTest, VoiceContentPublicAddDynamicNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const DynamicMarking   bad{NotationEntityId{}, eid, Dynamic::kMf};
  EXPECT_FALSE(voice.add_dynamic(bad).ok());
  EXPECT_EQ(voice.dynamics().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddHairpinNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Hairpin bad{NotationEntityId{}, eid, eid, HairpinDirection::kCrescendo};
  EXPECT_FALSE(voice.add_hairpin(bad).ok());
  EXPECT_EQ(voice.hairpins().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddSlurNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Slur             bad{NotationEntityId{}, eid, eid};
  EXPECT_FALSE(voice.add_slur(bad).ok());
  EXPECT_EQ(voice.slurs().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddBeamOverrideNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());

  const BeamOverride bad{
      NotationEntityId{},
      BeamOverride::Kind::kJoin,
      {event_id(voice.events()[0]), event_id(voice.events()[1])}};
  EXPECT_FALSE(voice.add_beam_override(bad).ok());
  EXPECT_EQ(voice.beam_overrides().size(), 0u);
}

TEST(CommandTest, TrackLaneAddPedalSpanNilIdRejected) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const PedalSpan bad{NotationEntityId{}, Rational(0), *Rational::create(1, 4)};
  EXPECT_FALSE(lane.add_pedal_span(stave, bad).ok());
  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  EXPECT_TRUE(spans == nullptr || spans->empty());
}

TEST(CommandTest, TrackLaneAddPedalSpanCrossStaveDuplicateRejected) {
  TrackLane     lane;
  const StaveId stave_a = StaveId::generate();
  const StaveId stave_b = StaveId::generate();
  lane.ensure_stave(stave_a);
  lane.ensure_stave(stave_b);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave_a, span).ok());
  // Same id on a different stave should be rejected.
  EXPECT_FALSE(lane.add_pedal_span(stave_b, span).ok());
}

// =====================================================================
// Phase 8e-ii — Execute/undo/redo exact round-trip for every 12 commands
// =====================================================================

TEST(CommandTest, AddDynamicExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kFf);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveDynamicExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kFf);
  ASSERT_TRUE(voice->add_dynamic(marking).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, AddHairpinExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hp =
      make_hairpin(event_id(voice->events()[0]), event_id(voice->events()[1]),
                   HairpinDirection::kCrescendo);

  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveHairpinExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hp =
      make_hairpin(event_id(voice->events()[0]), event_id(voice->events()[1]),
                   HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice->add_hairpin(hp).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);
}

TEST(CommandTest, AddSlurExactExecuteUndoRedo) {
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
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveSlurExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));
  ASSERT_TRUE(voice->add_slur(slur).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);
}

TEST(CommandTest, AddBeamOverrideExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});

  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveBeamOverrideExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});
  ASSERT_TRUE(voice->add_beam_override(beam).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);
}

TEST(CommandTest, AddGraceGroupExactExecuteUndoRedo) {
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
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});

  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveGraceGroupExactExecuteUndoRedo) {
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
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});
  ASSERT_TRUE(voice->add_grace_group(group).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
}

TEST(CommandTest, AddPedalSpanExactExecuteUndoRedo) {
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
  const TrackLane post_exec = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(lane->pedal_spans(fx.stave_id), nullptr);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_exec);
}

TEST(CommandTest, RemovePedalSpanExactExecuteUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());
  const TrackLane pre_exec = *lane;

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  // Whole-lane snapshot may preserve the key with an empty vector.
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  EXPECT_TRUE(spans == nullptr || spans->empty());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Whole-lane snapshot may preserve the key with an empty vector.
  const std::vector<PedalSpan>* spans_redo = lane->pedal_spans(fx.stave_id);
  EXPECT_TRUE(spans_redo == nullptr || spans_redo->empty());
}

TEST(CommandTest, PedalCommandsValidateOnlyTheAddressedStave) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);
  const StaveId unrelated = StaveId::generate();
  lane->ensure_stave(unrelated);
  // The unrelated stave's four voices deliberately remain incomplete.

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto add = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(add->execute(fx.project).ok());
  ASSERT_TRUE(add->undo(fx.project).ok());
  ASSERT_TRUE(add->redo(fx.project).ok());

  auto remove = std::make_unique<RemovePedalSpanCommand>(
      fx.node_id, fx.track_id, fx.stave_id, span.id);
  ASSERT_TRUE(remove->execute(fx.project).ok());
  ASSERT_TRUE(remove->undo(fx.project).ok());
  ASSERT_TRUE(remove->redo(fx.project).ok());
}
