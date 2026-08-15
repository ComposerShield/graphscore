// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/articulation.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/articulation.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

enum class ArticulationEdit { kApply, kChange, kRemove };

// Atomically edits one Note/Chord's value-owned articulations or stem override.
// The top-level event identity is re-resolved at execution time; snapshots make
// undo/redo reject an independently changed voice rather than overwrite it.
class EventStyleCommand final : public Command {
 public:
  EventStyleCommand(NodeId node, TrackId track, StaveId stave, Voice voice,
                    NotationEntityId event, ArticulationEdit edit,
                    Articulation                articulation,
                    std::optional<Articulation> replaced = std::nullopt);
  EventStyleCommand(NodeId node, TrackId track, StaveId stave, Voice voice,
                    NotationEntityId event, StemDirection stem);

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;
  Result compensate_undo(Project& project) noexcept override;
  Result compensate_redo(Project& project) noexcept override;

 private:
  enum class Kind { kArticulation, kStem };

  NodeId                      node_;
  TrackId                     track_;
  StaveId                     stave_;
  Voice                       voice_;
  NotationEntityId            event_;
  Kind                        kind_;
  ArticulationEdit            articulation_edit_ = ArticulationEdit::kApply;
  Articulation                articulation_      = Articulation::kAccent;
  std::optional<Articulation> replaced_;
  StemDirection               stem_ = StemDirection::kAuto;
  std::optional<VoiceContent> pre_snapshot_;
  std::optional<VoiceContent> post_snapshot_;
  std::optional<VoiceContent> compensation_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
