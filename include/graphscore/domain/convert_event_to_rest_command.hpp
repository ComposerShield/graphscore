// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Converts the VoiceEvent at an exact Rational position to a Rest of the
// same duration — the explicit "R" operation the plan requires as a
// first-class command rather than relying on undocumented caller
// convention. The replacement rest preserves the source event's
// NotationEntityId so redo produces the same id every time. Converting an
// existing Rest is a true no-op (same id, same duration). Since the
// duration does not change, a successful conversion leaves the voice
// rhythmically unchanged, but normalisation against the node's timeline is
// still applied as a safety measure.
//
// Converting an event that is tied into by its predecessor, is a slur
// endpoint, or is a grace group's principal event does not fail: the
// incoming tie, the attached slur, and the grace group are normalized away
// first, exactly as DeleteNoteheadCommand normalizes the same references,
// so "R" always succeeds on a valid single-event target rather than
// rejecting on a reference it could instead repair.
class ConvertEventToRestCommand : public Command {
 public:
  ConvertEventToRestCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                            Voice voice, Rational position)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        position_(position) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId   node_id_;
  TrackId  track_id_;
  StaveId  stave_id_;
  Voice    voice_;
  Rational position_;

  // Saved pre-edit state for undo restoration.
  std::optional<VoiceContent> pre_snapshot_;
  // Saved post-edit state, verified before undo to reject stale context.
  std::optional<VoiceContent> post_snapshot_;
  std::optional<VoiceEvent>   replacement_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
