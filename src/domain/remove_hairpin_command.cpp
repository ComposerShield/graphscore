// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/remove_hairpin_command.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>
#include "marking_command_helpers.hpp"

namespace graphscore {

Result RemoveHairpinCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  VoiceContent* voice =
      internal::resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  Node*               node     = project.find_node(node_id_);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end = timeline->node_end();

  try {
    VoiceContent pre_snapshot = *voice;
    VoiceContent candidate    = pre_snapshot;

    Result result = candidate.remove_hairpin(marking_id_);
    if (!result.ok())
      return result;

    Result vr = internal::validate_voice_candidate(candidate, node_end);
    if (!vr.ok())
      return vr;

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

Result RemoveHairpinCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  Result r =
      internal::voice_restore_snapshot(pre_snapshot_, post_snapshot_, node_id_,
                                       track_id_, stave_id_, voice_, project);
  if (!r.ok())
    return r;

  state_ = State::kUndone;
  return Result();
}

Result RemoveHairpinCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  Result r =
      internal::voice_restore_snapshot(post_snapshot_, pre_snapshot_, node_id_,
                                       track_id_, stave_id_, voice_, project);
  if (!r.ok())
    return r;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
