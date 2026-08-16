// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

class AddDynamicCommand : public Command {
 public:
  AddDynamicCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                    Voice voice, DynamicMarking marking)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        marking_(marking) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;
  // Publication-atomic compensation for a successful undo/redo whose
  // separate publication step failed: restores the exact content that
  // undo/redo displaced instead of re-running the inverse operation.
  Result compensate_undo(Project& project) noexcept override;
  Result compensate_redo(Project& project) noexcept override;

 private:
  NodeId         node_id_;
  TrackId        track_id_;
  StaveId        stave_id_;
  Voice          voice_;
  DynamicMarking marking_;

  std::optional<VoiceContent> pre_snapshot_;
  std::optional<VoiceContent> post_snapshot_;
  std::optional<VoiceContent> compensation_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
