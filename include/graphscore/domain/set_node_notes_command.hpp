// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the freeform notes of a node identified by its stable NodeId.
// Snapshots the old notes on first successful execution.
//
// Missing NodeId returns kInvalidArgument without mutation.
class SetNodeNotesCommand : public Command {
 public:
  SetNodeNotesCommand(NodeId node_id, std::string new_notes)
      : node_id_(node_id), new_notes_(std::move(new_notes)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId      node_id_;
  std::string new_notes_;
  std::string old_notes_;
  State       state_ = State::kFresh;
};

}  // namespace graphscore
