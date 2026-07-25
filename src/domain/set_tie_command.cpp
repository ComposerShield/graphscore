// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_tie_command.hpp>

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

std::optional<VoiceEvent> apply_tie(
    const VoiceEvent& event, const std::optional<std::size_t>& notehead_index,
    bool tied) {
  if (const auto* note = std::get_if<Note>(&event)) {
    if (notehead_index.has_value())
      return std::nullopt;
    Note modified         = *note;
    modified.tied_to_next = tied;
    return VoiceEvent(std::move(modified));
  }

  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (!notehead_index.has_value())
      return std::nullopt;
    if (*notehead_index >= chord->notes.size())
      return std::nullopt;
    Chord modified                               = *chord;
    modified.notes[*notehead_index].tied_to_next = tied;
    return VoiceEvent(std::move(modified));
  }

  return std::nullopt;
}

}  // namespace

Result SetTieCommand::execute(Project& project) noexcept {
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

    std::optional<VoiceEvent> modified =
        apply_tie(voice->events()[*idx], notehead_index_, tied_);
    if (!modified.has_value())
      return Result(ResultCode::kInvalidArgument);

    VoiceContent pre_snapshot = *voice;
    VoiceContent candidate    = pre_snapshot;

    Result result = candidate.replace_event(position_, *modified, node_end);
    if (!result.ok())
      return result;

    result = candidate.validate();
    if (!result.ok())
      return result;

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(candidate);
    if (!diags.empty())
      return Result(ResultCode::kInvalidArgument);

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

Result SetTieCommand::undo(Project& project) noexcept {
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

Result SetTieCommand::redo(Project& project) noexcept {
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
