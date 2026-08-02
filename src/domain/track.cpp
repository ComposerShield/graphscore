// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/track.hpp>

#include <algorithm>
#include <cassert>
#include <stdexcept>
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

Rational TrackLane::total_length() const {
  Rational max_len(0);
  for (const auto& entry : staves_) {
    const StaveVoices& sv = entry.second;
    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
      const std::optional<Voice> voice_opt = Voice::create(v);
      assert(voice_opt.has_value());
      const Rational len = sv.voice(*voice_opt).total_length();
      if (len > max_len)
        max_len = len;
    }
  }
  return max_len;
}

std::vector<StaveId> TrackLane::stave_ids() const {
  std::vector<StaveId> ids;
  ids.reserve(staves_.size());
  for (const auto& entry : staves_)
    ids.push_back(entry.first);
  std::ranges::sort(ids, {},
                    [](const StaveId id) { return id.value().bytes(); });
  return ids;
}

const std::vector<PedalSpan>* TrackLane::pedal_spans(StaveId stave_id) const {
  const auto it = pedal_spans_.find(stave_id);
  return it == pedal_spans_.end() ? nullptr : &it->second;
}

Result TrackLane::add_pedal_span(StaveId stave_id, PedalSpan span) {
  // Reject pedal spans for a nonexistent stave: a stave must be present
  // in staves_ (created via ensure_stave) before any pedal span can be
  // added to it.  This prevents orphan pedal map keys that validation
  // (which enumerates only real stave_ids) would never see.
  if (!staves_.contains(stave_id))
    return Result(ResultCode::kInvalidArgument);

  // Reject nil identifier — every PedalSpan must carry a non-nil identity
  // before any collision scan.
  if (span.id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);

  // Reject duplicate pedal ID across all staves.
  for (const auto& entry : pedal_spans_) {
    for (const PedalSpan& existing : entry.second) {
      if (existing.id == span.id)
        return Result(ResultCode::kInvalidArgument);
    }
  }

  const auto it = pedal_spans_.find(stave_id);
  try {
    if (it == pedal_spans_.end()) {
      // Absent key: build the populated vector first, then emplace.
      // If construction fails the map is never touched.
      std::vector<PedalSpan> prepared;
      prepared.reserve(1);
      prepared.push_back(span);
      pedal_spans_.emplace(stave_id, std::move(prepared));
    } else {
      // Existing key: copy, append under guard, then nonthrowing swap.
      std::vector<PedalSpan> prepared = it->second;
      prepared.reserve(prepared.size() + 1);
      prepared.push_back(span);
      // swap is noexcept; no further allocation after commit begins.
      using std::swap;
      swap(it->second, prepared);
    }
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result TrackLane::remove_pedal_span(StaveId stave_id, NotationEntityId id) {
  const auto it = pedal_spans_.find(stave_id);
  if (it == pedal_spans_.end())
    return Result(ResultCode::kInvalidArgument);

  const auto span_it =
      std::find_if(it->second.begin(), it->second.end(),
                   [id](const PedalSpan& s) { return s.id == id; });
  if (span_it == it->second.end())
    return Result(ResultCode::kInvalidArgument);

  try {
    it->second.erase(span_it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

}  // namespace graphscore
