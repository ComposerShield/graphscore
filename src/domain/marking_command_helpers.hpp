// SPDX-License-Identifier: Apache-2.0
//
// Internal helpers shared across voice-scoped and pedal marking commands.
// Not installed; included only by src/domain/* command .cpp files.

#pragma once

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
#include <graphscore/domain/voice_content.hpp>
#include "command_snapshot_compare.hpp"

namespace graphscore {
namespace internal {

inline VoiceContent* resolve_voice(Project& project, const NodeId node_id,
                                   const TrackId track_id,
                                   const StaveId stave_id, const Voice voice) {
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

// Centralized candidate validation: checks rhythmic completeness,
// structural validity, and referential integrity. Idempotent on the
// VoiceContent argument. Returns kInvalidArgument on any diagnostic.
inline Result validate_voice_candidate(const VoiceContent& voice,
                                       const Rational      node_end) {
  try {
    if (!voice.check_complete(node_end).ok())
      return Result(ResultCode::kInvalidArgument);
    if (!voice.validate().ok())
      return Result(ResultCode::kInvalidArgument);

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(voice);
    if (!diags.empty())
      return Result(ResultCode::kInvalidArgument);

    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

// Centralized candidate lane validation: checks every voice's
// rhythmic completeness, structural validity, and referential
// integrity, plus every stave's pedal spans against node_end.
// Whole-lane snapshots (pedal add/remove) require every voice in
// every stave to be complete and valid so that a pedal command
// never silently accepts a lane whose underlying notation is broken.
inline Result validate_lane_candidate(const TrackLane& lane,
                                      const Rational   node_end) {
  try {
    const std::vector<NotationDiagnostic> lane_diags =
        validate_lane_references(lane, node_end);
    if (!lane_diags.empty())
      return Result(ResultCode::kInvalidArgument);

    // Check rhythmic completeness and structural validity for every voice.
    for (const StaveId stave_id : lane.stave_ids()) {
      const StaveVoices* stave = lane.stave(stave_id);
      if (stave == nullptr)
        continue;
      for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
        const std::optional<Voice> voice = Voice::create(v);
        if (!voice.has_value())
          continue;
        const VoiceContent& vc = stave->voice(*voice);
        if (!vc.check_complete(node_end).ok())
          return Result(ResultCode::kInvalidArgument);
        if (!vc.validate().ok())
          return Result(ResultCode::kInvalidArgument);
      }
    }

    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

// Apply a pre_snapshot to a voice, verifying the current content matches
// `expected_current` (post_snapshot_ for undo, pre_snapshot_ for redo).
// Validates the restored candidate against current node_end.
// Returns ok on success, or an error code if context is stale or validation
// fails. Does not touch command state.
//
// `displaced`, when non-null, receives the content this restore replaced, so
// a caller can publish-compensate a successful undo/redo by putting the exact
// displaced object back rather than by re-running the inverse operation that
// already failed downstream (Command::compensate_undo/compensate_redo). It is
// only written on success.
inline Result voice_restore_snapshot(
    std::optional<VoiceContent>&       pre_snapshot,
    const std::optional<VoiceContent>& expected_current, const NodeId node_id,
    const TrackId track_id, const StaveId stave_id, const Voice voice,
    Project& project, std::optional<VoiceContent>* displaced = nullptr) {
  if (!pre_snapshot.has_value())
    return Result(ResultCode::kInternalError);

  VoiceContent* vc = resolve_voice(project, node_id, track_id, stave_id, voice);
  if (vc == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (expected_current.has_value() && !snapshot_matches(*vc, *expected_current))
    return Result(ResultCode::kInvalidArgument);

  Node*               node     = project.find_node(node_id);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end = timeline->node_end();

  try {
    VoiceContent candidate = *pre_snapshot;

    Result vr = validate_voice_candidate(candidate, node_end);
    if (!vr.ok())
      return vr;

    if (displaced != nullptr)
      *displaced = std::move(*vc);
    *vc = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  return Result();
}

// Apply a pre_snapshot lane, verifying the current lane matches
// `expected_current`. Validates every voice and every pedal span against
// the current node_end. Returns ok or a stale/validation error.
//
// `displaced` carries the same publication-compensation contract as
// voice_restore_snapshot's, for the whole-lane (pedal) commands.
inline Result lane_restore_snapshot(
    const std::optional<TrackLane>& pre_snapshot,
    const std::optional<TrackLane>& expected_current, const NodeId node_id,
    const TrackId track_id, Project& project,
    std::optional<TrackLane>* displaced = nullptr) {
  if (!pre_snapshot.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  TrackLane* lane = node->lane(track_id);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (expected_current.has_value() &&
      !snapshot_matches(*lane, *expected_current))
    return Result(ResultCode::kInvalidArgument);

  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Rational node_end = timeline->node_end();

  try {
    TrackLane candidate = *pre_snapshot;

    Result vr = validate_lane_candidate(candidate, node_end);
    if (!vr.ok())
      return vr;

    if (displaced != nullptr)
      *displaced = std::move(*lane);
    *lane = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  return Result();
}

}  // namespace internal
}  // namespace graphscore
