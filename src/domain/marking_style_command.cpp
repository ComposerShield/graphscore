// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/marking_style_command.hpp>

#include "command_snapshot_compare.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/domain/node.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {
namespace {

VoiceContent* resolve_voice(Project& project, NodeId node_id, TrackId track_id,
                            StaveId stave_id, Voice voice_number) {
  Node*        node  = project.find_node(node_id);
  TrackLane*   lane  = node == nullptr ? nullptr : node->lane(track_id);
  StaveVoices* stave = lane == nullptr ? nullptr : lane->stave(stave_id);
  return stave == nullptr ? nullptr : &stave->voice(voice_number);
}

[[nodiscard]] bool valid_dynamic(Dynamic value) {
  return std::find(kAllDynamics.begin(), kAllDynamics.end(), value) !=
         kAllDynamics.end();
}

[[nodiscard]] bool valid_direction(HairpinDirection direction) {
  return direction == HairpinDirection::kCrescendo ||
         direction == HairpinDirection::kDiminuendo;
}

Result change_dynamic(VoiceContent& candidate, NotationEntityId marking,
                      Dynamic value) {
  if (!valid_dynamic(value))
    return Result(ResultCode::kInvalidArgument);
  const auto found = std::ranges::find_if(
      candidate.dynamics(),
      [marking](const DynamicMarking& record) { return record.id == marking; });
  if (found == candidate.dynamics().end() || found->value == value)
    return Result(ResultCode::kInvalidArgument);
  DynamicMarking replacement = *found;
  replacement.value          = value;
  return candidate.replace_dynamic(marking, replacement);
}

Result change_hairpin(VoiceContent& candidate, NotationEntityId marking,
                      HairpinDirection direction) {
  if (!valid_direction(direction))
    return Result(ResultCode::kInvalidArgument);
  const auto found = std::ranges::find_if(
      candidate.hairpins(),
      [marking](const Hairpin& record) { return record.id == marking; });
  if (found == candidate.hairpins().end() || found->direction == direction)
    return Result(ResultCode::kInvalidArgument);
  Hairpin replacement   = *found;
  replacement.direction = direction;
  return candidate.replace_hairpin(marking, replacement);
}

}  // namespace

MarkingStyleCommand::MarkingStyleCommand(NodeId node, TrackId track,
                                         StaveId stave, Voice voice,
                                         NotationEntityId marking,
                                         Dynamic          value)
    : node_(node),
      track_(track),
      stave_(stave),
      voice_(voice),
      marking_(marking),
      kind_(Kind::kDynamic),
      value_(value) {}

MarkingStyleCommand::MarkingStyleCommand(NodeId node, TrackId track,
                                         StaveId stave, Voice voice,
                                         NotationEntityId marking,
                                         HairpinDirection direction)
    : node_(node),
      track_(track),
      stave_(stave),
      voice_(voice),
      marking_(marking),
      kind_(Kind::kHairpin),
      direction_(direction) {}

Result MarkingStyleCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr)
    return Result(ResultCode::kInvalidArgument);
  try {
    VoiceContent candidate = *target;
    const Result changed =
        kind_ == Kind::kDynamic
            ? change_dynamic(candidate, marking_, value_)
            : change_hairpin(candidate, marking_, direction_);
    if (!changed.ok())
      return changed;
    if (!validate_voice_references(candidate).empty())
      return Result(ResultCode::kInvalidArgument);
    pre_snapshot_  = *target;
    post_snapshot_ = candidate;
    *target        = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kDone;
  return Result();
}

Result MarkingStyleCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr ||
      !internal::snapshot_matches(*target, *post_snapshot_))
    return Result(ResultCode::kInvalidArgument);
  try {
    VoiceContent candidate = *pre_snapshot_;
    VoiceContent displaced = std::move(*target);
    *target                = std::move(candidate);
    compensation_snapshot_ = std::move(displaced);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kUndone;
  return Result();
}

Result MarkingStyleCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr || !internal::snapshot_matches(*target, *pre_snapshot_))
    return Result(ResultCode::kInvalidArgument);
  try {
    VoiceContent candidate = *post_snapshot_;
    VoiceContent displaced = std::move(*target);
    *target                = std::move(candidate);
    compensation_snapshot_ = std::move(displaced);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kDone;
  return Result();
}

Result MarkingStyleCommand::compensate_undo(Project& project) noexcept {
  if (state_ != State::kUndone || !compensation_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr)
    return Result(ResultCode::kInvalidArgument);
  *target = std::move(*compensation_snapshot_);
  compensation_snapshot_.reset();
  state_ = State::kDone;
  return Result();
}

Result MarkingStyleCommand::compensate_redo(Project& project) noexcept {
  if (state_ != State::kDone || !compensation_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr)
    return Result(ResultCode::kInvalidArgument);
  *target = std::move(*compensation_snapshot_);
  compensation_snapshot_.reset();
  state_ = State::kUndone;
  return Result();
}

}  // namespace graphscore
