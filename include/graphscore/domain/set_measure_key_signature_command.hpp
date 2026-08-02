// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>

#include <graphscore/core/key_signature.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/measure_map.hpp>

namespace graphscore {

class SetMeasureKeySignatureCommand : public Command {
 public:
  SetMeasureKeySignatureCommand(NodeId node_id, std::size_t measure_index,
                                KeySignature key_signature)
      : node_id_(node_id),
        measure_index_(measure_index),
        key_signature_(key_signature) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId       node_id_;
  std::size_t  measure_index_;
  KeySignature key_signature_;
  Measure      pre_snapshot_;
  Measure      post_snapshot_;
  State        state_ = State::kFresh;
};

}  // namespace graphscore
