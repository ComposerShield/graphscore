// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the display name of a node identified by its stable NodeId.
// Snapshots the old name on first successful execution.
class SetNodeNameCommand : public Command {
 public:
  SetNodeNameCommand(NodeId node_id, std::string new_name)
      : node_id_(node_id), new_name_(std::move(new_name)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId      node_id_;
  std::string new_name_;
  std::string old_name_;
  State       state_ = State::kFresh;
};

}  // namespace graphscore
