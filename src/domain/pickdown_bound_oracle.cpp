// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/pickdown_bound_oracle.hpp>

#include <cstdint>

namespace graphscore {

PickdownBoundResult prove_pickdown_overlap_bound(const Project& project) {
  std::uint64_t bound = 0;
  for (const Node& node : project.nodes()) {
    const NodeTimeline* timeline = node.timeline();
    if (timeline != nullptr && timeline->pickdown_duration().has_value())
      ++bound;
  }

  PickdownBoundResult result;
  result.status = PickdownBoundStatus::kBounded;
  result.bound  = bound;
  return result;
}

}  // namespace graphscore
