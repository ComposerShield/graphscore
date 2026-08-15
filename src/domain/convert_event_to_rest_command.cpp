// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/convert_event_to_rest_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include "command_snapshot_compare.hpp"
#include "event_replacement_helpers.hpp"

namespace graphscore {

namespace {

VoiceContent* resolve_voice(Project& project, const NodeId node_id,
                            const TrackId track_id, const StaveId stave_id,
                            const Voice voice) {
  Node* node = project.find_node(node_id);
  if (node == nullptr)
    return nullptr;

  TrackLane* lane = node->lane(track_id);
  if (lane == nullptr)
    return nullptr;

  StaveVoices* stave = lane->stave(stave_id);
  if (stave == nullptr)
    return nullptr;

  return &stave->voice(voice);
}

}  // namespace

Result ConvertEventToRestCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  VoiceContent* voice =
      resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  Node*               node     = project.find_node(node_id_);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end = timeline->node_end();

  try {
    const auto idx = voice->find_event_index_at(position_);
    if (!idx.has_value() || *idx >= voice->events().size())
      return Result(ResultCode::kInvalidArgument);

    const VoiceEvent& existing   = voice->events()[*idx];
    const bool        converting = !std::holds_alternative<Rest>(existing);
    const Duration&   dur        = event_duration(existing);

    VoiceEvent replacement;
    if (!converting) {
      replacement = existing;
    } else {
      const NotationEntityId source_id = event_id(existing);
      replacement =
          VoiceEvent(Rest{source_id, dur, event_tuplet_group(existing)});
    }

    VoiceContent pre_snapshot = *voice;
    VoiceContent candidate    = pre_snapshot;

    Result result;
    // Converting a tied-into, slurred, or grace-principal event to a Rest
    // normalizes those references (drops the incoming tie, the attached
    // slur, or the grace group) exactly as DeleteNoteheadCommand does,
    // rather than rejecting the conversion outright.
    if (converting) {
      result =
          internal::clear_incoming_ties(candidate, *idx, replacement, node_end);
      if (!result.ok())
        return result;
    }

    result = candidate.replace_event(position_, replacement, node_end);
    if (!result.ok())
      return result;

    if (converting) {
      result = internal::normalize_references_for_replaced_event(
          candidate, event_id(replacement));
      if (!result.ok())
        return result;
    }

    result = candidate.validate();
    if (!result.ok())
      return result;

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(candidate);
    if (!diags.empty())
      return Result(ResultCode::kInvalidArgument);

    replacement_   = replacement;
    pre_snapshot_  = std::move(pre_snapshot);
    post_snapshot_ = candidate;
    *voice         = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kDone;
  return Result();
}

Result ConvertEventToRestCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  VoiceContent* voice =
      resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (post_snapshot_.has_value() &&
      !internal::snapshot_matches(*voice, *post_snapshot_))
    return Result(ResultCode::kInvalidArgument);

  Node*               node     = project.find_node(node_id_);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end = timeline->node_end();

  try {
    VoiceContent candidate = *pre_snapshot_;

    if (!candidate.check_complete(node_end).ok())
      return Result(ResultCode::kInvalidArgument);
    if (!candidate.validate().ok())
      return Result(ResultCode::kInvalidArgument);

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(candidate);
    if (!diags.empty())
      return Result(ResultCode::kInvalidArgument);

    *voice = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kUndone;
  return Result();
}

Result ConvertEventToRestCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  VoiceContent* voice =
      resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (pre_snapshot_.has_value() &&
      !internal::snapshot_matches(*voice, *pre_snapshot_))
    return Result(ResultCode::kInvalidArgument);

  Node*               node     = project.find_node(node_id_);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end = timeline->node_end();

  try {
    VoiceContent candidate = *post_snapshot_;

    if (!candidate.check_complete(node_end).ok())
      return Result(ResultCode::kInvalidArgument);
    if (!candidate.validate().ok())
      return Result(ResultCode::kInvalidArgument);

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(candidate);
    if (!diags.empty())
      return Result(ResultCode::kInvalidArgument);

    *voice = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
