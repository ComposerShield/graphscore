// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/tempo.hpp>
#include <graphscore/core/tempo_point.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Replaces the tempo and segment kind of the TempoPoint at an exact
// Rational position in a node's node-wide tempo lane, leaving that point's
// position and every other point in the lane untouched. This is the "edit
// the value at an anchor" companion to MoveTempoPointCommand's "edit where
// an anchor sits".
//
// Snapshot: the lane's entire point vector before and after the edit, as
// std::optional<std::vector<TempoPoint>>, where an empty optional means
// "the node had no tempo lane" (a state this command can only observe, not
// produce).
//
// Fails with kInvalidArgument when:
// - the node id does not resolve, or the node has no NodeTimeline;
// - the node has no tempo lane, or the lane has no point at exactly
//   `position`;
// - the rewritten lane fails NodeTimeline::set_tempo (positions are
//   untouched, so this only propagates a lane that was already invalid
//   against the current node_end());
// - the command is not in the phase the call requires (double execute,
//   undo before execute, redo before undo);
// - undo/redo finds the node's current lane state different from the one
//   this command left behind (stale context).
//
// On every failure path the project and the command's own state are
// unchanged.
class SetTempoPointCommand : public Command {
 public:
  SetTempoPointCommand(NodeId node_id, Rational position, Tempo tempo,
                       TempoSegmentKind segment_kind)
      : node_id_(node_id),
        position_(position),
        tempo_(tempo),
        segment_kind_(segment_kind) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId           node_id_;
  Rational         position_;
  Tempo            tempo_;
  TempoSegmentKind segment_kind_;

  // Saved pre-edit lane state for undo restoration.
  std::optional<std::vector<TempoPoint>> pre_snapshot_;
  // Saved post-edit lane state, verified before undo to reject stale
  // context.
  std::optional<std::vector<TempoPoint>> post_snapshot_;
  State                                  state_ = State::kFresh;
};

}  // namespace graphscore
