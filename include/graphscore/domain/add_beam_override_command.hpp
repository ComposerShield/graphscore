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

class AddBeamOverrideCommand : public Command {
 public:
  AddBeamOverrideCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                         Voice voice, BeamOverride beam_override)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        beam_override_(std::move(beam_override)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId       node_id_;
  TrackId      track_id_;
  StaveId      stave_id_;
  Voice        voice_;
  BeamOverride beam_override_;

  std::optional<VoiceContent> pre_snapshot_;
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
