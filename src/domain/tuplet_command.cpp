// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/tuplet_command.hpp>

#include <cstddef>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/domain/node.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include "command_snapshot_compare.hpp"

namespace graphscore {
namespace {

struct VoiceLocation {
  VoiceContent*       content;
  const NodeTimeline* timeline;
};

std::optional<VoiceLocation> resolve(Project& project, NodeId node_id,
                                     TrackId track_id, StaveId stave_id,
                                     Voice voice) {
  Node* node = project.find_node(node_id);
  if (node == nullptr || node->timeline() == nullptr)
    return std::nullopt;
  TrackLane* lane = node->lane(track_id);
  if (lane == nullptr)
    return std::nullopt;
  StaveVoices* stave = lane->stave(stave_id);
  if (stave == nullptr)
    return std::nullopt;
  return VoiceLocation{&stave->voice(voice), node->timeline()};
}

std::optional<std::pair<std::size_t, std::size_t>> locate_create_target(
    const VoiceContent& voice, const std::vector<NotationEntityId>& ids) {
  if (ids.empty())
    return std::nullopt;
  std::optional<std::size_t> first;
  for (std::size_t index = 0; index < voice.events().size(); ++index) {
    if (event_id(voice.events()[index]) == ids.front()) {
      first = index;
      break;
    }
  }
  if (!first.has_value() || ids.size() > voice.events().size() - *first)
    return std::nullopt;
  for (std::size_t offset = 0; offset < ids.size(); ++offset) {
    const VoiceEvent& event = voice.events()[*first + offset];
    if (event_id(event) != ids[offset] ||
        event_tuplet_group(event).has_value() ||
        event_duration(event).tuplet().has_value()) {
      return std::nullopt;
    }
  }
  return std::pair{*first, ids.size()};
}

std::optional<std::pair<std::size_t, std::size_t>> locate_group(
    const VoiceContent& voice, TupletGroupId group_id) {
  std::optional<std::size_t> first;
  std::size_t                count = 0;
  bool                       ended = false;
  for (std::size_t index = 0; index < voice.events().size(); ++index) {
    if (event_tuplet_group(voice.events()[index]) == group_id) {
      if (ended)
        return std::nullopt;
      if (!first.has_value())
        first = index;
      ++count;
    } else if (first.has_value()) {
      ended = true;
    }
  }
  if (!first.has_value())
    return std::nullopt;
  return std::pair{*first, count};
}

Result validate_candidate(const VoiceContent& voice, Rational node_end) {
  if (!voice.check_complete(node_end).ok() || !voice.validate().ok() ||
      !validate_voice_references(voice).empty()) {
    return Result(ResultCode::kInvalidArgument);
  }
  return Result();
}

}  // namespace

TupletCommand TupletCommand::create_group(NodeId node_id, TrackId track_id,
                                          StaveId stave_id, Voice voice,
                                          std::vector<NotationEntityId> events,
                                          TupletRatio                   ratio) {
  return TupletCommand(node_id, track_id, stave_id, voice, Kind::kCreate,
                       TupletGroupId::generate(), std::move(events), ratio);
}

TupletCommand TupletCommand::change_group(NodeId node_id, TrackId track_id,
                                          StaveId stave_id, Voice voice,
                                          TupletGroupId group_id,
                                          TupletRatio   ratio) {
  return TupletCommand(node_id, track_id, stave_id, voice, Kind::kChange,
                       group_id, {}, ratio);
}

TupletCommand TupletCommand::remove_group(NodeId node_id, TrackId track_id,
                                          StaveId stave_id, Voice voice,
                                          TupletGroupId group_id) {
  return TupletCommand(node_id, track_id, stave_id, voice, Kind::kRemove,
                       group_id, {}, std::nullopt);
}

Result TupletCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh ||
      (ratio_.has_value() && ratio_->played() == ratio_->normal())) {
    return Result(ResultCode::kInvalidArgument);
  }
  const std::optional<VoiceLocation> location =
      resolve(project, node_id_, track_id_, stave_id_, voice_);
  if (!location.has_value())
    return Result(ResultCode::kInvalidArgument);

  try {
    VoiceContent candidate = *location->content;
    const auto   bounds    = kind_ == Kind::kCreate
                                 ? locate_create_target(candidate, events_)
                                 : locate_group(candidate, group_id_);
    if (!bounds.has_value())
      return Result(ResultCode::kInvalidArgument);
    const std::optional<TupletGroupId> replacement_group =
        kind_ == Kind::kRemove ? std::nullopt
                               : std::optional<TupletGroupId>(group_id_);
    const Result result = candidate.set_tuplet_group(
        bounds->first, bounds->second, replacement_group, ratio_,
        location->timeline->node_end());
    if (!result.ok())
      return result;
    const Result valid =
        validate_candidate(candidate, location->timeline->node_end());
    if (!valid.ok())
      return valid;

    pre_snapshot_      = *location->content;
    post_snapshot_     = candidate;
    *location->content = std::move(candidate);
    state_             = State::kDone;
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result TupletCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value()) {
    return Result(ResultCode::kInvalidArgument);
  }
  const std::optional<VoiceLocation> location =
      resolve(project, node_id_, track_id_, stave_id_, voice_);
  if (!location.has_value() ||
      !internal::snapshot_matches(*location->content, *post_snapshot_)) {
    return Result(ResultCode::kInvalidArgument);
  }
  try {
    VoiceContent candidate = *pre_snapshot_;
    const Result valid =
        validate_candidate(candidate, location->timeline->node_end());
    if (!valid.ok())
      return valid;
    *location->content = std::move(candidate);
    state_             = State::kUndone;
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result TupletCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value()) {
    return Result(ResultCode::kInvalidArgument);
  }
  const std::optional<VoiceLocation> location =
      resolve(project, node_id_, track_id_, stave_id_, voice_);
  if (!location.has_value() ||
      !internal::snapshot_matches(*location->content, *pre_snapshot_)) {
    return Result(ResultCode::kInvalidArgument);
  }
  try {
    VoiceContent candidate = *post_snapshot_;
    const Result valid =
        validate_candidate(candidate, location->timeline->node_end());
    if (!valid.ok())
      return valid;
    *location->content = std::move(candidate);
    state_             = State::kDone;
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

}  // namespace graphscore
