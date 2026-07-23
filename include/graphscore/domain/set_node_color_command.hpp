// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the custom color (packed 0xRRGGBBAA) of a node identified by its
// stable NodeId. Snapshots the old color on first successful execution.
//
// Missing NodeId returns kInvalidArgument without mutation.
class SetNodeColorCommand : public Command {
 public:
  SetNodeColorCommand(NodeId node_id, std::uint32_t new_color)
      : node_id_(node_id), new_color_(new_color) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId        node_id_;
  std::uint32_t new_color_;
  std::uint32_t old_color_ = 0;
  State         state_     = State::kFresh;
};

}  // namespace graphscore
