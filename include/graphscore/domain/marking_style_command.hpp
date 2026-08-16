// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/dynamic.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Which half of a marking family's editing surface an operation addresses.
// Apply and remove are already owned by the family's own Add*/Remove*
// commands; MarkingStyleCommand owns kChange, the one operation that has no
// add/remove spelling because it must preserve the marking's identity.
enum class MarkingEdit { kApply, kChange, kRemove };

// Atomically changes one existing voice-scoped marking's style attribute: a
// DynamicMarking's value or a Hairpin's direction. The marking identity and
// its endpoint references are preserved; only the attribute changes, so a
// selection naming the marking stays valid across the edit.
//
// The marking's own position within its VoiceContent collection is preserved
// as well: the change runs through VoiceContent::replace_dynamic /
// replace_hairpin, which replace the record in place and emit one kUpdate
// delta op. Collection order is what the notation index turns into each
// marking's order key, and the engraver allocates lanes to horizontally
// overlapping markings in order-key order, so a remove-then-add spelling
// would silently restack overlapping markings on a style-only edit.
//
// The marking identity is re-resolved at execution time; snapshots make
// undo/redo reject an independently changed voice rather than overwrite it.
class MarkingStyleCommand final : public Command {
 public:
  MarkingStyleCommand(NodeId node, TrackId track, StaveId stave, Voice voice,
                      NotationEntityId marking, Dynamic value);
  MarkingStyleCommand(NodeId node, TrackId track, StaveId stave, Voice voice,
                      NotationEntityId marking, HairpinDirection direction);

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;
  Result compensate_undo(Project& project) noexcept override;
  Result compensate_redo(Project& project) noexcept override;

 private:
  enum class Kind { kDynamic, kHairpin };

  NodeId                      node_;
  TrackId                     track_;
  StaveId                     stave_;
  Voice                       voice_;
  NotationEntityId            marking_;
  Kind                        kind_;
  Dynamic                     value_     = Dynamic::kMf;
  HairpinDirection            direction_ = HairpinDirection::kCrescendo;
  std::optional<VoiceContent> pre_snapshot_;
  std::optional<VoiceContent> post_snapshot_;
  std::optional<VoiceContent> compensation_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
