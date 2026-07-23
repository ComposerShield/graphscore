// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/track.hpp>

#include <utility>
#include <vector>

namespace graphscore {

StaveVoices* TrackLane::stave(StaveId stave_id) {
  const auto it = staves_.find(stave_id);
  return it == staves_.end() ? nullptr : &it->second;
}

const StaveVoices* TrackLane::stave(StaveId stave_id) const {
  const auto it = staves_.find(stave_id);
  return it == staves_.end() ? nullptr : &it->second;
}

void TrackLane::ensure_stave(StaveId stave_id) {
  staves_.try_emplace(stave_id);
}

std::vector<StaveId> TrackLane::stave_ids() const {
  std::vector<StaveId> ids;
  ids.reserve(staves_.size());
  for (const auto& entry : staves_)
    ids.push_back(entry.first);
  return ids;
}

const std::vector<PedalSpan>* TrackLane::pedal_spans(StaveId stave_id) const {
  const auto it = pedal_spans_.find(stave_id);
  return it == pedal_spans_.end() ? nullptr : &it->second;
}

void TrackLane::add_pedal_span(StaveId stave_id, PedalSpan span) {
  pedal_spans_[stave_id].push_back(span);
}

}  // namespace graphscore
