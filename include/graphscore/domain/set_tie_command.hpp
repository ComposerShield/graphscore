// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <utility>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Sets or clears the tied_to_next flag on a Note or on an individual
// Chord notehead at an exact Rational position in one voice.
//
// - For Note events, `notehead_index` must be absent; supplying an index is
//   rejected (a Note carries a single tie flag).
// - For Chord events, `notehead_index` is required and selects the 0-based
//   notehead whose tied_to_next is set (or cleared).  Duplicate pitches are
//   unambiguous because the index is explicit.  A missing or out-of-range
//   index is rejected.
// - If the event is a Rest the command fails (a rest carries no tie flag).
//
// Since only a flag changes the voice's rhythmic structure is unchanged;
// normalization against the node's timeline is still applied as a safety
// measure. This is one command covering both tie and untie per the plan's
// instruction that tie/untie may be a single setter if both operations
// are explicit in its API.
class SetTieCommand : public Command {
 public:
  SetTieCommand(NodeId node_id, TrackId track_id, StaveId stave_id, Voice voice,
                Rational position, std::optional<std::size_t> notehead_index,
                bool tied)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        position_(position),
        notehead_index_(notehead_index),
        tied_(tied) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                     node_id_;
  TrackId                    track_id_;
  StaveId                    stave_id_;
  Voice                      voice_;
  Rational                   position_;
  std::optional<std::size_t> notehead_index_;
  bool                       tied_;

  // Saved pre-edit state for undo restoration.
  std::optional<VoiceContent> pre_snapshot_;
  // Saved post-edit state, verified before undo to reject stale context.
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
