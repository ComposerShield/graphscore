// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/tempo_point.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Removes the TempoPoint at an exact Rational position from a node's
// node-wide tempo lane. Removing the lane's only point removes the lane
// itself: an empty point vector is not a valid TempoLane, so "no lane" is
// the correct result state.
//
// Snapshot: the lane's entire point vector before and after the edit, as
// std::optional<std::vector<TempoPoint>>, where an empty optional means
// "the node had no tempo lane". That is what makes removing the LAST tempo
// point exactly reversible — undo recreates the lane with its original
// points, tempi, and segment kinds.
//
// Fails with kInvalidArgument when:
// - the node id does not resolve, or the node has no NodeTimeline;
// - the node has no tempo lane, or the lane has no point at exactly
//   `position`;
// - the remaining points fail NodeTimeline::set_tempo — notably removing
//   the point at 0/1 while later points remain, since a lane must begin
//   exactly at its start;
// - the command is not in the phase the call requires (double execute,
//   undo before execute, redo before undo);
// - undo/redo finds the node's current lane state different from the one
//   this command left behind (stale context).
//
// On every failure path the project and the command's own state are
// unchanged.
class RemoveTempoPointCommand : public Command {
 public:
  RemoveTempoPointCommand(NodeId node_id, Rational position)
      : node_id_(node_id), position_(position) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId   node_id_;
  Rational position_;

  // Saved pre-edit lane state for undo restoration.
  std::optional<std::vector<TempoPoint>> pre_snapshot_;
  // Saved post-edit lane state, verified before undo to reject stale
  // context.
  std::optional<std::vector<TempoPoint>> post_snapshot_;
  State                                  state_ = State::kFresh;
};

}  // namespace graphscore
