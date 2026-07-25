// SPDX-License-Identifier: Apache-2.0
//
// Internal helpers shared across the node tempo-lane commands.
// Not installed; included only by src/domain/* command .cpp files.

#pragma once

#include <new>
#include <optional>
#include <stdexcept>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/core/tempo_point.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/tempo_lane.hpp>

namespace graphscore {
namespace internal {

// A node's whole tempo-lane state as an undo snapshot. An empty optional
// means "the node had no tempo lane at all" — the valid default state — so
// that creating the first tempo point and removing the last one are both
// exactly reversible. It is NOT an "unset snapshot" marker: the commands'
// State machine guarantees both snapshots are meaningful whenever undo or
// redo may read them.
using TempoSnapshot = std::optional<std::vector<TempoPoint>>;

inline NodeTimeline* resolve_timeline(Project& project, const NodeId node_id) {
  Node* node = project.find_node(node_id);
  if (node == nullptr)
    return nullptr;
  return node->timeline();
}

// Reads the timeline's current tempo-lane state. Allocates; callers must
// invoke it inside their own bad_alloc/length_error guard.
inline TempoSnapshot read_tempo_points(const NodeTimeline& timeline) {
  const TempoLane* lane = timeline.tempo();
  if (lane == nullptr)
    return std::nullopt;
  return lane->points();
}

// Applies a snapshot to a timeline: an empty snapshot clears the lane, a
// non-empty one is revalidated by set_tempo against the CURRENT node_end()
// and leaves the timeline unchanged if it no longer fits. Allocates;
// callers must invoke it inside their own bad_alloc/length_error guard.
inline Result apply_tempo_points(NodeTimeline&        timeline,
                                 const TempoSnapshot& snapshot) {
  if (!snapshot.has_value()) {
    timeline.clear_tempo();
    return Result();
  }
  return timeline.set_tempo(*snapshot);
}

// Restores `snapshot` after verifying the node's current tempo-lane state
// equals `expected_current` (post_snapshot_ for undo, pre_snapshot_ for
// redo); a mismatch is stale context and is rejected without mutating the
// timeline. Does not touch command state.
inline Result tempo_restore_snapshot(const TempoSnapshot& snapshot,
                                     const TempoSnapshot& expected_current,
                                     const NodeId node_id, Project& project) {
  NodeTimeline* timeline = resolve_timeline(project, node_id);
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    const TempoSnapshot current = read_tempo_points(*timeline);
    if (current != expected_current)
      return Result(ResultCode::kInvalidArgument);

    return apply_tempo_points(*timeline, snapshot);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

}  // namespace internal
}  // namespace graphscore
