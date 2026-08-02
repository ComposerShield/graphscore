// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>

#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>

namespace graphscore {

// Deletes one complete measure, removes attacks and absolute lane content
// inside it, and shifts all later music left. Boundary-crossing attacks are
// clipped rather than recreated after the deleted region. Pedal spans are
// compressed/clipped with their existing ids; wholly deleted or zero-length
// spans are removed. The exact pickdown duration is retained or the complete
// edit is rejected.
class DeleteMeasureCommand : public Command {
 public:
  DeleteMeasureCommand(NodeId node_id, std::size_t index)
      : node_id_(node_id), index_(index) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId              node_id_;
  std::size_t         index_;
  std::optional<Node> pre_snapshot_;
  std::optional<Node> post_snapshot_;
  State               state_ = State::kFresh;
};

}  // namespace graphscore
