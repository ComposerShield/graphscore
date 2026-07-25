// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

class RemoveHairpinCommand : public Command {
 public:
  RemoveHairpinCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                       Voice voice, NotationEntityId marking_id)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        marking_id_(marking_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId           node_id_;
  TrackId          track_id_;
  StaveId          stave_id_;
  Voice            voice_;
  NotationEntityId marking_id_;

  std::optional<VoiceContent> pre_snapshot_;
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
