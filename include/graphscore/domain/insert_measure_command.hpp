// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>

#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/node.hpp>

namespace graphscore {

// Inserts one rest-filled measure and shifts all music at or after its start
// right by the inserted duration. The explicit constructor uses `measure`
// verbatim; the two-argument constructor inherits the destination measure (or
// the current last measure when appending), matching MeasureMap's overloads.
// Boundary-crossing attacks are clipped and their ties severed. Crossing
// pedal spans expand, while spans starting at the boundary shift right; every
// surviving pedal span retains its NotationEntityId.
class InsertMeasureCommand : public Command {
 public:
  InsertMeasureCommand(NodeId node_id, std::size_t index, Measure measure)
      : node_id_(node_id), index_(index), measure_(measure) {}

  InsertMeasureCommand(NodeId node_id, std::size_t index)
      : node_id_(node_id), index_(index) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                 node_id_;
  std::size_t            index_;
  std::optional<Measure> measure_;
  std::optional<Node>    pre_snapshot_;
  std::optional<Node>    post_snapshot_;
  State                  state_ = State::kFresh;
};

}  // namespace graphscore
