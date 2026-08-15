// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Atomically creates, changes, or removes one single-level tuplet group.
// Create targets an exact, ordered, contiguous list of top-level events.
// Change/remove address the stable group identity and always operate on the
// complete group, so a partial target cannot be represented.
class TupletCommand : public Command {
 public:
  [[nodiscard]] static TupletCommand create_group(
      NodeId node_id, TrackId track_id, StaveId stave_id, Voice voice,
      std::vector<NotationEntityId> events, TupletRatio ratio);

  [[nodiscard]] static TupletCommand change_group(NodeId  node_id,
                                                  TrackId track_id,
                                                  StaveId stave_id, Voice voice,
                                                  TupletGroupId group_id,
                                                  TupletRatio   ratio);

  [[nodiscard]] static TupletCommand remove_group(NodeId  node_id,
                                                  TrackId track_id,
                                                  StaveId stave_id, Voice voice,
                                                  TupletGroupId group_id);

  [[nodiscard]] TupletGroupId group_id() const noexcept { return group_id_; }

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  enum class Kind { kCreate, kChange, kRemove };

  TupletCommand(NodeId node_id, TrackId track_id, StaveId stave_id, Voice voice,
                Kind kind, TupletGroupId group_id,
                std::vector<NotationEntityId> events,
                std::optional<TupletRatio>    ratio)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice),
        kind_(kind),
        group_id_(group_id),
        events_(std::move(events)),
        ratio_(ratio) {}

  NodeId                        node_id_;
  TrackId                       track_id_;
  StaveId                       stave_id_;
  Voice                         voice_;
  Kind                          kind_;
  TupletGroupId                 group_id_;
  std::vector<NotationEntityId> events_;
  std::optional<TupletRatio>    ratio_;
  std::optional<VoiceContent>   pre_snapshot_;
  std::optional<VoiceContent>   post_snapshot_;
  State                         state_ = State::kFresh;
};

}  // namespace graphscore
