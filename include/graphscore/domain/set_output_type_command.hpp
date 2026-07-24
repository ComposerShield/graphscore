// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/connector.hpp>

namespace graphscore {

// Changes the ConnectorType of an output connector identified by its node
// and stable ConnectorId (see Node::set_output_type). Snapshots the old
// type on first successful execution.
//
// Missing NodeId/ConnectorId returns kInvalidArgument without mutation.
// Propagates Node::set_output_type's failure Result (a vertical/sequential
// clash with another output bound to the same event) without changing
// state.
class SetOutputTypeCommand : public Command {
 public:
  SetOutputTypeCommand(NodeId node_id, ConnectorId output_id,
                       ConnectorType new_type)
      : node_id_(node_id), output_id_(output_id), new_type_(new_type) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId        node_id_;
  ConnectorId   output_id_;
  ConnectorType new_type_;
  ConnectorType old_type_ = ConnectorType::kSequential;
  State         state_    = State::kFresh;
};

}  // namespace graphscore
