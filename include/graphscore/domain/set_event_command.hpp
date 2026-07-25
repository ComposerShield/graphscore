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

// Replaces the VoiceEvent at an exact Rational position in one voice with
// a new event, then normalizes the voice against the node's timeline.
// Snapshot: the entire VoiceContent before the edit.
//
// Duration expansion: when the replacement event is longer than the event
// it replaces, the command consumes immediately following rests to make
// room without clipping later sounding content.  It consumes whole rests
// greedily and, if needed, shortens the final consumed rest by exactly
// the remaining Rational amount.  Expansion is rejected atomically if
// contiguous following rests do not cover the growth before a sounding
// event or node end.  Duration contraction fills the gap with normalized
// rests (decompose_rest).
//
// Covers every category the plan calls out:
// - note <-> rest <-> chord conversion
// - duration changes (replace with a different-duration event ->
//   re-normalize the voice)
// - chord building (replace with a Chord whose notehead set differs)
class SetEventCommand : public Command {
 public:
  SetEventCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                  Voice voice, Rational position, VoiceEvent new_event)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        position_(position),
        new_event_(std::move(new_event)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId     node_id_;
  TrackId    track_id_;
  StaveId    stave_id_;
  Voice      voice_;
  Rational   position_;
  VoiceEvent new_event_;

  // Saved pre-edit state for undo restoration.
  std::optional<VoiceContent> pre_snapshot_;
  // Saved post-edit state, verified before undo to reject stale context.
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
