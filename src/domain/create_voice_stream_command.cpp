// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/create_voice_stream_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
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

VoiceContent* resolve_voice(Project& project, NodeId node_id, TrackId track_id,
                            StaveId stave_id, Voice voice) {
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

Result CreateVoiceStreamCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  VoiceContent* voice =
      resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);
  if (!voice->events().empty())
    return Result(ResultCode::kInvalidArgument);

  Node*               node     = project.find_node(node_id_);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    const std::optional<std::vector<Rest>> rests =
        decompose_measure_aligned_rests(*timeline);
    if (!rests.has_value())
      return Result(ResultCode::kInvalidArgument);

    VoiceContent pre_snapshot = *voice;
    VoiceContent candidate;
    for (const Rest& rest : *rests) {
      const Result append_result = candidate.append(rest);
      if (!append_result.ok())
        return append_result;
    }

    if (!candidate.check_complete(timeline->node_end()).ok())
      return Result(ResultCode::kInvalidArgument);

    Result result = candidate.validate();
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

Result CreateVoiceStreamCommand::undo(Project& project) noexcept {
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

  try {
    *voice = *pre_snapshot_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kUndone;
  return Result();
}

Result CreateVoiceStreamCommand::redo(Project& project) noexcept {
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
