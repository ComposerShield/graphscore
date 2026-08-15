// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"
#include "command_test_tempo_support.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8h-ii — non-length-changing node-timeline commands
// =========================================================================

namespace {

struct TimelineCommandSetup {
  Project project;
  NodeId  node_id;
  StaveId upper_stave;
  StaveId lower_stave;
};

TimelineCommandSetup make_timeline_command_setup() {
  Project       project = make_project();
  const NodeId  node_id = project.add_node("Timeline Node");
  const StaveId upper   = StaveId::generate();
  const StaveId lower   = StaveId::generate();
  const std::vector<graphscore::StaveDefinition> staves = {
      {upper, Clef::kTreble}, {lower, Clef::kBass}};
  const std::vector<Measure> measures = {
      {*TimeSignature::create(3, 4), *KeySignature::create(0)},
      {*TimeSignature::create(4, 4), *KeySignature::create(1)},
      {*TimeSignature::create(5, 8), *KeySignature::create(-1)},
  };
  auto timeline = NodeTimeline::create(measures, staves);
  assert(timeline.has_value());
  [[maybe_unused]] const Result pickdown_result =
      timeline->set_pickdown(*Rational::create(1, 4));
  assert(pickdown_result.ok());
  [[maybe_unused]] const Result tempo_result = timeline->set_tempo(
      {tempo_point(Rational(0), 120), tempo_point(Rational(2), 90)});
  assert(tempo_result.ok());
  [[maybe_unused]] const Result upper_clef_result =
      timeline->add_clef_change(upper, *Rational::create(1, 2), Clef::kAlto);
  assert(upper_clef_result.ok());
  [[maybe_unused]] const Result lower_clef_result =
      timeline->add_clef_change(lower, Rational(1), Clef::kTenor);
  assert(lower_clef_result.ok());
  project.find_node(node_id)->set_timeline(std::move(*timeline));
  return {std::move(project), node_id, upper, lower};
}

NodeTimeline* timeline_of(TimelineCommandSetup* setup) {
  return setup->project.find_node(setup->node_id)->timeline();
}

std::optional<TempoLane> snapshot_tempo(const NodeTimeline& timeline) {
  const TempoLane* lane = timeline.tempo();
  return lane == nullptr ? std::nullopt : std::optional<TempoLane>(*lane);
}

void expect_tempo(const NodeTimeline&             timeline,
                  const std::optional<TempoLane>& expected) {
  const TempoLane* lane = timeline.tempo();
  ASSERT_EQ(lane != nullptr, expected.has_value());
  if (expected.has_value())
    EXPECT_EQ(*lane, *expected);
}

void expect_full_lifecycle(Command* command, Project* project) {
  EXPECT_EQ(command->undo(*project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command->redo(*project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(command->execute(*project).ok());
  EXPECT_EQ(command->execute(*project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command->redo(*project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(command->undo(*project).ok());
  EXPECT_EQ(command->undo(*project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(command->redo(*project).ok());
}

NodeTimeline make_different_length_timeline(
    const TimelineCommandSetup& setup, const std::optional<Rational>& pickdown,
    const std::vector<TempoPoint>& tempo_points) {
  const std::vector<graphscore::StaveDefinition> staves = {
      {setup.upper_stave, Clef::kTreble}, {setup.lower_stave, Clef::kBass}};
  const std::vector<Measure> measures = {
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
      {*TimeSignature::create(4, 4), *KeySignature::create(1)},
      {*TimeSignature::create(5, 8), *KeySignature::create(-1)},
  };
  auto timeline = NodeTimeline::create(measures, staves);
  assert(timeline.has_value());
  if (pickdown.has_value()) {
    [[maybe_unused]] const Result pickdown_result =
        timeline->set_pickdown(*pickdown);
    assert(pickdown_result.ok());
  }
  [[maybe_unused]] const Result tempo_result =
      timeline->set_tempo(tempo_points);
  assert(tempo_result.ok());
  return std::move(*timeline);
}

template <typename PickdownCommand>
void expect_tempo_end_stale_undo_redo_retry(PickdownCommand       command,
                                            TimelineCommandSetup* setup) {
  Node* node = setup->project.find_node(setup->node_id);
  assert(node != nullptr);
  assert(node->timeline() != nullptr);
  const NodeTimeline expected_before = *node->timeline();

  ASSERT_TRUE(command.execute(setup->project).ok());
  const NodeTimeline expected_after = *node->timeline();
  ASSERT_NE(expected_before.tempo(), nullptr);
  ASSERT_NE(expected_after.tempo(), nullptr);

  NodeTimeline stale_after =
      make_different_length_timeline(*setup, expected_after.pickdown_duration(),
                                     expected_after.tempo()->points());
  ASSERT_EQ(stale_after.pickdown_duration(),
            expected_after.pickdown_duration());
  ASSERT_EQ(stale_after.tempo()->points(), expected_after.tempo()->points());
  ASSERT_NE(stale_after.tempo()->end(), expected_after.tempo()->end());
  const Rational stale_after_length = stale_after.measures().total_length();
  const auto     stale_after_tempo  = snapshot_tempo(stale_after);
  node->set_timeline(std::move(stale_after));

  EXPECT_EQ(command.undo(setup->project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(node->timeline()->pickdown_duration(),
            expected_after.pickdown_duration());
  EXPECT_EQ(node->timeline()->measures().total_length(), stale_after_length);
  expect_tempo(*node->timeline(), stale_after_tempo);

  node->set_timeline(expected_after);
  ASSERT_TRUE(command.undo(setup->project).ok());
  expect_tempo(*node->timeline(), snapshot_tempo(expected_before));

  NodeTimeline stale_before = make_different_length_timeline(
      *setup, expected_before.pickdown_duration(),
      expected_before.tempo()->points());
  ASSERT_EQ(stale_before.pickdown_duration(),
            expected_before.pickdown_duration());
  ASSERT_EQ(stale_before.tempo()->points(), expected_before.tempo()->points());
  ASSERT_NE(stale_before.tempo()->end(), expected_before.tempo()->end());
  const Rational stale_before_length = stale_before.measures().total_length();
  const auto     stale_before_tempo  = snapshot_tempo(stale_before);
  node->set_timeline(std::move(stale_before));

  EXPECT_EQ(command.redo(setup->project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(node->timeline()->pickdown_duration(),
            expected_before.pickdown_duration());
  EXPECT_EQ(node->timeline()->measures().total_length(), stale_before_length);
  expect_tempo(*node->timeline(), stale_before_tempo);

  node->set_timeline(expected_before);
  ASSERT_TRUE(command.redo(setup->project).ok());
  expect_tempo(*node->timeline(), snapshot_tempo(expected_after));
}

}  // namespace

TEST(CommandTest, TimelineCommandsAreNoexcept) {
  static_assert(noexcept(std::declval<SetMeasureKeySignatureCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(std::declval<SetMeasureKeySignatureCommand&>().undo(
      std::declval<Project&>())));
  static_assert(noexcept(std::declval<SetMeasureKeySignatureCommand&>().redo(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddClefChangeCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddClefChangeCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddClefChangeCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(std::declval<RemoveClefChangeCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveClefChangeCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveClefChangeCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(std::declval<MoveClefChangeCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveClefChangeCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveClefChangeCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetPickdownCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetPickdownCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetPickdownCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<ClearPickdownCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<ClearPickdownCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<ClearPickdownCommand&>().redo(std::declval<Project&>())));
}

TEST(CommandTest, SetMeasureKeySignatureFirstMiddleLastExactRoundTrips) {
  auto          setup    = make_timeline_command_setup();
  NodeTimeline* timeline = timeline_of(&setup);
  ASSERT_NE(timeline, nullptr);
  const graphscore::MeasureMap original_measures = timeline->measures();
  const auto     original_pickdown = timeline->pickdown_duration();
  const auto     original_tempo    = timeline->tempo()->points();
  const ClefLane original_upper    = *timeline->clef_lane(setup.upper_stave);
  const ClefLane original_lower    = *timeline->clef_lane(setup.lower_stave);
  const Rational original_boundary = timeline->boundary_position();
  const Rational original_end      = timeline->node_end();

  const std::vector<std::pair<std::size_t, KeySignature>> edits = {
      {0, *KeySignature::create(-7)},
      {1, *KeySignature::create(4, graphscore::Mode::kMinor)},
      {2, *KeySignature::create(7)},
  };
  for (const auto& [index, key] : edits) {
    const Measure                 before = timeline->measures().measure(index);
    SetMeasureKeySignatureCommand command(setup.node_id, index, key);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const Measure after = timeline->measures().measure(index);
    EXPECT_EQ(after.time_signature, before.time_signature);
    EXPECT_EQ(after.key_signature, key);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->measures().measure(index), before);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->measures().measure(index), after);
  }

  EXPECT_EQ(timeline->measures().measure_count(),
            original_measures.measure_count());
  for (std::size_t index = 0; index < original_measures.measure_count();
       ++index) {
    EXPECT_EQ(timeline->measures().measure(index).time_signature,
              original_measures.measure(index).time_signature);
    EXPECT_EQ(timeline->measures().measure_start(index),
              original_measures.measure_start(index));
    EXPECT_EQ(timeline->measures().measure_length(index),
              original_measures.measure_length(index));
  }
  EXPECT_EQ(timeline->boundary_position(), original_boundary);
  EXPECT_EQ(timeline->node_end(), original_end);
  EXPECT_EQ(timeline->pickdown_duration(), original_pickdown);
  EXPECT_EQ(timeline->tempo()->points(), original_tempo);
  EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), original_upper);
  EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), original_lower);
}

TEST(CommandTest, ClefAddRemoveMoveExactRoundTripsAndIndependentStaves) {
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    const ClefLane       before   = *timeline->clef_lane(setup.upper_stave);
    const ClefLane       other    = *timeline->clef_lane(setup.lower_stave);
    AddClefChangeCommand command(setup.node_id, setup.upper_stave, Rational(2),
                                 Clef::kBass);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), before);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), after);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
  }
  {
    auto                    setup    = make_timeline_command_setup();
    NodeTimeline*           timeline = timeline_of(&setup);
    const ClefLane          before   = *timeline->clef_lane(setup.lower_stave);
    const ClefLane          other    = *timeline->clef_lane(setup.upper_stave);
    RemoveClefChangeCommand command(setup.node_id, setup.lower_stave,
                                    Rational(1));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.lower_stave);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), other);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), before);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), other);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), after);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), other);
  }
  {
    auto                  setup    = make_timeline_command_setup();
    NodeTimeline*         timeline = timeline_of(&setup);
    const ClefLane        before   = *timeline->clef_lane(setup.upper_stave);
    const ClefLane        other    = *timeline->clef_lane(setup.lower_stave);
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), before);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), after);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
  }
}

TEST(CommandTest, PickdownSetReplacementAndClearExactRoundTrips) {
  {
    auto               setup        = make_timeline_command_setup();
    NodeTimeline*      timeline     = timeline_of(&setup);
    const auto         before_tempo = snapshot_tempo(*timeline);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());
    EXPECT_EQ(after_tempo->points(), before_tempo->points());
    EXPECT_EQ(after_tempo->start(), Rational(0));
    EXPECT_EQ(after_tempo->end(), timeline->node_end());
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 4));
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    expect_tempo(*timeline, after_tempo);
  }
  {
    auto                 setup        = make_timeline_command_setup();
    NodeTimeline*        timeline     = timeline_of(&setup);
    const auto           before_tempo = snapshot_tempo(*timeline);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    EXPECT_FALSE(timeline->pickdown_duration().has_value());
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());
    EXPECT_EQ(after_tempo->points(), before_tempo->points());
    EXPECT_EQ(after_tempo->start(), Rational(0));
    EXPECT_EQ(after_tempo->end(), timeline->boundary_position());
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 4));
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_FALSE(timeline->pickdown_duration().has_value());
    expect_tempo(*timeline, after_tempo);
  }
  {
    auto setup = make_timeline_command_setup();
    ASSERT_TRUE(timeline_of(&setup)->clear_pickdown().ok());
    const auto         before_tempo = snapshot_tempo(*timeline_of(&setup));
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 4));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline_of(&setup));
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_FALSE(timeline_of(&setup)->pickdown_duration().has_value());
    expect_tempo(*timeline_of(&setup), before_tempo);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline_of(&setup)->pickdown_duration(),
              *Rational::create(1, 4));
    expect_tempo(*timeline_of(&setup), after_tempo);
  }
}

TEST(CommandTest, PickdownCommandsPreserveAbsentTempoLane) {
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    timeline->clear_tempo();
    ASSERT_EQ(timeline->tempo(), nullptr);

    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
  }
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    timeline->clear_tempo();
    ASSERT_EQ(timeline->tempo(), nullptr);

    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
  }
}

TEST(CommandTest, TimelineCommandFamilyRejectsIllegalLifecycleCalls) {
  {
    auto                          setup = make_timeline_command_setup();
    SetMeasureKeySignatureCommand command(setup.node_id, 0,
                                          *KeySignature::create(3));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                 setup = make_timeline_command_setup();
    AddClefChangeCommand command(setup.node_id, setup.upper_stave, Rational(2),
                                 Clef::kBass);
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                    setup = make_timeline_command_setup();
    RemoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                    *Rational::create(1, 2));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                  setup = make_timeline_command_setup();
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto               setup = make_timeline_command_setup();
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                 setup = make_timeline_command_setup();
    ClearPickdownCommand command(setup.node_id);
    expect_full_lifecycle(&command, &setup.project);
  }
}

TEST(CommandTest, TimelineCommandsRejectMissingNodeAndTimelinelessNode) {
  auto         setup       = make_timeline_command_setup();
  const NodeId missing     = NodeId::generate();
  const NodeId no_timeline = setup.project.add_node("No timeline");
  for (const NodeId node : {missing, no_timeline}) {
    SetMeasureKeySignatureCommand key(node, 0, *KeySignature::create(2));
    AddClefChangeCommand add(node, setup.upper_stave, Rational(2), Clef::kBass);
    RemoveClefChangeCommand remove(node, setup.upper_stave, Rational(0));
    MoveClefChangeCommand   move(node, setup.upper_stave, Rational(0),
                                 Rational(1));
    SetPickdownCommand      set(node, *Rational::create(1, 4));
    ClearPickdownCommand    clear(node);
    EXPECT_EQ(key.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(add.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(remove.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(move.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(set.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(clear.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
  }
}

TEST(CommandTest, TimelineCommandsRejectInvalidTargetsWithoutMutation) {
  auto                         setup    = make_timeline_command_setup();
  NodeTimeline*                timeline = timeline_of(&setup);
  const graphscore::MeasureMap measures = timeline->measures();
  const ClefLane               upper = *timeline->clef_lane(setup.upper_stave);
  const auto                   pickdown      = timeline->pickdown_duration();
  const StaveId                missing_stave = StaveId::generate();

  SetMeasureKeySignatureCommand bad_index(setup.node_id, 3,
                                          *KeySignature::create(2));
  AddClefChangeCommand          duplicate(setup.node_id, setup.upper_stave,
                                          *Rational::create(1, 2), Clef::kBass);
  AddClefChangeCommand negative(setup.node_id, setup.upper_stave, Rational(-1),
                                Clef::kBass);
  AddClefChangeCommand missing_stave_add(setup.node_id, missing_stave,
                                         Rational(0), Clef::kBass);
  RemoveClefChangeCommand missing_position(setup.node_id, setup.upper_stave,
                                           Rational(3));
  RemoveClefChangeCommand negative_remove(setup.node_id, setup.upper_stave,
                                          Rational(-1));
  MoveClefChangeCommand   missing_move(setup.node_id, setup.upper_stave,
                                       Rational(3), Rational(4));
  MoveClefChangeCommand   negative_move(setup.node_id, setup.upper_stave,
                                        *Rational::create(1, 2), Rational(-1));
  ASSERT_TRUE(
      timeline->add_clef_change(setup.upper_stave, Rational(2), Clef::kTenor)
          .ok());
  const ClefLane upper_with_target = *timeline->clef_lane(setup.upper_stave);
  MoveClefChangeCommand occupied_now(setup.node_id, setup.upper_stave,
                                     *Rational::create(1, 2), Rational(2));

  EXPECT_EQ(bad_index.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(duplicate.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(negative.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(missing_stave_add.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(missing_position.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(negative_remove.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(missing_move.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(negative_move.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(occupied_now.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(timeline->measures(), measures);
  EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), upper_with_target);
  EXPECT_EQ(timeline->pickdown_duration(), pickdown);
  EXPECT_NE(upper, upper_with_target);
}

TEST(CommandTest, SetPickdownRejectsZeroNegativeAndBoundaryAtomically) {
  auto           setup        = make_timeline_command_setup();
  NodeTimeline*  timeline     = timeline_of(&setup);
  const auto     before       = timeline->pickdown_duration();
  const auto     tempo_before = timeline->tempo()->points();
  const Rational boundary_measure_length =
      timeline->measures().measure_length(2);
  for (const Rational invalid :
       {Rational(0), Rational(-1), boundary_measure_length, Rational(1)}) {
    SetPickdownCommand command(setup.node_id, invalid);
    EXPECT_EQ(command.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(timeline->pickdown_duration(), before);
    EXPECT_EQ(timeline->tempo()->points(), tempo_before);
  }
}

TEST(CommandTest, PickdownCommandsPreserveTempoFailureAtomicity) {
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 2)).ok());
    ASSERT_TRUE(timeline
                    ->set_tempo({tempo_point(Rational(0), 120),
                                 tempo_point(*Rational::create(11, 4), 90)})
                    .ok());
    const auto         tempo_before = timeline->tempo()->points();
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 4));
    EXPECT_EQ(command.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    EXPECT_EQ(timeline->tempo()->points(), tempo_before);
  }
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 2)).ok());
    ASSERT_TRUE(timeline
                    ->set_tempo({tempo_point(Rational(0), 120),
                                 tempo_point(*Rational::create(11, 4), 90)})
                    .ok());
    const auto           tempo_before = timeline->tempo()->points();
    ClearPickdownCommand command(setup.node_id);
    EXPECT_EQ(command.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    EXPECT_EQ(timeline->tempo()->points(), tempo_before);
  }
}

TEST(CommandTest, KeyAndClefCommandsRejectStaleUndoAndRedoRetryably) {
  {
    auto                          setup    = make_timeline_command_setup();
    NodeTimeline*                 timeline = timeline_of(&setup);
    const Measure                 before   = timeline->measures().measure(0);
    SetMeasureKeySignatureCommand command(setup.node_id, 0,
                                          *KeySignature::create(3));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const Measure after = timeline->measures().measure(0);
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, *KeySignature::create(4)).ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, after.key_signature).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, *KeySignature::create(5)).ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, before.key_signature).ok());
    EXPECT_TRUE(command.redo(setup.project).ok());
  }
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    const ClefLane       before   = *timeline->clef_lane(setup.upper_stave);
    AddClefChangeCommand command(setup.node_id, setup.upper_stave, Rational(2),
                                 Clef::kBass);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.upper_stave);
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), before);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), after);
  }
}

TEST(CommandTest, RemoveMoveClefAndPickdownRejectStaleContexts) {
  {
    auto                    setup    = make_timeline_command_setup();
    NodeTimeline*           timeline = timeline_of(&setup);
    RemoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                    *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
  }
  {
    auto                  setup    = make_timeline_command_setup();
    NodeTimeline*         timeline = timeline_of(&setup);
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
  }
  {
    auto               setup    = make_timeline_command_setup();
    NodeTimeline*      timeline = timeline_of(&setup);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
  }
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
  }
}

TEST(CommandTest, RemoveMoveClefAndPickdownRejectStaleRedoContexts) {
  {
    auto                    setup    = make_timeline_command_setup();
    NodeTimeline*           timeline = timeline_of(&setup);
    RemoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                    *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
  }
  {
    auto                  setup    = make_timeline_command_setup();
    NodeTimeline*         timeline = timeline_of(&setup);
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
  }
  {
    auto               setup    = make_timeline_command_setup();
    NodeTimeline*      timeline = timeline_of(&setup);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
  }
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
  }
}

TEST(CommandTest, PickdownCommandsRejectStaleTempoUndoAndRedoRetryably) {
  {
    auto               setup        = make_timeline_command_setup();
    NodeTimeline*      timeline     = timeline_of(&setup);
    const auto         before_tempo = snapshot_tempo(*timeline);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());

    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 60)}).ok());
    const auto stale_undo_tempo    = snapshot_tempo(*timeline);
    const auto stale_undo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_undo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_undo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(after_tempo->points()).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 72)}).ok());
    const auto stale_redo_tempo    = snapshot_tempo(*timeline);
    const auto stale_redo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_redo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_redo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(before_tempo->points()).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
    expect_tempo(*timeline, after_tempo);
  }
  {
    auto                 setup        = make_timeline_command_setup();
    NodeTimeline*        timeline     = timeline_of(&setup);
    const auto           before_tempo = snapshot_tempo(*timeline);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());

    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 60)}).ok());
    const auto stale_undo_tempo    = snapshot_tempo(*timeline);
    const auto stale_undo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_undo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_undo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(after_tempo->points()).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 72)}).ok());
    const auto stale_redo_tempo    = snapshot_tempo(*timeline);
    const auto stale_redo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_redo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_redo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(before_tempo->points()).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
    expect_tempo(*timeline, after_tempo);
  }
}

TEST(CommandTest, PickdownCommandsRejectTempoEndStaleUndoRedoRetryably) {
  {
    auto setup = make_timeline_command_setup();
    expect_tempo_end_stale_undo_redo_retry(
        SetPickdownCommand(setup.node_id, *Rational::create(1, 2)), &setup);
  }
  {
    auto setup = make_timeline_command_setup();
    expect_tempo_end_stale_undo_redo_retry(ClearPickdownCommand(setup.node_id),
                                           &setup);
  }
}
