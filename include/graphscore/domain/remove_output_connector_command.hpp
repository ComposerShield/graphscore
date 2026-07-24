// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_listener.hpp>

namespace graphscore {

// Removes an output connector, identified by its stable ConnectorId, from a
// node identified by its stable NodeId (see Node::remove_output). Fails,
// leaving the node unchanged, if `node_id`/`output_id` is unknown.
//
// Reversibility subtlety: removing the last output on a node bound to an
// event destroys that node's EventListener entirely (see
// Node::cleanup_listener_if_unused). This command snapshots the full
// removed OutputConnector value and, if it carried an event binding whose
// listener would be destroyed, a copy of that EventListener, on first
// successful execute. Undo re-inserts the exact output value via
// Node::restore_output, which also recreates the listener from the
// snapshot iff it was destroyed -- leaving a surviving listener (the
// removal was not the sole binding) untouched. Redo removes the output
// again.
class RemoveOutputConnectorCommand : public Command {
 public:
  RemoveOutputConnectorCommand(NodeId node_id, ConnectorId output_id)
      : node_id_(node_id), output_id_(output_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                         node_id_;
  ConnectorId                    output_id_;
  std::optional<OutputConnector> removed_;
  std::optional<EventListener>   saved_listener_;
  State                          state_ = State::kFresh;
};

}  // namespace graphscore
