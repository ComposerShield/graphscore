// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the tier-3 weighted-random selection weight of an output connector
// identified by its node and stable ConnectorId (see
// OutputConnector::set_weight). Snapshots the old weight on first
// successful execution.
//
// Missing NodeId/ConnectorId returns kInvalidArgument without mutation.
// Propagates OutputConnector::set_weight's failure Result (a negative
// weight) without changing state.
class SetOutputWeightCommand : public Command {
 public:
  SetOutputWeightCommand(NodeId node_id, ConnectorId output_id,
                         Rational new_weight)
      : node_id_(node_id), output_id_(output_id), new_weight_(new_weight) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId      node_id_;
  ConnectorId output_id_;
  Rational    new_weight_;
  Rational    old_weight_ = Rational(1);
  State       state_      = State::kFresh;
};

}  // namespace graphscore
