// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"
#include "command_test_tempo_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Node tempo-lane commands: AddTempoPointCommand, RemoveTempoPointCommand,
// MoveTempoPointCommand, SetTempoPointCommand
// =========================================================================

TEST(CommandTest, TempoPointCommandsAreNoexcept) {
  static_assert(noexcept(
      std::declval<AddTempoPointCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddTempoPointCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<RemoveTempoPointCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveTempoPointCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<MoveTempoPointCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveTempoPointCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTempoPointCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTempoPointCommand&>().redo(std::declval<Project&>())));
}

TEST(CommandTest, AddTempoPointRoundTrip) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(2), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100, TempoSegmentKind::kLinear));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 3u);
  EXPECT_EQ(after[0].position, Rational(0));
  EXPECT_EQ(after[1].position, Rational(1));
  EXPECT_EQ(after[2].position, Rational(2));
  EXPECT_EQ(after[1].segment_kind, TempoSegmentKind::kLinear);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, AddTempoPointCreatesLaneAndUndoRemovesItEntirely) {
  auto fx = make_tempo_setup();
  ASSERT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(0), 132, TempoSegmentKind::kSmooth));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TempoLane* lane = tempo_lane(fx.project, fx.node_id);
  ASSERT_NE(lane, nullptr);
  const std::vector<TempoPoint> created = lane->points();
  ASSERT_EQ(created.size(), 1u);
  EXPECT_EQ(created[0].segment_kind, TempoSegmentKind::kSmooth);
  EXPECT_EQ(lane->start(), Rational(0));
  EXPECT_EQ(lane->end(), Rational(4));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const TempoLane* recreated = tempo_lane(fx.project, fx.node_id);
  ASSERT_NE(recreated, nullptr);
  EXPECT_EQ(recreated->points(), created);
  EXPECT_EQ(recreated->end(), Rational(4));
}

TEST(CommandTest, AddTempoPointOnLanelessNodeRejectsNonZeroPosition) {
  auto fx = make_tempo_setup();

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);
}

TEST(CommandTest, AddTempoPointRejectsDuplicatePosition) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 60));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, AddTempoPointRejectsPositionAtNodeEndAndStaysFresh) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto rejected = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(4), 90));
  EXPECT_EQ(rejected->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // The failed execute left the command fresh, so a valid one still works.
  auto accepted = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(3), 90));
  EXPECT_TRUE(accepted->execute(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);
}

TEST(CommandTest, AddTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});

  auto fresh = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  EXPECT_EQ(fresh->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(fresh->execute(fx.project).ok());
  EXPECT_EQ(fresh->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fresh->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);
}

TEST(CommandTest, AddTempoPointUndoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // Someone else rewrote the lane behind the command's back.
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(2), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);
}

TEST(CommandTest, RemoveTempoPointRoundTrip) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120),
             tempo_point(Rational(1), 100, TempoSegmentKind::kLinear),
             tempo_point(Rational(2), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[0].position, Rational(0));
  EXPECT_EQ(after[1].position, Rational(2));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, RemoveSoleTempoPointClearsLaneAndUndoRestoresIt) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 144, TempoSegmentKind::kSmooth)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const TempoLane* restored = tempo_lane(fx.project, fx.node_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->points(), before);
  EXPECT_EQ(restored->points()[0].tempo,
            *Tempo::create(Rational(144), NoteValue::kQuarter));
  EXPECT_EQ(restored->points()[0].segment_kind, TempoSegmentKind::kSmooth);
  EXPECT_EQ(restored->start(), Rational(0));
  EXPECT_EQ(restored->end(), Rational(4));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);
}

TEST(CommandTest, RemoveTempoPointAtZeroWithLaterPointsFails) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(0));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, RemoveTempoPointMissingPositionOrLaneFails) {
  auto fx = make_tempo_setup();

  auto no_lane =
      std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(0));
  EXPECT_EQ(no_lane->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto missing = std::make_unique<RemoveTempoPointCommand>(
      fx.node_id, *Rational::create(1, 2));
  EXPECT_EQ(missing->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, RemoveTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 1u);
}

TEST(CommandTest, RemoveTempoPointUndoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(3), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);
}

TEST(CommandTest, MoveTempoPointRoundTrip) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     *Rational::create(3, 2));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[1].position, *Rational::create(3, 2));
  EXPECT_EQ(after[1].tempo, before[1].tempo);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, MoveTempoPointCrossingAnotherPointReSorts) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120),
             tempo_point(Rational(1), 100, TempoSegmentKind::kLinear),
             tempo_point(Rational(2), 90, TempoSegmentKind::kSmooth)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     Rational(3));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 3u);
  EXPECT_EQ(after[0].position, Rational(0));
  EXPECT_EQ(after[1].position, Rational(2));
  EXPECT_EQ(after[2].position, Rational(3));

  // The moved point carried its tempo and segment kind across the point it
  // passed; the point it passed is otherwise untouched.
  EXPECT_EQ(after[2].tempo, before[1].tempo);
  EXPECT_EQ(after[2].segment_kind, TempoSegmentKind::kLinear);
  EXPECT_EQ(after[1], before[2]);
  EXPECT_EQ(after[0], before[0]);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointToItsOwnPositionIsAnAcceptedNoOp) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     Rational(1));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointRejectsCollisionAndMissingSource) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 100),
             tempo_point(Rational(2), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto collide = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, Rational(1), Rational(2));
  EXPECT_EQ(collide->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  auto missing = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, *Rational::create(1, 2), Rational(3));
  EXPECT_EQ(missing->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointRejectsLeavingZeroOrLeavingTheNode) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  // A lane must begin exactly at its start, so the point at 0 cannot move
  // while later points remain.
  auto off_start = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, Rational(0), *Rational::create(1, 2));
  EXPECT_EQ(off_start->execute(fx.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // node_end() is exclusive: a point may not sit at or past it.
  auto past_end = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, Rational(1), Rational(4));
  EXPECT_EQ(past_end->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointWithoutLaneFails) {
  auto fx = make_tempo_setup();

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(0),
                                                     Rational(1));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);
}

TEST(CommandTest, MoveTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     Rational(2));
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id)[1].position, Rational(2));
}

TEST(CommandTest, SetTempoPointChangesOnlyTheTargetedPoint) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120),
             tempo_point(Rational(1), 100, TempoSegmentKind::kLinear),
             tempo_point(Rational(2), 90, TempoSegmentKind::kSmooth)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(1), *Tempo::create(Rational(66), NoteValue::kHalf),
      TempoSegmentKind::kSmooth);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 3u);
  EXPECT_EQ(after[0], before[0]);
  EXPECT_EQ(after[2], before[2]);
  EXPECT_EQ(after[1].position, Rational(1));
  EXPECT_EQ(after[1].tempo, *Tempo::create(Rational(66), NoteValue::kHalf));
  EXPECT_EQ(after[1].segment_kind, TempoSegmentKind::kSmooth);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, SetTempoPointMissingPositionOrLaneFails) {
  auto fx = make_tempo_setup();

  auto no_lane = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(0),
      *Tempo::create(Rational(100), NoteValue::kQuarter),
      TempoSegmentKind::kStep);
  EXPECT_EQ(no_lane->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto missing = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(1),
      *Tempo::create(Rational(100), NoteValue::kQuarter),
      TempoSegmentKind::kStep);
  EXPECT_EQ(missing->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, SetTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});

  auto cmd = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(0),
      *Tempo::create(Rational(60), NoteValue::kQuarter),
      TempoSegmentKind::kLinear);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id)[0].segment_kind,
            TempoSegmentKind::kLinear);
}

TEST(CommandTest, TempoPointCommandsRejectMissingNode) {
  auto         fx      = make_tempo_setup();
  const NodeId missing = NodeId::generate();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  AddTempoPointCommand add(missing, tempo_point(Rational(0), 100));
  EXPECT_EQ(add.execute(fx.project).code(), ResultCode::kInvalidArgument);

  RemoveTempoPointCommand remove(missing, Rational(0));
  EXPECT_EQ(remove.execute(fx.project).code(), ResultCode::kInvalidArgument);

  MoveTempoPointCommand move(missing, Rational(0), Rational(1));
  EXPECT_EQ(move.execute(fx.project).code(), ResultCode::kInvalidArgument);

  SetTempoPointCommand set(missing, Rational(0),
                           *Tempo::create(Rational(90), NoteValue::kQuarter),
                           TempoSegmentKind::kStep);
  EXPECT_EQ(set.execute(fx.project).code(), ResultCode::kInvalidArgument);

  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, TempoPointCommandsRejectTimelinelessNode) {
  Project      project = make_project();
  const NodeId node_id = project.add_node("No timeline");
  ASSERT_EQ(project.find_node(node_id)->timeline(), nullptr);

  AddTempoPointCommand add(node_id, tempo_point(Rational(0), 100));
  EXPECT_EQ(add.execute(project).code(), ResultCode::kInvalidArgument);

  RemoveTempoPointCommand remove(node_id, Rational(0));
  EXPECT_EQ(remove.execute(project).code(), ResultCode::kInvalidArgument);

  MoveTempoPointCommand move(node_id, Rational(0), Rational(1));
  EXPECT_EQ(move.execute(project).code(), ResultCode::kInvalidArgument);

  SetTempoPointCommand set(node_id, Rational(0),
                           *Tempo::create(Rational(90), NoteValue::kQuarter),
                           TempoSegmentKind::kStep);
  EXPECT_EQ(set.execute(project).code(), ResultCode::kInvalidArgument);

  EXPECT_EQ(project.find_node(node_id)->timeline(), nullptr);
}

TEST(CommandTest, TempoPointCommandsInterleavedUndoRedo) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> initial = tempo_points(fx.project, fx.node_id);

  auto add = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  auto set = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(0),
      *Tempo::create(Rational(90), NoteValue::kQuarter),
      TempoSegmentKind::kLinear);

  ASSERT_TRUE(add->execute(fx.project).ok());
  const std::vector<TempoPoint> after_add =
      tempo_points(fx.project, fx.node_id);
  ASSERT_TRUE(set->execute(fx.project).ok());
  const std::vector<TempoPoint> after_set =
      tempo_points(fx.project, fx.node_id);

  ASSERT_TRUE(set->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after_add);
  ASSERT_TRUE(add->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), initial);

  ASSERT_TRUE(add->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after_add);
  ASSERT_TRUE(set->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after_set);
}

TEST(CommandTest, TempoPointCommandStreamReplaysDeterministically) {
  const auto run = [](TempoSetup* fx) {
    AddTempoPointCommand add(fx->node_id, tempo_point(Rational(0), 120));
    AddTempoPointCommand add2(
        fx->node_id,
        tempo_point(*Rational::create(1, 2), 100, TempoSegmentKind::kLinear));
    MoveTempoPointCommand move(fx->node_id, *Rational::create(1, 2),
                               *Rational::create(3, 2));
    SetTempoPointCommand  set(fx->node_id, *Rational::create(3, 2),
                              *Tempo::create(Rational(72), NoteValue::kQuarter),
                              TempoSegmentKind::kSmooth);

    EXPECT_TRUE(add.execute(fx->project).ok());
    EXPECT_TRUE(add2.execute(fx->project).ok());
    EXPECT_TRUE(move.execute(fx->project).ok());
    EXPECT_TRUE(set.execute(fx->project).ok());
  };

  auto first  = make_tempo_setup();
  auto second = make_tempo_setup();
  run(&first);
  run(&second);

  EXPECT_EQ(tempo_points(first.project, first.node_id),
            tempo_points(second.project, second.node_id));
  EXPECT_EQ(tempo_points(first.project, first.node_id).size(), 2u);
}

// =========================================================================
// Phase 8e-iii — Tempo stale-context rejection and retryability
// =========================================================================

TEST(CommandTest, AddTempoPointRedoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Someone else rewrote the lane behind the command's back.
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(2), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  // The rejection left the command undone, so restoring the exact pre-edit
  // lane — the state redo verifies — lets the retried redo through.
  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, RemoveTempoPointRedoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(3), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, MoveTempoPointStaleContextRejectedOnUndoAndRedo) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     *Rational::create(3, 2));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);

  const std::vector<TempoPoint> foreign = {tempo_point(Rational(0), 60),
                                           tempo_point(Rational(2), 60)};

  // undo verifies the post-edit lane it left behind.
  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, after);
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // redo verifies the pre-edit lane instead.
  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, SetTempoPointStaleContextRejectedOnUndoAndRedo) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(1), *Tempo::create(Rational(66), NoteValue::kHalf),
      TempoSegmentKind::kSmooth);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);

  const std::vector<TempoPoint> foreign = {tempo_point(Rational(0), 60),
                                           tempo_point(Rational(2), 60)};

  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, after);
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, TempoPointUndoStaleContextRetryable) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> initial = tempo_points(fx.project, fx.node_id);

  auto add = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  ASSERT_TRUE(add->execute(fx.project).ok());
  const std::vector<TempoPoint> after_add =
      tempo_points(fx.project, fx.node_id);

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(3), 60)});
  EXPECT_EQ(add->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // The rejected undo left the command done, so restoring the exact
  // post-edit lane makes the retried undo succeed.
  seed_lane(&fx.project, fx.node_id, after_add);
  ASSERT_TRUE(add->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), initial);

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before_remove =
      tempo_points(fx.project, fx.node_id);

  auto remove =
      std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  ASSERT_TRUE(remove->execute(fx.project).ok());
  const std::vector<TempoPoint> after_remove =
      tempo_points(fx.project, fx.node_id);

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 72), tempo_point(Rational(2), 72)});
  EXPECT_EQ(remove->undo(fx.project).code(), ResultCode::kInvalidArgument);

  seed_lane(&fx.project, fx.node_id, after_remove);
  ASSERT_TRUE(remove->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before_remove);
}

TEST(CommandTest, TempoPointStaleContextAcrossLanePresence) {
  // Empty snapshot vs a lane that exists: the command created the lane,
  // then someone removed it behind the command's back.
  auto created = make_tempo_setup();
  ASSERT_EQ(tempo_lane(created.project, created.node_id), nullptr);

  auto add = std::make_unique<AddTempoPointCommand>(
      created.node_id, tempo_point(Rational(0), 132));
  ASSERT_TRUE(add->execute(created.project).ok());
  ASSERT_NE(tempo_lane(created.project, created.node_id), nullptr);

  created.project.find_node(created.node_id)->timeline()->clear_tempo();
  EXPECT_EQ(add->undo(created.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(created.project, created.node_id), nullptr);

  // A lane that exists vs an empty snapshot: the command removed the last
  // point, then someone seeded a fresh lane behind its back.
  auto cleared = make_tempo_setup();
  seed_lane(&cleared.project, cleared.node_id, {tempo_point(Rational(0), 144)});

  auto remove =
      std::make_unique<RemoveTempoPointCommand>(cleared.node_id, Rational(0));
  ASSERT_TRUE(remove->execute(cleared.project).ok());
  ASSERT_EQ(tempo_lane(cleared.project, cleared.node_id), nullptr);

  seed_lane(&cleared.project, cleared.node_id, {tempo_point(Rational(0), 60)});
  const std::vector<TempoPoint> foreign =
      tempo_points(cleared.project, cleared.node_id);

  EXPECT_EQ(remove->undo(cleared.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(cleared.project, cleared.node_id), foreign);
}

TEST(CommandTest, TempoPointEditsRespectPickdownCoverage) {
  auto          fx       = make_tempo_setup(1);
  NodeTimeline* timeline = fx.project.find_node(fx.node_id)->timeline();
  ASSERT_NE(timeline, nullptr);
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  // node_end() is 5/4 with the pickdown; a point exactly there is outside
  // the lane's coverage and must be rejected.
  auto at_end = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(*Rational::create(5, 4), 90));
  EXPECT_EQ(at_end->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // A point inside the pickdown is fine.
  auto in_pickdown = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(*Rational::create(9, 8), 90));
  ASSERT_TRUE(in_pickdown->execute(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);

  // ...and now clearing the pickdown would leave that point uncovered, so
  // the region change is rejected instead.
  EXPECT_FALSE(
      fx.project.find_node(fx.node_id)->timeline()->clear_pickdown().ok());

  // Undoing the tempo edit releases the constraint again.
  ASSERT_TRUE(in_pickdown->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
  EXPECT_TRUE(
      fx.project.find_node(fx.node_id)->timeline()->clear_pickdown().ok());

  // With the pickdown gone node_end() is back to 1, so redo revalidates its
  // post-edit lane against the shorter node and is rejected; the lane is
  // left exactly as it was.
  EXPECT_EQ(in_pickdown->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // The rejection left the command undone: restoring the pickdown makes the
  // point at 9/8 fit again and the retried redo succeeds.
  ASSERT_TRUE(fx.project.find_node(fx.node_id)
                  ->timeline()
                  ->set_pickdown(*Rational::create(1, 4))
                  .ok());
  ASSERT_TRUE(in_pickdown->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);
}
