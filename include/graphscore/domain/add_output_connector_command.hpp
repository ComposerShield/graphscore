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

// Adds a new output connector, with the given name and type, to a node
// identified by its stable NodeId (see Node::add_output). Fails if
// `node_id` is unknown.
//
// Reversibility: execute mints the connector's id and snapshots the created
// OutputConnector value on first success. Undo removes it by id via
// Node::remove_output. Redo re-inserts the exact snapshotted value via
// Node::restore_output, preserving the same ConnectorId rather than minting
// a fresh one; a freshly added output has no event binding, so redo never
// needs to restore a listener.
class AddOutputConnectorCommand : public Command {
 public:
  AddOutputConnectorCommand(NodeId node_id, std::string name,
                            ConnectorType type)
      : node_id_(node_id), name_(std::move(name)), type_(type) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                         node_id_;
  std::string                    name_;
  ConnectorType                  type_;
  std::optional<OutputConnector> created_;
  State                          state_ = State::kFresh;
};

}  // namespace graphscore
