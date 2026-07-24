// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets whether an output connector, identified by its node and stable
// ConnectorId, must have exactly one destination to count toward
// Graph::is_export_ready (see OutputConnector::set_export_enabled).
// Snapshots the old flag on first successful execution.
//
// Missing NodeId/ConnectorId returns kInvalidArgument without mutation.
class SetOutputExportEnabledCommand : public Command {
 public:
  SetOutputExportEnabledCommand(NodeId node_id, ConnectorId output_id,
                                bool new_enabled)
      : node_id_(node_id), output_id_(output_id), new_enabled_(new_enabled) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId      node_id_;
  ConnectorId output_id_;
  bool        new_enabled_;
  bool        old_enabled_ = true;
  State       state_       = State::kFresh;
};

}  // namespace graphscore
