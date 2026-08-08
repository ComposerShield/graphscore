// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/track.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace graphscore {

// --- TrackLane mutation tracking (pedal spans) -------------------------------
// Fixed-capacity ring: pedal_deltas_ is std::array<Vec, kMaxTrackedRevisions>,
// with pedal_ring_head_ (next write position) and pedal_ring_count_ (valid).

static_assert(std::is_nothrow_move_assignable_v<std::vector<PedalDeltaOp>>);
static_assert(std::is_nothrow_move_assignable_v<PedalSpan>);

VoiceRevision TrackLane::capture_revision() const noexcept {
  return pedal_revision_;
}

std::optional<std::vector<PedalDeltaOp>> TrackLane::pedal_delta_since(
    VoiceRevision since) const {
  // Different lineages → stale token (copy/move-assignment reset).
  if (since.lineage_ != pedal_revision_.lineage_)
    return std::nullopt;

  if (pedal_revision_.value_ == since.value_)
    return std::vector<PedalDeltaOp>{};

  const std::uint32_t oldest =
      pedal_ring_count_ >= kMaxTrackedRevisions
          ? pedal_revision_.value_ -
                static_cast<std::uint32_t>(kMaxTrackedRevisions)
          : 0;
  if (since.value_ < oldest || since.value_ > pedal_revision_.value_)
    return std::nullopt;

  std::vector<PedalDeltaOp> merged;
  for (std::uint32_t j = 0; j < pedal_ring_count_; ++j) {
    const std::uint32_t first =
        (pedal_ring_head_ + kMaxTrackedRevisions - pedal_ring_count_) %
        static_cast<std::uint32_t>(kMaxTrackedRevisions);
    const std::uint32_t idx =
        (first + j) % static_cast<std::uint32_t>(kMaxTrackedRevisions);
    const std::uint32_t delta_rev = oldest + j + 1;
    if (delta_rev <= since.value_)
      continue;
    const std::vector<PedalDeltaOp>& d = pedal_deltas_[idx];
    merged.insert(merged.end(), d.begin(), d.end());
  }
  return merged;
}

void TrackLane::advance_revision(std::vector<PedalDeltaOp> deltas) noexcept {
  pedal_deltas_[pedal_ring_head_] = std::move(deltas);
  pedal_ring_head_ =
      (pedal_ring_head_ + 1) % static_cast<std::uint32_t>(kMaxTrackedRevisions);
  if (pedal_ring_count_ < kMaxTrackedRevisions)
    ++pedal_ring_count_;
  pedal_revision_ =
      VoiceRevision{pedal_revision_.value_ + 1, pedal_revision_.lineage_};
}

void TrackLane::reset_revision_tracking() noexcept {
  pedal_revision_   = VoiceRevision{};  // generates new lineage
  pedal_ring_head_  = 0;
  pedal_ring_count_ = 0;
}

bool TrackLane::operator==(const TrackLane& other) const {
  return staves_ == other.staves_ && pedal_spans_ == other.pedal_spans_;
}

TrackLane::TrackLane(const TrackLane& other)
    : staves_(other.staves_), pedal_spans_(other.pedal_spans_) {}

TrackLane& TrackLane::operator=(const TrackLane& other) {
  if (this == &other)
    return *this;
  TrackLane prepared(other);
  staves_.swap(prepared.staves_);
  pedal_spans_.swap(prepared.pedal_spans_);
  reset_revision_tracking();
  return *this;
}

TrackLane::TrackLane(TrackLane&& other) noexcept
    : staves_(std::move(other.staves_)),
      pedal_spans_(std::move(other.pedal_spans_)) {
  other.reset_revision_tracking();
}

TrackLane& TrackLane::operator=(TrackLane&& other) noexcept {
  if (this == &other)
    return *this;
  staves_      = std::move(other.staves_);
  pedal_spans_ = std::move(other.pedal_spans_);
  reset_revision_tracking();
  other.reset_revision_tracking();
  return *this;
}

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
  if (!staves_.contains(stave_id))
    return Result(ResultCode::kInvalidArgument);

  if (span.id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);

  for (const auto& entry : pedal_spans_) {
    if (std::ranges::any_of(entry.second, [span](const PedalSpan& existing) {
          return existing.id == span.id;
        })) {
      return Result(ResultCode::kInvalidArgument);
    }
  }

  // Prepare the journal payload before semantic mutation.
  RefOp<PedalSpan> op;
  op.kind   = RefOpKind::kAdd;
  op.id     = span.id;
  op.record = span;
  std::vector<PedalDeltaOp> deltas;
  deltas.reserve(1);
  deltas.push_back(PedalDeltaOp{stave_id, op});

  const auto existing = pedal_spans_.find(stave_id);
  try {
    if (existing == pedal_spans_.end()) {
      std::vector<PedalSpan> prepared{span};
      pedal_spans_.emplace(stave_id, std::move(prepared));
    } else {
      std::vector<PedalSpan> prepared = existing->second;
      prepared.push_back(span);
      existing->second.swap(prepared);
    }
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(deltas));  // noexcept commit
  return Result();
}

Result TrackLane::remove_pedal_span(StaveId stave_id, NotationEntityId id) {
  const auto it = pedal_spans_.find(stave_id);
  if (it == pedal_spans_.end())
    return Result(ResultCode::kInvalidArgument);

  // Prepare delta BEFORE mutation.
  RefOp<PedalSpan> op;
  op.kind = RefOpKind::kRemove;
  op.id   = id;
  std::vector<PedalDeltaOp> deltas;
  deltas.reserve(1);
  deltas.push_back(PedalDeltaOp{stave_id, op});

  const auto span_it =
      std::find_if(it->second.begin(), it->second.end(),
                   [id](const PedalSpan& s) { return s.id == id; });
  if (span_it == it->second.end())
    return Result(ResultCode::kInvalidArgument);

  try {
    std::vector<PedalSpan> prepared = it->second;
    prepared.erase(prepared.begin() + (span_it - it->second.begin()));
    it->second.swap(prepared);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(deltas));  // noexcept commit
  return Result();
}

}  // namespace graphscore
