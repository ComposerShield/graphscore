// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the sequential/vertical arbitration priority of an output connector
// identified by its node and stable ConnectorId (see
// OutputConnector::set_priority). Snapshots the old priority on first
// successful execution.
//
// Missing NodeId/ConnectorId returns kInvalidArgument without mutation.
class SetOutputPriorityCommand : public Command {
 public:
  SetOutputPriorityCommand(NodeId node_id, ConnectorId output_id,
                           int new_priority)
      : node_id_(node_id), output_id_(output_id), new_priority_(new_priority) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId      node_id_;
  ConnectorId output_id_;
  int         new_priority_;
  int         old_priority_ = 0;
  State       state_        = State::kFresh;
};

}  // namespace graphscore
