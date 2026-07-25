// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/tempo_point.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Inserts one TempoPoint into a node's node-wide tempo lane at its sorted
// position, creating the lane if the node has none yet.
//
// Snapshot: the lane's entire point vector before and after the edit, as
// std::optional<std::vector<TempoPoint>>, where an empty optional means
// "the node had no tempo lane". That is what makes creating the FIRST
// tempo point exactly reversible — undo removes the lane again rather than
// leaving an empty or default one behind.
//
// Fails with kInvalidArgument when:
// - the node id does not resolve, or the node has no NodeTimeline;
// - `point`'s position duplicates an existing point's position (the lane's
//   ordering invariant is strict);
// - the node has no lane yet and `point.position` is not exactly 0/1
//   (TempoLane::create requires a lane to begin at its start), or the
//   resulting vector otherwise fails NodeTimeline::set_tempo — e.g. a
//   position at or past node_end();
// - the command is not in the phase the call requires (double execute,
//   undo before execute, redo before undo);
// - undo/redo finds the node's current lane state different from the one
//   this command left behind (stale context).
//
// On every failure path the project and the command's own state are
// unchanged.
class AddTempoPointCommand : public Command {
 public:
  AddTempoPointCommand(NodeId node_id, TempoPoint point)
      : node_id_(node_id), point_(point) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId     node_id_;
  TempoPoint point_;

  // Saved pre-edit lane state for undo restoration.
  std::optional<std::vector<TempoPoint>> pre_snapshot_;
  // Saved post-edit lane state, verified before undo to reject stale
  // context.
  std::optional<std::vector<TempoPoint>> post_snapshot_;
  State                                  state_ = State::kFresh;
};

}  // namespace graphscore
