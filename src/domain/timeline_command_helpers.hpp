// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/clef_lane.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/tempo_lane.hpp>

namespace graphscore::internal {

inline NodeTimeline* resolve_node_timeline(Project&     project,
                                           const NodeId node_id) {
  Node* node = project.find_node(node_id);
  return node == nullptr ? nullptr : node->timeline();
}

inline const ClefLane* resolve_clef_lane(const NodeTimeline& timeline,
                                         const StaveId       stave_id) {
  return timeline.clef_lane(stave_id);
}

inline ClefLane* resolve_clef_lane(NodeTimeline& timeline,
                                   const StaveId stave_id) {
  return timeline.clef_lane(stave_id);
}

inline Result restore_clef_lane_snapshot(
    Project& project, const NodeId node_id, const StaveId stave_id,
    const ClefLane& snapshot, const ClefLane& expected_current) noexcept {
  NodeTimeline*   timeline = resolve_node_timeline(project, node_id);
  const ClefLane* lane =
      timeline == nullptr ? nullptr : resolve_clef_lane(*timeline, stave_id);
  if (lane == nullptr || *lane != expected_current)
    return Result(ResultCode::kInvalidArgument);

  try {
    ClefLane prepared = snapshot;
    return timeline->restore_clef_lane(stave_id, std::move(prepared));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

using TempoLaneSnapshot = std::optional<TempoLane>;

inline TempoLaneSnapshot snapshot_tempo_lane(const NodeTimeline& timeline) {
  const TempoLane* tempo = timeline.tempo();
  return tempo == nullptr ? std::nullopt : TempoLaneSnapshot(*tempo);
}

inline bool tempo_lane_equal(const NodeTimeline&      timeline,
                             const TempoLaneSnapshot& expected) {
  const TempoLane* tempo = timeline.tempo();
  if (!expected.has_value())
    return tempo == nullptr;
  return tempo != nullptr && *tempo == *expected;
}

struct PickdownTempoSnapshots {
  TempoLaneSnapshot pre;
  TempoLaneSnapshot post;
};

inline std::optional<PickdownTempoSnapshots> prepare_pickdown_tempo_snapshots(
    const NodeTimeline& timeline, const Rational post_end) {
  TempoLaneSnapshot pre = snapshot_tempo_lane(timeline);
  if (!pre.has_value())
    return PickdownTempoSnapshots{};

  std::optional<TempoLane> post =
      TempoLane::create(pre->points(), pre->start(), post_end);
  if (!post.has_value())
    return std::nullopt;
  return PickdownTempoSnapshots{std::move(pre), std::move(post)};
}

inline Result apply_pickdown_snapshot(NodeTimeline&                  timeline,
                                      const std::optional<Rational>& snapshot) {
  return snapshot.has_value() ? timeline.set_pickdown(*snapshot)
                              : timeline.clear_pickdown();
}

inline Result restore_pickdown_snapshot(
    Project& project, const NodeId node_id,
    const std::optional<Rational>& snapshot,
    const std::optional<Rational>& expected_current,
    const TempoLaneSnapshot&       expected_tempo) noexcept {
  NodeTimeline* timeline = resolve_node_timeline(project, node_id);
  if (timeline == nullptr ||
      timeline->pickdown_duration() != expected_current ||
      !tempo_lane_equal(*timeline, expected_tempo)) {
    return Result(ResultCode::kInvalidArgument);
  }

  try {
    return apply_pickdown_snapshot(*timeline, snapshot);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

}  // namespace graphscore::internal
