// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>

#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/time_signature.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>

namespace graphscore {

// Replaces one measure's meter. Equal-length replacements update only the
// signature; an expansion inserts rest time at the measure's old end and a
// contraction deletes its new tail, using the same atomic cascade as measure
// insertion/deletion.
class SetMeasureTimeSignatureCommand : public Command {
 public:
  SetMeasureTimeSignatureCommand(NodeId node_id, std::size_t measure_index,
                                 TimeSignature time_signature)
      : node_id_(node_id),
        measure_index_(measure_index),
        time_signature_(time_signature) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId              node_id_;
  std::size_t         measure_index_;
  TimeSignature       time_signature_;
  std::optional<Node> pre_snapshot_;
  std::optional<Node> post_snapshot_;
  State               state_ = State::kFresh;
};

}  // namespace graphscore
