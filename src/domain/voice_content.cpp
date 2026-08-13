// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/voice_content.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/node_timeline.hpp>

namespace graphscore {

// --- VoiceContent mutation tracking
// -------------------------------------------
// Fixed-capacity ring: deltas_ is std::array<VoiceDelta, kMaxTrackedRevisions>,
// with ring_head_ (next write position) and ring_count_ (valid entries).
// Delta payload allocation happens before semantic mutation. Moving a prepared
// payload into a ring slot is required to be non-throwing.

static_assert(std::is_nothrow_move_assignable_v<VoiceDelta>);
static_assert(std::is_nothrow_move_constructible_v<VoiceEvent>);
static_assert(std::is_nothrow_move_assignable_v<DynamicMarking>);
static_assert(std::is_nothrow_move_assignable_v<Hairpin>);
static_assert(std::is_nothrow_move_assignable_v<Slur>);
static_assert(std::is_nothrow_move_assignable_v<BeamOverride>);
static_assert(std::is_nothrow_move_assignable_v<GraceGroup>);

VoiceRevision VoiceContent::capture_revision() const noexcept {
  return revision_;
}

std::optional<VoiceDelta> VoiceContent::delta_since(VoiceRevision since) const {
  // Different lineages → stale token (copy/move-assignment reset).
  if (since.lineage_ != revision_.lineage_)
    return std::nullopt;

  if (revision_.value_ == since.value_)
    return VoiceDelta{};

  const std::uint32_t oldest =
      ring_count_ >= kMaxTrackedRevisions
          ? revision_.value_ - static_cast<std::uint32_t>(kMaxTrackedRevisions)
          : 0;
  if (since.value_ < oldest || since.value_ > revision_.value_)
    return std::nullopt;

  VoiceDelta merged;
  for (std::uint32_t j = 0; j < ring_count_; ++j) {
    // Walk ring from oldest to newest.
    const std::uint32_t first =
        (ring_head_ + kMaxTrackedRevisions - ring_count_) %
        static_cast<std::uint32_t>(kMaxTrackedRevisions);
    const std::uint32_t idx =
        (first + j) % static_cast<std::uint32_t>(kMaxTrackedRevisions);
    const VoiceDelta&   d         = deltas_[idx];
    const std::uint32_t delta_rev = oldest + j + 1;
    if (delta_rev <= since.value_)
      continue;
    if (d.full_reset) {
      merged.full_reset = true;
      break;
    }
    if (d.event_reorder)
      merged.event_reorder = true;
    merged.changed_event_ids.insert(merged.changed_event_ids.end(),
                                    d.changed_event_ids.begin(),
                                    d.changed_event_ids.end());
    merged.dynamic_ops.insert(merged.dynamic_ops.end(), d.dynamic_ops.begin(),
                              d.dynamic_ops.end());
    merged.hairpin_ops.insert(merged.hairpin_ops.end(), d.hairpin_ops.begin(),
                              d.hairpin_ops.end());
    merged.slur_ops.insert(merged.slur_ops.end(), d.slur_ops.begin(),
                           d.slur_ops.end());
    merged.beam_override_ops.insert(merged.beam_override_ops.end(),
                                    d.beam_override_ops.begin(),
                                    d.beam_override_ops.end());
    merged.grace_group_ops.insert(merged.grace_group_ops.end(),
                                  d.grace_group_ops.begin(),
                                  d.grace_group_ops.end());
  }
  return merged;
}

void VoiceContent::advance_revision(VoiceDelta delta) noexcept {
  deltas_[ring_head_] = std::move(delta);
  ring_head_ =
      (ring_head_ + 1) % static_cast<std::uint32_t>(kMaxTrackedRevisions);
  if (ring_count_ < kMaxTrackedRevisions)
    ++ring_count_;
  revision_ = VoiceRevision{revision_.value_ + 1, revision_.lineage_};
}

void VoiceContent::reset_revision_tracking() noexcept {
  revision_   = VoiceRevision{};  // generates new lineage
  ring_head_  = 0;
  ring_count_ = 0;
}

bool VoiceContent::operator==(const VoiceContent& other) const {
  return events_ == other.events_ && dynamics_ == other.dynamics_ &&
         hairpins_ == other.hairpins_ && slurs_ == other.slurs_ &&
         beam_overrides_ == other.beam_overrides_ &&
         grace_groups_ == other.grace_groups_;
}

VoiceContent::VoiceContent(const VoiceContent& other)
    : events_(other.events_),
      dynamics_(other.dynamics_),
      hairpins_(other.hairpins_),
      slurs_(other.slurs_),
      beam_overrides_(other.beam_overrides_),
      grace_groups_(other.grace_groups_) {}

VoiceContent& VoiceContent::operator=(const VoiceContent& other) {
  if (this == &other)
    return *this;
  VoiceContent prepared(other);
  events_.swap(prepared.events_);
  dynamics_.swap(prepared.dynamics_);
  hairpins_.swap(prepared.hairpins_);
  slurs_.swap(prepared.slurs_);
  beam_overrides_.swap(prepared.beam_overrides_);
  grace_groups_.swap(prepared.grace_groups_);
  reset_revision_tracking();
  return *this;
}

VoiceContent::VoiceContent(VoiceContent&& other) noexcept
    : events_(std::move(other.events_)),
      dynamics_(std::move(other.dynamics_)),
      hairpins_(std::move(other.hairpins_)),
      slurs_(std::move(other.slurs_)),
      beam_overrides_(std::move(other.beam_overrides_)),
      grace_groups_(std::move(other.grace_groups_)) {
  other.reset_revision_tracking();
}

VoiceContent& VoiceContent::operator=(VoiceContent&& other) noexcept {
  if (this == &other)
    return *this;
  events_         = std::move(other.events_);
  dynamics_       = std::move(other.dynamics_);
  hairpins_       = std::move(other.hairpins_);
  slurs_          = std::move(other.slurs_);
  beam_overrides_ = std::move(other.beam_overrides_);
  grace_groups_   = std::move(other.grace_groups_);
  reset_revision_tracking();
  other.reset_revision_tracking();
  return *this;
}

// --- Internal helpers --------------------------------------------------------

namespace {

[[nodiscard]] bool event_id_exists(const std::vector<VoiceEvent>& events,
                                   NotationEntityId               id) {
  for (const VoiceEvent& event : events) {
    if (event_id(event) == id)
      return true;
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& note : chord->notes) {
        if (note.id == id)
          return true;
      }
    }
  }
  return false;
}

std::optional<std::size_t> find_index_at(const std::vector<VoiceEvent>& events,
                                         Rational position) {
  Rational cumulative(0);
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (cumulative == position)
      return i;
    cumulative = cumulative + event_duration(events[i]).resolved();
  }
  if (cumulative == position)
    return events.size();
  return std::nullopt;
}

Rational total_length_of(const std::vector<VoiceEvent>& events) {
  Rational total(0);
  for (const VoiceEvent& event : events)
    total = total + event_duration(event).resolved();
  return total;
}

// Normalize a temporary events vector to `target_length` by appending
// decompose_rest output. Returns a Result; allocation failure is caught.
Result normalize_temp(std::vector<VoiceEvent>& events, Rational target_length) {
  const Rational current = total_length_of(events);
  if (current > target_length)
    return Result(ResultCode::kInvalidArgument);
  if (current == target_length)
    return Result();

  std::optional<std::vector<Rest>> rests;
  try {
    rests = decompose_rest(target_length - current);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!rests.has_value())
    return Result(ResultCode::kInvalidArgument);

  try {
    for (const Rest& rest : *rests)
      events.push_back(rest);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

// Decompose `length` into rests, assigning `first_id` to the first Rest
// produced.  This preserves a consumed Rest's NotationEntityId on the
// surviving remainder so markings referencing it stay valid.
//
// Returns a Result + output vector so allocation failure is distinguishable
// from an invalid length (callers propagate kOutOfMemory).
struct DecomposeVoiceResult {
  Result                  result;
  std::vector<VoiceEvent> events;
};

[[nodiscard]] DecomposeVoiceResult decompose_rest_with_id(
    Rational length, NotationEntityId first_id) {
  try {
    const std::optional<std::vector<Rest>> rests = decompose_rest(length);
    if (!rests.has_value())
      return {Result(ResultCode::kInvalidArgument), {}};

    std::vector<VoiceEvent> events;
    events.reserve(rests->size());
    bool gave_id = false;
    for (const Rest& r : *rests) {
      if (!gave_id) {
        events.push_back(VoiceEvent(Rest{first_id, r.duration}));
        gave_id = true;
      } else {
        events.push_back(r);
      }
    }
    return {Result(), std::move(events)};
  } catch (const std::bad_alloc&) {
    return {Result(ResultCode::kOutOfMemory), {}};
  } catch (const std::length_error&) {
    return {Result(ResultCode::kOutOfMemory), {}};
  }
}

}  // namespace

// Helper to build an operation-complete VoiceDelta for event mutations.
namespace {
VoiceDelta make_event_delta(std::vector<NotationEntityId> ids, bool reorder) {
  VoiceDelta d;
  d.changed_event_ids = std::move(ids);
  d.event_reorder     = reorder;
  return d;
}

template <typename Record>
RefOp<Record> make_add_op(Record record) {
  return RefOp<Record>{RefOpKind::kAdd, record.id, std::move(record)};
}

template <typename Record>
RefOp<Record> make_remove_op(NotationEntityId id) {
  return RefOp<Record>{RefOpKind::kRemove, id, Record{}};
}
}  // namespace

Result VoiceContent::append(VoiceEvent event) {
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (chord->notes.size() < 2)
      return Result(ResultCode::kInvalidArgument);
  }

  const NotationEntityId new_id = event_id(event);
  if (new_id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (marking_id_exists(new_id))
    return Result(ResultCode::kInvalidArgument);
  if (const auto* chord = std::get_if<Chord>(&event)) {
    const NotationEntityId parent_id = chord->id;
    for (std::size_t i = 0; i < chord->notes.size(); ++i) {
      const NotationEntityId cn_id = chord->notes[i].id;
      if (cn_id == NotationEntityId{})
        return Result(ResultCode::kInvalidArgument);
      if (cn_id == parent_id)
        return Result(ResultCode::kInvalidArgument);
      for (std::size_t j = 0; j < i; ++j) {
        if (cn_id == chord->notes[j].id)
          return Result(ResultCode::kInvalidArgument);
      }
      if (marking_id_exists(cn_id))
        return Result(ResultCode::kInvalidArgument);
    }
  }

  // Prepare the journal payload before semantic mutation.
  VoiceDelta d = make_event_delta({new_id}, true);

  try {
    events_.push_back(std::move(event));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

std::optional<std::size_t> VoiceContent::find_event_index_at(
    Rational position) const {
  const std::optional<std::size_t> index = find_index_at(events_, position);
  if (!index.has_value() || *index >= events_.size())
    return std::nullopt;
  return index;
}

std::optional<Rational> VoiceContent::position_of_event(
    NotationEntityId id) const {
  NotationEntityId              resolved = id;
  std::vector<NotationEntityId> visited;

  while (true) {
    Rational cumulative(0);
    for (std::size_t i = 0; i < events_.size(); ++i) {
      const VoiceEvent& event = events_[i];
      if (event_id(event) == resolved)
        return cumulative;

      if (const auto* chord = std::get_if<Chord>(&event)) {
        for (const ChordNote& cn : chord->notes) {
          if (cn.id == resolved)
            return cumulative;
        }
      }

      cumulative = cumulative + event_duration(event).resolved();
    }

    bool advanced = false;
    for (const GraceGroup& g : grace_groups_) {
      for (const GraceNote& gn : g.notes) {
        if (gn.id == resolved) {
          if (std::find(visited.begin(), visited.end(), g.principal_event) !=
              visited.end())
            return std::nullopt;
          visited.push_back(g.principal_event);

          resolved = g.principal_event;
          advanced = true;
          break;
        }
      }
      if (advanced)
        break;
    }

    if (!advanced)
      return std::nullopt;
  }
}

Result VoiceContent::insert_event(Rational position, VoiceEvent event,
                                  Rational target_length) {
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (chord->notes.size() < 2)
      return Result(ResultCode::kInvalidArgument);
  }

  const std::optional<std::size_t> index = find_index_at(events_, position);
  if (!index.has_value())
    return Result(ResultCode::kInvalidArgument);

  const NotationEntityId new_id = event_id(event);
  if (new_id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (marking_id_exists(new_id))
    return Result(ResultCode::kInvalidArgument);
  if (const auto* chord = std::get_if<Chord>(&event)) {
    const NotationEntityId parent_id = chord->id;
    for (std::size_t i = 0; i < chord->notes.size(); ++i) {
      const NotationEntityId cn_id = chord->notes[i].id;
      if (cn_id == NotationEntityId{})
        return Result(ResultCode::kInvalidArgument);
      if (cn_id == parent_id)
        return Result(ResultCode::kInvalidArgument);
      for (std::size_t j = 0; j < i; ++j) {
        if (cn_id == chord->notes[j].id)
          return Result(ResultCode::kInvalidArgument);
      }
      if (marking_id_exists(cn_id))
        return Result(ResultCode::kInvalidArgument);
    }
  }

  const Rational new_dur = event_duration(event).resolved();
  if (*index == events_.size()) {
    const Rational new_total = total_length() + new_dur;
    if (new_total > target_length)
      return Result(ResultCode::kInvalidArgument);

    std::vector<VoiceEvent> temp;
    try {
      temp = events_;
      temp.push_back(event);
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }

    const Result norm_result = normalize_temp(temp, target_length);
    if (!norm_result.ok())
      return norm_result;

    VoiceDelta d = make_event_delta({new_id}, true);  // prepare BEFORE swap
    events_.swap(temp);
    advance_revision(std::move(d));  // noexcept commit
    return Result();
  }

  if (!std::holds_alternative<Rest>(events_[*index]))
    return Result(ResultCode::kInvalidArgument);

  Rational          consumed(0);
  const std::size_t consume_start   = *index;
  std::size_t       consume_end     = *index;
  bool              split_remainder = false;
  Rational          split_remainder_amount(0);
  NotationEntityId  split_rest_id;

  for (std::size_t i = *index; i < events_.size() && consumed < new_dur; ++i) {
    if (!std::holds_alternative<Rest>(events_[i]))
      break;

    const Rational rest_dur = event_duration(events_[i]).resolved();
    if (consumed + rest_dur <= new_dur) {
      consumed = consumed + rest_dur;
      ++consume_end;
    } else {
      split_remainder_amount = (consumed + rest_dur) - new_dur;
      split_remainder        = true;
      split_rest_id          = event_id(events_[i]);
      consumed               = new_dur;
      ++consume_end;
      break;
    }
  }

  if (consumed < new_dur)
    return Result(ResultCode::kInvalidArgument);

  std::vector<VoiceEvent> temp;
  try {
    temp.reserve(events_.size() + 4);

    temp.insert(temp.end(), events_.begin(),
                events_.begin() + static_cast<std::ptrdiff_t>(consume_start));

    temp.push_back(event);

    if (split_remainder && split_remainder_amount > Rational(0)) {
      DecomposeVoiceResult remainder =
          decompose_rest_with_id(split_remainder_amount, split_rest_id);
      if (!remainder.result.ok())
        return remainder.result;
      temp.insert(temp.end(), remainder.events.begin(), remainder.events.end());
    }

    temp.insert(temp.end(),
                events_.begin() + static_cast<std::ptrdiff_t>(consume_end),
                events_.end());
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const Result norm_result = normalize_temp(temp, target_length);
  if (!norm_result.ok())
    return norm_result;

  VoiceDelta d = make_event_delta({new_id}, true);  // prepare BEFORE swap
  events_.swap(temp);
  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::remove_event(Rational position, Rational target_length) {
  const std::optional<std::size_t> index = find_index_at(events_, position);
  if (!index.has_value())
    return Result(ResultCode::kInvalidArgument);
  if (*index >= events_.size())
    return Result(ResultCode::kInvalidArgument);

  const Rational         old_dur = event_duration(events_[*index]).resolved();
  const NotationEntityId removed_id = event_id(events_[*index]);

  std::optional<std::vector<Rest>> gap_rests;
  try {
    gap_rests = decompose_rest(old_dur);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!gap_rests.has_value())
    return Result(ResultCode::kInvalidArgument);

  std::vector<VoiceEvent> temp;
  try {
    temp = events_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  try {
    temp.erase(temp.begin() + static_cast<std::ptrdiff_t>(*index));
    temp.insert(temp.begin() + static_cast<std::ptrdiff_t>(*index),
                gap_rests->begin(), gap_rests->end());
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const Result norm_result = normalize_temp(temp, target_length);
  if (!norm_result.ok())
    return norm_result;

  VoiceDelta d = make_event_delta({removed_id}, true);  // prepare BEFORE swap
  events_.swap(temp);
  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::replace_event(Rational position, VoiceEvent event,
                                   Rational target_length) {
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (chord->notes.size() < 2)
      return Result(ResultCode::kInvalidArgument);
  }

  const std::optional<std::size_t> index = find_index_at(events_, position);
  if (!index.has_value())
    return Result(ResultCode::kInvalidArgument);
  if (*index >= events_.size())
    return Result(ResultCode::kInvalidArgument);

  const NotationEntityId new_id    = event_id(event);
  const NotationEntityId target_id = event_id(events_[*index]);
  if (new_id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (id_collision_if_not_target(new_id, *index))
    return Result(ResultCode::kInvalidArgument);
  if (const auto* repl_chord = std::get_if<Chord>(&event)) {
    const NotationEntityId parent_id = repl_chord->id;
    for (std::size_t i = 0; i < repl_chord->notes.size(); ++i) {
      const NotationEntityId cn_id = repl_chord->notes[i].id;
      if (cn_id == NotationEntityId{})
        return Result(ResultCode::kInvalidArgument);
      if (cn_id == parent_id)
        return Result(ResultCode::kInvalidArgument);
      for (std::size_t j = 0; j < i; ++j) {
        if (cn_id == repl_chord->notes[j].id)
          return Result(ResultCode::kInvalidArgument);
      }
      if (id_collision_if_not_target(cn_id, *index))
        return Result(ResultCode::kInvalidArgument);
    }
  }

  const Rational old_dur = event_duration(events_[*index]).resolved();
  const Rational new_dur = event_duration(event).resolved();

  std::vector<VoiceEvent> temp;
  try {
    temp = events_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  if (new_dur < old_dur) {
    const Rational                   gap = old_dur - new_dur;
    std::optional<std::vector<Rest>> gap_rests;
    try {
      gap_rests = decompose_rest(gap);
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }
    if (!gap_rests.has_value())
      return Result(ResultCode::kInvalidArgument);

    try {
      temp[*index] = std::move(event);
      temp.insert(temp.begin() + static_cast<std::ptrdiff_t>(*index + 1),
                  gap_rests->begin(), gap_rests->end());
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }

    const Result norm_result = normalize_temp(temp, target_length);
    if (!norm_result.ok())
      return norm_result;

    VoiceDelta d = make_event_delta({target_id, new_id}, true);
    if (new_id != target_id)
      d.full_reset = true;
    events_.swap(temp);
    if (new_id != target_id)
      remap_event_id_across_references(target_id, new_id);
    advance_revision(std::move(d));
    return Result();
  }

  if (new_dur == old_dur) {
    try {
      temp[*index] = std::move(event);
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    }

    const Result norm_result = normalize_temp(temp, target_length);
    if (!norm_result.ok())
      return norm_result;

    VoiceDelta d = make_event_delta({target_id, new_id}, false);
    if (new_id != target_id)
      d.full_reset = true;
    events_.swap(temp);
    if (new_id != target_id)
      remap_event_id_across_references(target_id, new_id);
    advance_revision(std::move(d));
    return Result();
  }

  // Duration expansion: consume immediately following rests.
  const Rational    needed = new_dur - old_dur;
  Rational          consumed(0);
  const std::size_t erase_start = *index + 1;
  std::size_t       erase_end   = erase_start;

  for (std::size_t i = *index + 1; i < temp.size() && consumed < needed; ++i) {
    if (!std::holds_alternative<Rest>(temp[i]))
      break;

    const Rational rest_dur = event_duration(temp[i]).resolved();
    if (consumed + rest_dur <= needed) {
      consumed = consumed + rest_dur;
      ++erase_end;
    } else {
      const Rational remainder = (consumed + rest_dur) - needed;
      consumed                 = needed;
      if (remainder <= Rational(0)) {
        ++erase_end;
      } else {
        const NotationEntityId orig_rest_id = event_id(temp[i]);
        DecomposeVoiceResult   tail;
        try {
          tail = decompose_rest_with_id(remainder, orig_rest_id);
        } catch (const std::bad_alloc&) {
          return Result(ResultCode::kOutOfMemory);
        } catch (const std::length_error&) {
          return Result(ResultCode::kOutOfMemory);
        }
        if (!tail.result.ok())
          return tail.result;

        std::vector<VoiceEvent> suffix;
        try {
          suffix.assign(temp.begin() + static_cast<std::ptrdiff_t>(i + 1),
                        temp.end());
        } catch (const std::bad_alloc&) {
          return Result(ResultCode::kOutOfMemory);
        } catch (const std::length_error&) {
          return Result(ResultCode::kOutOfMemory);
        }

        try {
          temp.erase(temp.begin() + static_cast<std::ptrdiff_t>(i), temp.end());
          temp.insert(temp.end(), tail.events.begin(), tail.events.end());
          temp.insert(temp.end(), suffix.begin(), suffix.end());
        } catch (const std::bad_alloc&) {
          return Result(ResultCode::kOutOfMemory);
        } catch (const std::length_error&) {
          return Result(ResultCode::kOutOfMemory);
        }
        erase_end = erase_start;
        break;
      }
    }
  }

  if (consumed < needed)
    return Result(ResultCode::kInvalidArgument);

  if (erase_end > erase_start) {
    try {
      temp.erase(temp.begin() + static_cast<std::ptrdiff_t>(erase_start),
                 temp.begin() + static_cast<std::ptrdiff_t>(erase_end));
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }
  }

  try {
    temp[*index] = std::move(event);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const Result norm_result = normalize_temp(temp, target_length);
  if (!norm_result.ok())
    return norm_result;

  VoiceDelta d = make_event_delta({target_id, new_id}, true);
  if (new_id != target_id)
    d.full_reset = true;
  events_.swap(temp);
  if (new_id != target_id)
    remap_event_id_across_references(target_id, new_id);
  advance_revision(std::move(d));
  return Result();
}

Result VoiceContent::set_notehead_pitch(NotationEntityId id,
                                        SpelledPitch     pitch) {
  // Top-level Note, or a ChordNote inside a Chord.
  for (std::size_t i = 0; i < events_.size(); ++i) {
    if (const auto* note = std::get_if<Note>(&events_[i])) {
      if (note->id != id)
        continue;
      try {
        Note modified  = *note;
        modified.pitch = pitch;
        events_[i]     = std::move(modified);
      } catch (const std::bad_alloc&) {
        return Result(ResultCode::kOutOfMemory);
      } catch (const std::length_error&) {
        return Result(ResultCode::kOutOfMemory);
      }
      advance_revision(make_event_delta({id}, false));
      return Result();
    }
    if (const auto* chord = std::get_if<Chord>(&events_[i])) {
      for (std::size_t j = 0; j < chord->notes.size(); ++j) {
        if (chord->notes[j].id != id)
          continue;
        const NotationEntityId chord_id = chord->id;
        try {
          Chord modified          = *chord;
          modified.notes[j].pitch = pitch;
          events_[i]              = std::move(modified);
        } catch (const std::bad_alloc&) {
          return Result(ResultCode::kOutOfMemory);
        } catch (const std::length_error&) {
          return Result(ResultCode::kOutOfMemory);
        }
        advance_revision(make_event_delta({chord_id}, false));
        return Result();
      }
    }
  }

  // A GraceNote inside a GraceGroup.
  for (std::size_t g = 0; g < grace_groups_.size(); ++g) {
    for (std::size_t j = 0; j < grace_groups_[g].notes.size(); ++j) {
      if (grace_groups_[g].notes[j].id != id)
        continue;
      VoiceDelta d;
      try {
        GraceGroup modified     = grace_groups_[g];
        modified.notes[j].pitch = pitch;
        d.grace_group_ops.push_back(
            RefOp<GraceGroup>{RefOpKind::kUpdate, modified.id, modified});
        grace_groups_[g] = std::move(modified);
      } catch (const std::bad_alloc&) {
        return Result(ResultCode::kOutOfMemory);
      } catch (const std::length_error&) {
        return Result(ResultCode::kOutOfMemory);
      }
      advance_revision(std::move(d));  // noexcept commit
      return Result();
    }
  }

  return Result(ResultCode::kInvalidArgument);
}

Rational VoiceContent::total_length() const {
  return total_length_of(events_);
}

Result VoiceContent::check_complete(Rational target_length) const {
  return total_length() == target_length ? Result()
                                         : Result(ResultCode::kInvalidArgument);
}

Result VoiceContent::normalize(Rational target_length) {
  const Rational current = total_length();
  if (current > target_length)
    return Result(ResultCode::kInvalidArgument);
  if (current == target_length)
    return Result();

  std::optional<std::vector<Rest>> rests;
  try {
    rests = decompose_rest(target_length - current);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!rests.has_value())
    return Result(ResultCode::kInvalidArgument);

  std::vector<NotationEntityId> new_rest_ids;
  std::vector<VoiceEvent>       temp;
  try {
    new_rest_ids.reserve(rests->size());
    for (const Rest& rest : *rests)
      new_rest_ids.push_back(rest.id);
    temp.reserve(events_.size() + rests->size());
    temp = events_;
    for (const Rest& rest : *rests)
      temp.push_back(rest);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  VoiceDelta d = make_event_delta(std::move(new_rest_ids), true);
  events_.swap(temp);              // nonthrowing
  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::validate() const {
  return validate_ties(events_);
}

void VoiceContent::clear() {
  // Prepare the journal payload before clearing content.
  VoiceDelta full_reset_delta;
  full_reset_delta.full_reset = true;

  events_.clear();
  dynamics_.clear();
  hairpins_.clear();
  slurs_.clear();
  beam_overrides_.clear();
  grace_groups_.clear();
  // Commit — noexcept.
  advance_revision(std::move(full_reset_delta));
}

bool VoiceContent::marking_id_exists(NotationEntityId id) const {
  if (event_id_exists(events_, id))
    return true;
  for (const DynamicMarking& marking : dynamics_) {
    if (marking.id == id)
      return true;
  }
  for (const Hairpin& hairpin : hairpins_) {
    if (hairpin.id == id)
      return true;
  }
  for (const Slur& slur : slurs_) {
    if (slur.id == id)
      return true;
  }
  for (const BeamOverride& override : beam_overrides_) {
    if (override.id == id)
      return true;
  }
  for (const GraceGroup& group : grace_groups_) {
    if (group.id == id)
      return true;
    for (const GraceNote& note : group.notes) {
      if (note.id == id)
        return true;
    }
  }
  return false;
}

bool VoiceContent::id_collision_if_not_target(NotationEntityId id,
                                              std::size_t event_index) const {
  for (const DynamicMarking& marking : dynamics_) {
    if (marking.id == id)
      return true;
  }
  for (const Hairpin& hairpin : hairpins_) {
    if (hairpin.id == id)
      return true;
  }
  for (const Slur& slur : slurs_) {
    if (slur.id == id)
      return true;
  }
  for (const BeamOverride& override : beam_overrides_) {
    if (override.id == id)
      return true;
  }
  for (const GraceGroup& group : grace_groups_) {
    if (group.id == id)
      return true;
    for (const GraceNote& note : group.notes) {
      if (note.id == id)
        return true;
    }
  }
  for (std::size_t index = 0; index < events_.size(); ++index) {
    if (index == event_index)
      continue;
    if (event_id(events_[index]) == id)
      return true;
    if (const auto* chord = std::get_if<Chord>(&events_[index])) {
      for (const ChordNote& note : chord->notes) {
        if (note.id == id)
          return true;
      }
    }
  }
  return false;
}

Result VoiceContent::add_dynamic(DynamicMarking marking) {
  if (marking.id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (marking_id_exists(marking.id))
    return Result(ResultCode::kInvalidArgument);

  // Prepare the journal payload before semantic mutation.
  VoiceDelta d;
  d.dynamic_ops.push_back(make_add_op(marking));

  try {
    dynamics_.push_back(marking);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::add_hairpin(Hairpin hairpin) {
  if (hairpin.id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (marking_id_exists(hairpin.id))
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.hairpin_ops.push_back(make_add_op(hairpin));

  try {
    hairpins_.push_back(hairpin);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::add_slur(Slur slur) {
  if (slur.id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (marking_id_exists(slur.id))
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.slur_ops.push_back(make_add_op(slur));

  try {
    slurs_.push_back(slur);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::add_beam_override(BeamOverride override) {
  if (override.id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (marking_id_exists(override.id))
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.beam_override_ops.push_back(make_add_op(override));

  try {
    beam_overrides_.push_back(std::move(override));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::add_grace_group(GraceGroup group) {
  if (group.id == NotationEntityId{})
    return Result(ResultCode::kInvalidArgument);
  if (group.id == group.principal_event)
    return Result(ResultCode::kInvalidArgument);
  if (marking_id_exists(group.id))
    return Result(ResultCode::kInvalidArgument);
  const NotationEntityId parent_id = group.id;
  for (std::size_t i = 0; i < group.notes.size(); ++i) {
    const NotationEntityId gn_id = group.notes[i].id;
    if (gn_id == NotationEntityId{})
      return Result(ResultCode::kInvalidArgument);
    if (gn_id == parent_id)
      return Result(ResultCode::kInvalidArgument);
    if (gn_id == group.principal_event)
      return Result(ResultCode::kInvalidArgument);
    for (std::size_t j = 0; j < i; ++j) {
      if (gn_id == group.notes[j].id)
        return Result(ResultCode::kInvalidArgument);
    }
    if (marking_id_exists(gn_id))
      return Result(ResultCode::kInvalidArgument);
  }

  VoiceDelta d;
  d.grace_group_ops.push_back(make_add_op(group));

  try {
    grace_groups_.push_back(std::move(group));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::remove_dynamic(NotationEntityId id) {
  const auto it =
      std::find_if(dynamics_.begin(), dynamics_.end(),
                   [id](const DynamicMarking& m) { return m.id == id; });
  if (it == dynamics_.end())
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.dynamic_ops.push_back(make_remove_op<DynamicMarking>(id));

  try {
    dynamics_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::remove_hairpin(NotationEntityId id) {
  const auto it = std::find_if(hairpins_.begin(), hairpins_.end(),
                               [id](const Hairpin& m) { return m.id == id; });
  if (it == hairpins_.end())
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.hairpin_ops.push_back(make_remove_op<Hairpin>(id));

  try {
    hairpins_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::remove_slur(NotationEntityId id) {
  const auto it = std::find_if(slurs_.begin(), slurs_.end(),
                               [id](const Slur& m) { return m.id == id; });
  if (it == slurs_.end())
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.slur_ops.push_back(make_remove_op<Slur>(id));

  try {
    slurs_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::remove_beam_override(NotationEntityId id) {
  const auto it =
      std::find_if(beam_overrides_.begin(), beam_overrides_.end(),
                   [id](const BeamOverride& m) { return m.id == id; });
  if (it == beam_overrides_.end())
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.beam_override_ops.push_back(make_remove_op<BeamOverride>(id));

  try {
    beam_overrides_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

Result VoiceContent::remove_grace_group(NotationEntityId id) {
  const auto it =
      std::find_if(grace_groups_.begin(), grace_groups_.end(),
                   [id](const GraceGroup& m) { return m.id == id; });
  if (it == grace_groups_.end())
    return Result(ResultCode::kInvalidArgument);

  VoiceDelta d;
  d.grace_group_ops.push_back(make_remove_op<GraceGroup>(id));

  try {
    grace_groups_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }

  advance_revision(std::move(d));  // noexcept commit
  return Result();
}

namespace {

// Every plain (non-tuplet) Duration resolves to an exact multiple of
// 1/256 (double-dotted sixty-fourth = 7/256).  Scaling to integer 256ths
// lets us run an exact dynamic-programming decomposition without floating
// point or Rational-gcd churn.
constexpr std::int64_t kScale = 256;

std::vector<Duration> plain_rest_candidates() {
  constexpr std::array<NoteValue, 7> kBases = {
      NoteValue::kWhole,       NoteValue::kHalf,      NoteValue::kQuarter,
      NoteValue::kEighth,      NoteValue::kSixteenth, NoteValue::kThirtySecond,
      NoteValue::kSixtyFourth,
  };

  std::vector<Duration> candidates;
  candidates.reserve(kBases.size() * (Duration::kMaxDots + 1));
  for (const NoteValue base : kBases) {
    for (std::uint8_t dots = 0; dots <= Duration::kMaxDots; ++dots) {
      const std::optional<Duration> duration = Duration::create(base, dots);
      assert(duration.has_value());
      candidates.push_back(*duration);
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Duration& a, const Duration& b) {
              return a.resolved() > b.resolved();
            });
  return candidates;
}

struct DpEntry {
  int count      = -1;  // -1 = unreachable
  int first_step = -1;  // index into kCandidates (best first choice)
};

// The duration-only core of decompose_rest(): identical exact DP, but
// returns the decomposition as plain Duration values rather than minting a
// fresh Rest (and the Uuid::generate() that entails) per term. decompose_rest
// below wraps this to mint ids for callers that materialize real Rests;
// decompose_measure_aligned_rest_durations (voice_content.hpp) calls this
// directly per measure so a purely informational caller -- preview_note_entry
// (graphscore_notation), which discards every id immediately -- never mints
// one in the first place.
std::optional<std::vector<Duration>> decompose_rest_durations(Rational length) {
  if (length <= Rational(0))
    return std::nullopt;

  static const std::vector<Duration> kCandidates = plain_rest_candidates();
  constexpr int                      kMaxTerms   = 64;

  // The DP table is capped to a fixed size proportional to kMaxTerms so
  // the algorithm's memory and time are bounded regardless of Rational
  // magnitude.  The largest candidate is the double-dotted whole
  // (448/256), so kMaxTerms of those is the theoretical upper bound of
  // what the term limit permits.  Any input whose scaled value exceeds
  // this cap can never decompose within the term limit; we check the
  // bound before multiplication to rule out signed overflow.
  constexpr std::int64_t kDpCap = static_cast<std::int64_t>(kMaxTerms) * 448;

  // Scale to (1/256)-ths without multiplying until bounds are proven.
  // length is already reduced to lowest terms and positive, so its
  // denominator is guaranteed to be positive.  If the denominator does
  // not divide 256, length is not an exact multiple of 1/256 and no
  // decomposition exists.
  const std::int64_t den = length.denominator();
  if (kScale % den != 0)
    return std::nullopt;

  const std::int64_t multiplier = kScale / den;
  const std::int64_t numerator  = length.numerator();

  // Reject if numerator × multiplier would exceed kDpCap — this prevents
  // signed overflow while correctly rejecting values too large to ever
  // decompose within kMaxTerms.
  if (numerator > kDpCap / multiplier)
    return std::nullopt;

  const std::int64_t target = numerator * multiplier;  // safe: ≤ kDpCap
  if (target <= 0)
    return std::nullopt;

  // Precompute candidate values in integer 256ths.
  std::vector<std::int64_t> cand_vals;
  cand_vals.reserve(kCandidates.size());
  for (const Duration& c : kCandidates) {
    const Rational r = c.resolved() * Rational(kScale);
    cand_vals.push_back(r.numerator());
  }

  // --- Exact DP (backward formulation) ---
  // dp[i] records the minimal rest count and the index of the first
  // candidate to apply.  Among equal-count decompositions the smaller
  // candidate index (larger Duration) wins, which yields the deterministic
  // largest-duration-first tiebreak the public contract promises.
  std::vector<DpEntry> dp(static_cast<std::size_t>(target) + 1);
  dp[0] = {0, -1};

  for (std::int64_t i = 0; i < target; ++i) {
    const DpEntry& cur = dp[static_cast<std::size_t>(i)];
    if (cur.count < 0)
      continue;
    const int next_count = cur.count + 1;
    for (int j = 0; j < static_cast<int>(cand_vals.size()); ++j) {
      const std::int64_t nxt = i + cand_vals[static_cast<std::size_t>(j)];
      if (nxt > target)
        continue;
      DpEntry& dest = dp[static_cast<std::size_t>(nxt)];
      if (dest.count < 0 || next_count < dest.count ||
          (next_count == dest.count && j < dest.first_step)) {
        dest.count      = next_count;
        dest.first_step = j;
      }
    }
  }

  const DpEntry& end = dp[static_cast<std::size_t>(target)];
  if (end.count < 0 || end.count > kMaxTerms)
    return std::nullopt;

  // Reconstruct from target down to 0.  dp[i].first_step is the
  // *first* candidate applied when reaching i, so following the chain
  // yields terms in application order (largest first thanks to the
  // tiebreak above).
  std::vector<Duration> durations;
  durations.reserve(static_cast<std::size_t>(end.count));
  std::int64_t pos = target;
  while (pos > 0) {
    const int j = dp[static_cast<std::size_t>(pos)].first_step;
    durations.push_back(kCandidates[static_cast<std::size_t>(j)]);
    pos -= cand_vals[static_cast<std::size_t>(j)];
  }

  return durations;
}

}  // namespace

std::optional<std::vector<Rest>> decompose_rest(Rational length) {
  const std::optional<std::vector<Duration>> durations =
      decompose_rest_durations(length);
  if (!durations.has_value())
    return std::nullopt;

  std::vector<Rest> rests;
  rests.reserve(durations->size());
  for (const Duration& duration : *durations)
    rests.push_back(make_rest(duration));
  return rests;
}

std::optional<std::vector<Duration>> decompose_measure_aligned_rest_durations(
    const NodeTimeline& timeline) {
  const MeasureMap&     measures = timeline.measures();
  std::vector<Duration> result;

  for (std::size_t index = 0; index < measures.measure_count(); ++index) {
    const std::optional<std::vector<Duration>> piece =
        decompose_rest_durations(measures.measure_length(index));
    if (!piece.has_value())
      return std::nullopt;
    result.insert(result.end(), piece->begin(), piece->end());
  }

  if (const std::optional<Rational> pickdown = timeline.pickdown_duration();
      pickdown.has_value()) {
    const std::optional<std::vector<Duration>> piece =
        decompose_rest_durations(*pickdown);
    if (!piece.has_value())
      return std::nullopt;
    result.insert(result.end(), piece->begin(), piece->end());
  }

  return result;
}

std::optional<std::vector<Rest>> decompose_measure_aligned_rests(
    const NodeTimeline& timeline) {
  const std::optional<std::vector<Duration>> durations =
      decompose_measure_aligned_rest_durations(timeline);
  if (!durations.has_value())
    return std::nullopt;

  std::vector<Rest> result;
  result.reserve(durations->size());
  for (const Duration& duration : *durations)
    result.push_back(make_rest(duration));
  return result;
}

void VoiceContent::remap_event_id_across_references(NotationEntityId old_id,
                                                    NotationEntityId new_id) {
  if (old_id == new_id)
    return;

  for (DynamicMarking& d : dynamics_) {
    if (d.at_event == old_id)
      d.at_event = new_id;
  }
  for (Hairpin& h : hairpins_) {
    if (h.start_event == old_id)
      h.start_event = new_id;
    if (h.end_event == old_id)
      h.end_event = new_id;
  }
  for (Slur& s : slurs_) {
    if (s.start_event == old_id)
      s.start_event = new_id;
    if (s.end_event == old_id)
      s.end_event = new_id;
  }
  for (BeamOverride& b : beam_overrides_) {
    for (NotationEntityId& eid : b.events) {
      if (eid == old_id)
        eid = new_id;
    }
  }
  for (GraceGroup& g : grace_groups_) {
    if (g.principal_event == old_id)
      g.principal_event = new_id;
  }
}

}  // namespace graphscore
