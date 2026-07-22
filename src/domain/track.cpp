// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/track.hpp>

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

}  // namespace graphscore
