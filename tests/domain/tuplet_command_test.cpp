// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>
#include "command/command_test_support.hpp"

namespace graphscore {
namespace {

constexpr Voice kVoice = *Voice::create(1);

VoiceContent* voice_for(NotationSetup& setup) {
  return &setup.project.find_node(setup.node_id)
              ->lane(setup.track_id)
              ->stave(setup.stave_id)
              ->voice(kVoice);
}

std::vector<NotationEntityId> append_mixed_eighths(VoiceContent* voice) {
  const Duration          duration = *Duration::create(NoteValue::kEighth, 0);
  std::vector<VoiceEvent> events{
      make_note(pitch_c4(), duration),
      make_chord(duration, {{.pitch = pitch_c4()}, {.pitch = pitch_e4()}}),
      make_rest(duration)};
  std::vector<NotationEntityId> ids;
  for (const VoiceEvent& event : events) {
    ids.push_back(event_id(event));
    EXPECT_TRUE(voice->append(event).ok());
  }
  return ids;
}

TupletCommand create(NotationSetup& setup, std::vector<NotationEntityId> ids,
                     std::uint16_t played = 3, std::uint16_t normal = 2) {
  return TupletCommand::create_group(setup.node_id, setup.track_id,
                                     setup.stave_id, kVoice, std::move(ids),
                                     *TupletRatio::create(played, normal));
}

}  // namespace

TEST(TupletCommandTest, CreatesArbitraryRatioAcrossMixedEvents) {
  NotationSetup  setup     = make_notation_setup();
  VoiceContent*  voice     = voice_for(setup);
  const Duration sixteenth = *Duration::create(NoteValue::kSixteenth, 0);
  std::vector<NotationEntityId> ids;
  for (std::size_t index = 0; index < 5; ++index) {
    VoiceEvent event = make_note(pitch_c4(), sixteenth);
    if (index == 1) {
      event =
          make_chord(sixteenth, {{.pitch = pitch_c4()}, {.pitch = pitch_e4()}});
    } else if (index == 3) {
      event = make_rest(sixteenth);
    }
    ids.push_back(event_id(event));
    ASSERT_TRUE(voice->append(event).ok());
  }
  ASSERT_TRUE(voice->normalize(setup.node_end).ok());

  TupletCommand command = create(setup, ids, 5, 4);
  ASSERT_TRUE(command.execute(setup.project).ok());
  for (std::size_t index = 0; index < ids.size(); ++index) {
    EXPECT_EQ(event_tuplet_group(voice->events()[index]), command.group_id());
    EXPECT_EQ(event_duration(voice->events()[index]).tuplet(),
              TupletRatio::create(5, 4));
  }
  EXPECT_TRUE(voice->check_complete(setup.node_end).ok());
  EXPECT_TRUE(validate_voice_references(*voice).empty());
}

TEST(TupletCommandTest, CreateChangeRemoveHaveStableUndoRedoIdentity) {
  NotationSetup                       setup = make_notation_setup();
  VoiceContent*                       voice = voice_for(setup);
  const std::vector<NotationEntityId> ids   = append_mixed_eighths(voice);
  ASSERT_TRUE(voice->normalize(setup.node_end).ok());
  const VoiceContent original = *voice;

  TupletCommand       create_command = create(setup, ids);
  const TupletGroupId group          = create_command.group_id();
  ASSERT_TRUE(create_command.execute(setup.project).ok());
  const VoiceContent created = *voice;
  ASSERT_TRUE(create_command.undo(setup.project).ok());
  EXPECT_EQ(*voice, original);
  ASSERT_TRUE(create_command.redo(setup.project).ok());
  EXPECT_EQ(*voice, created);
  EXPECT_EQ(event_tuplet_group(voice->events()[0]), group);

  TupletCommand change =
      TupletCommand::change_group(setup.node_id, setup.track_id, setup.stave_id,
                                  kVoice, group, *TupletRatio::create(3, 1));
  ASSERT_TRUE(change.execute(setup.project).ok());
  EXPECT_EQ(event_duration(voice->events()[0]).tuplet(),
            TupletRatio::create(3, 1));
  EXPECT_EQ(event_tuplet_group(voice->events()[0]), group);
  ASSERT_TRUE(change.undo(setup.project).ok());
  EXPECT_EQ(*voice, created);
  ASSERT_TRUE(change.redo(setup.project).ok());

  TupletCommand remove = TupletCommand::remove_group(
      setup.node_id, setup.track_id, setup.stave_id, kVoice, group);
  ASSERT_TRUE(remove.execute(setup.project).ok());
  EXPECT_FALSE(event_duration(voice->events()[0]).tuplet().has_value());
  EXPECT_FALSE(event_tuplet_group(voice->events()[0]).has_value());
  ASSERT_TRUE(remove.undo(setup.project).ok());
  EXPECT_EQ(event_tuplet_group(voice->events()[0]), group);
  ASSERT_TRUE(remove.redo(setup.project).ok());
  EXPECT_TRUE(voice->check_complete(setup.node_end).ok());
}

TEST(TupletCommandTest, AdjacentIdenticalGroupsRemainDistinctAndSelectable) {
  NotationSetup                       setup  = make_notation_setup();
  VoiceContent*                       voice  = voice_for(setup);
  const std::vector<NotationEntityId> first  = append_mixed_eighths(voice);
  const std::vector<NotationEntityId> second = append_mixed_eighths(voice);
  ASSERT_TRUE(voice->normalize(setup.node_end).ok());

  TupletCommand left  = create(setup, first);
  TupletCommand right = create(setup, second);
  ASSERT_TRUE(left.execute(setup.project).ok());
  ASSERT_TRUE(right.execute(setup.project).ok());
  ASSERT_NE(left.group_id(), right.group_id());
  ASSERT_TRUE(validate_voice_references(*voice).empty());

  for (const NotationEntityId anchor : {first.front(), second.front()}) {
    const auto set = MarkingSet::create(
        {MarkingItem{setup.node_id, setup.track_id, setup.stave_id, kVoice,
                     MarkingKind::kTuplet, anchor, std::nullopt}});
    ASSERT_TRUE(set.has_value());
    EXPECT_TRUE(validate_selection(setup.project, Selection(*set)).empty());
  }
}

TEST(TupletCommandTest, RejectsNestedPartialMixedAndInvalidRatioAtomically) {
  NotationSetup                       setup = make_notation_setup();
  VoiceContent*                       voice = voice_for(setup);
  const std::vector<NotationEntityId> ids   = append_mixed_eighths(voice);
  ASSERT_TRUE(voice->normalize(setup.node_end).ok());

  GraceGroup grace = make_grace_group(
      ids.front(), {{.pitch = pitch_d4(), .duration = eighth()}});
  ASSERT_TRUE(voice->add_grace_group(grace).ok());
  TupletCommand      grace_only = create(setup, {grace.notes.front().id});
  const VoiceContent before     = *voice;
  EXPECT_FALSE(grace_only.execute(setup.project).ok());
  EXPECT_EQ(*voice, before);

  TupletCommand initial = create(setup, ids);
  ASSERT_TRUE(initial.execute(setup.project).ok());
  const VoiceContent grouped = *voice;

  TupletCommand nested = create(setup, ids);
  EXPECT_FALSE(nested.execute(setup.project).ok());
  EXPECT_EQ(*voice, grouped);

  TupletCommand partial = create(setup, {ids[0], ids[2]});
  EXPECT_FALSE(partial.execute(setup.project).ok());
  EXPECT_EQ(*voice, grouped);

  TupletCommand incompatible = TupletCommand::change_group(
      setup.node_id, setup.track_id, setup.stave_id, kVoice, initial.group_id(),
      *TupletRatio::create(5, 4));
  EXPECT_FALSE(incompatible.execute(setup.project).ok());
  EXPECT_EQ(*voice, grouped);

  TupletCommand trivial = TupletCommand::change_group(
      setup.node_id, setup.track_id, setup.stave_id, kVoice, initial.group_id(),
      *TupletRatio::create(1, 1));
  EXPECT_FALSE(trivial.execute(setup.project).ok());
  EXPECT_EQ(*voice, grouped);
}

TEST(TupletCommandTest, ValidationRejectsMissingOrDiscontiguousGroupIdentity) {
  VoiceContent   voice;
  const Duration triplet =
      *Duration::create(NoteValue::kEighth, 0, TupletRatio::create(3, 2));
  Note missing         = make_note(pitch_c4(), eighth());
  missing.tuplet_group = TupletGroupId::generate();
  ASSERT_TRUE(voice.append(missing).ok());
  EXPECT_FALSE(validate_voice_references(voice).empty());

  VoiceContent        discontiguous;
  const TupletGroupId group = TupletGroupId::generate();
  Note                first = make_note(pitch_c4(), triplet);
  first.tuplet_group        = group;
  Note middle               = make_note(pitch_d4(), eighth());
  Note last                 = make_note(pitch_e4(), triplet);
  last.tuplet_group         = group;
  ASSERT_TRUE(discontiguous.append(first).ok());
  ASSERT_TRUE(discontiguous.append(middle).ok());
  ASSERT_TRUE(discontiguous.append(last).ok());
  EXPECT_FALSE(validate_voice_references(discontiguous).empty());
}

TEST(TupletCommandTest, SetEventCannotPartiallyEditTupletMembership) {
  NotationSetup                       setup = make_notation_setup();
  VoiceContent*                       voice = voice_for(setup);
  const std::vector<NotationEntityId> ids   = append_mixed_eighths(voice);
  ASSERT_TRUE(voice->normalize(setup.node_end).ok());
  TupletCommand group = create(setup, ids);
  ASSERT_TRUE(group.execute(setup.project).ok());
  const VoiceContent before = *voice;

  VoiceEvent replacement = voice->events().front();
  std::visit(
      [](auto& event) {
        event.duration = *Duration::create(NoteValue::kEighth, 0);
        event.tuplet_group.reset();
      },
      replacement);
  SetEventCommand partial(setup.node_id, setup.track_id, setup.stave_id, kVoice,
                          Rational(0), replacement);
  EXPECT_FALSE(partial.execute(setup.project).ok());
  EXPECT_EQ(*voice, before);
}

}  // namespace graphscore
