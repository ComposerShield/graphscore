// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Deletes one Note, ChordNote, or GraceNote addressed by persistent identity.
// A complete event deletion leaves a same-duration normalized Rest. Deleting
// one pitch from a chord preserves the remaining pitch; a two-note chord is
// reduced to a normal Note. The complete VoiceContent is snapshotted so the
// operation is reversible without regenerating any surviving identities.
class DeleteNoteheadCommand : public Command {
 public:
  DeleteNoteheadCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                        Voice voice, NotationEntityId notehead_id)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        notehead_id_(notehead_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId           node_id_;
  TrackId          track_id_;
  StaveId          stave_id_;
  Voice            voice_;
  NotationEntityId notehead_id_;

  std::optional<VoiceContent> pre_snapshot_;
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
