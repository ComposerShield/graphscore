// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/rational.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/clef_lane.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

class RemoveClefChangeCommand : public Command {
 public:
  RemoveClefChangeCommand(NodeId node_id, StaveId stave_id, Rational position)
      : node_id_(node_id), stave_id_(stave_id), position_(position) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId   node_id_;
  StaveId  stave_id_;
  Rational position_;
  ClefLane pre_snapshot_{Clef::kTreble};
  ClefLane post_snapshot_{Clef::kTreble};
  State    state_ = State::kFresh;
};

}  // namespace graphscore
