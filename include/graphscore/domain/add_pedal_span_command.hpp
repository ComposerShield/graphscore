// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

class AddPedalSpanCommand : public Command {
 public:
  AddPedalSpanCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                      PedalSpan span)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        span_(span) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;
  // Publication-atomic compensation for a successful undo/redo whose
  // separate publication step failed: restores the exact content that
  // undo/redo displaced instead of re-running the inverse operation.
  Result compensate_undo(Project& project) noexcept override;
  Result compensate_redo(Project& project) noexcept override;

 private:
  NodeId    node_id_;
  TrackId   track_id_;
  StaveId   stave_id_;
  PedalSpan span_;

  std::optional<TrackLane> pre_snapshot_;
  std::optional<TrackLane> post_snapshot_;
  std::optional<TrackLane> compensation_snapshot_;
  State                    state_ = State::kFresh;
};

}  // namespace graphscore
