// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets or clears the project's designated start node. Constructed with
// std::optional<NodeId>: a value calls Project::set_start_node, nullopt
// calls clear_start_node. Snapshots the old optional on first successful
// execution.
//
// Invalid/unowned NodeId returns kInvalidArgument without mutation.
// Undo/redo may legitimately fail after external structural mutation and
// preserve command state for retry.
class SetStartNodeCommand : public Command {
 public:
  explicit SetStartNodeCommand(std::optional<NodeId> new_start_node)
      : new_start_node_(new_start_node) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  std::optional<NodeId> new_start_node_;
  std::optional<NodeId> old_start_node_;
  State                 state_ = State::kFresh;
};

}  // namespace graphscore
