// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/tempo_lane.hpp>

namespace graphscore {

class ClearPickdownCommand : public Command {
 public:
  explicit ClearPickdownCommand(NodeId node_id) : node_id_(node_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                   node_id_;
  std::optional<Rational>  pre_snapshot_;
  std::optional<Rational>  post_snapshot_;
  std::optional<TempoLane> pre_tempo_snapshot_;
  std::optional<TempoLane> post_tempo_snapshot_;
  State                    state_ = State::kFresh;
};

}  // namespace graphscore
