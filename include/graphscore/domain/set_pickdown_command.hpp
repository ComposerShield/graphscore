// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/tempo_lane.hpp>

namespace graphscore {

class SetPickdownCommand : public Command {
 public:
  SetPickdownCommand(NodeId node_id, Rational duration)
      : node_id_(node_id), duration_(duration) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                   node_id_;
  Rational                 duration_;
  std::optional<Rational>  pre_snapshot_;
  std::optional<Rational>  post_snapshot_;
  std::optional<TempoLane> pre_tempo_snapshot_;
  std::optional<TempoLane> post_tempo_snapshot_;
  State                    state_ = State::kFresh;
};

}  // namespace graphscore
