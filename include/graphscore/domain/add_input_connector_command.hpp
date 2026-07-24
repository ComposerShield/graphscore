// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/connector.hpp>

namespace graphscore {

// Adds a new input connector, with the given name, to a node identified by
// its stable NodeId (see Node::add_input). Fails if `node_id` is unknown.
//
// Reversibility: execute mints the connector's id and snapshots the created
// InputConnector value on first success. Undo removes it by id through
// Graph::remove_input -- the cross-node-safe entry point, though a freshly
// added input has no inbound edges for the removal cascade to clear. Redo
// re-inserts the exact snapshotted value via Node::restore_input,
// preserving the same ConnectorId rather than minting a fresh one.
class AddInputConnectorCommand : public Command {
 public:
  AddInputConnectorCommand(NodeId node_id, std::string name)
      : node_id_(node_id), name_(std::move(name)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                        node_id_;
  std::string                   name_;
  std::optional<InputConnector> created_;
  State                         state_ = State::kFresh;
};

}  // namespace graphscore
