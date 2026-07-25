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

// Moves the TempoPoint at exact position `from` to exact position `to` in a
// node's node-wide tempo lane, carrying its tempo and segment kind
// unchanged and re-sorting the lane. Moving a point across another one is
// a normal edit: only the resulting order matters, not the direction.
//
// `from == to` is accepted as a no-op edit so that a drag that ends where
// it started is harmless rather than an error the caller has to
// special-case. The resulting point vector is identical to the input —
// nothing about the lane's content changes, though set_tempo does rebuild
// the TempoLane object — and the command still round-trips through
// undo/redo like any other successful edit. A caller driving this command
// through CommandHistory should expect a no-op move to occupy an undo-stack
// slot all the same: undoing it is visibly a no-op.
//
// Snapshot: the lane's entire point vector before and after the edit, as
// std::optional<std::vector<TempoPoint>>, where an empty optional means
// "the node had no tempo lane" (a state this command can only observe, not
// produce).
//
// Fails with kInvalidArgument when:
// - the node id does not resolve, or the node has no NodeTimeline;
// - the node has no tempo lane, or the lane has no point at exactly
//   `from`;
// - `to` collides with a DIFFERENT existing point's position (the lane's
//   ordering invariant is strict);
// - the moved lane fails NodeTimeline::set_tempo — notably moving the
//   point at 0/1 away from 0/1 while others remain, or moving a point to
//   or past node_end();
// - the command is not in the phase the call requires (double execute,
//   undo before execute, redo before undo);
// - undo/redo finds the node's current lane state different from the one
//   this command left behind (stale context).
//
// On every failure path the project and the command's own state are
// unchanged.
class MoveTempoPointCommand : public Command {
 public:
  MoveTempoPointCommand(NodeId node_id, Rational from, Rational to)
      : node_id_(node_id), from_(from), to_(to) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId   node_id_;
  Rational from_;
  Rational to_;

  // Saved pre-edit lane state for undo restoration.
  std::optional<std::vector<TempoPoint>> pre_snapshot_;
  // Saved post-edit lane state, verified before undo to reject stale
  // context.
  std::optional<std::vector<TempoPoint>> post_snapshot_;
  State                                  state_ = State::kFresh;
};

}  // namespace graphscore
