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
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

[[nodiscard]] bool event_id_exists(const std::vector<VoiceEvent>& events,
                                   NotationEntityId               id) {
  for (const VoiceEvent& event : events) {
    if (event_id(event) == id)
      return true;
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

Result VoiceContent::append(VoiceEvent event) {
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (chord->notes.size() < 2)
      return Result(ResultCode::kInvalidArgument);
  }

  // Reject duplicate NotationEntityId: the new event's id must not
  // collide with any event or any marking (dynamic, hairpin, slur,
  // beam override, grace group) already present in the voice.
  const NotationEntityId new_id = event_id(event);
  if (marking_id_exists(new_id))
    return Result(ResultCode::kInvalidArgument);

  try {
    events_.push_back(std::move(event));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

std::optional<std::size_t> VoiceContent::find_event_index_at(
    Rational position) const {
  const std::optional<std::size_t> index = find_index_at(events_, position);
  if (!index.has_value() || *index >= events_.size())
    return std::nullopt;
  return index;
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

  // Reject duplicate NotationEntityId: the new event's id must not
  // collide with any other event id or any marking id already present
  // in the voice.
  const NotationEntityId new_id = event_id(event);
  if (marking_id_exists(new_id))
    return Result(ResultCode::kInvalidArgument);

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

    events_.swap(temp);
    return Result();
  }

  // Insertion at the start of an existing event: must consume contiguous
  // Rest coverage beginning at `position`.
  if (!std::holds_alternative<Rest>(events_[*index]))
    return Result(ResultCode::kInvalidArgument);

  // Scan forward consuming rests until we have covered new_dur.
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
      // Split the final Rest: consume part, leave remainder.
      split_remainder_amount = (consumed + rest_dur) - new_dur;
      split_remainder        = true;
      split_rest_id          = event_id(events_[i]);
      consumed               = new_dur;
      ++consume_end;  // this rest is fully consumed, remainder will be spliced
      break;
    }
  }

  if (consumed < new_dur)
    return Result(ResultCode::kInvalidArgument);

  // Build the new events vector:
  //   [0..consume_start) + [new event] + [remainder rests] + [consume_end..)
  std::vector<VoiceEvent> temp;
  try {
    temp.reserve(events_.size() + 4);

    // Before insertion boundary.
    temp.insert(temp.end(), events_.begin(),
                events_.begin() + static_cast<std::ptrdiff_t>(consume_start));

    // The new event.
    temp.push_back(event);

    // Remaining portion of the split rest, if any, preserving its original id.
    if (split_remainder && split_remainder_amount > Rational(0)) {
      DecomposeVoiceResult remainder =
          decompose_rest_with_id(split_remainder_amount, split_rest_id);
      if (!remainder.result.ok())
        return remainder.result;
      temp.insert(temp.end(), remainder.events.begin(), remainder.events.end());
    }

    // Everything after the consumed region.
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

  events_.swap(temp);
  return Result();
}

Result VoiceContent::remove_event(Rational position, Rational target_length) {
  const std::optional<std::size_t> index = find_index_at(events_, position);
  if (!index.has_value())
    return Result(ResultCode::kInvalidArgument);
  if (*index >= events_.size())
    return Result(ResultCode::kInvalidArgument);

  const Rational old_dur = event_duration(events_[*index]).resolved();

  // Replace the removed event with normalized rests of the same duration
  // at the same position, preserving every later event's onset.
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

  events_.swap(temp);
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

  // Reject duplicate NotationEntityId:
  //   - The replacement may reuse the target event's own id for the
  //     event collection, but must never collide with any marking
  //     (dynamic, hairpin, slur, beam override, grace group).
  //   - If the new id differs from the target's, it must additionally
  //     not collide with any other event.
  const NotationEntityId new_id    = event_id(event);
  const NotationEntityId target_id = event_id(events_[*index]);
  if (marking_only_id_exists(new_id))
    return Result(ResultCode::kInvalidArgument);
  if (new_id != target_id && event_id_exists(events_, new_id))
    return Result(ResultCode::kInvalidArgument);

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

  // Duration contraction: insert normalized rests for the gap immediately
  // after the replacement so every later event's onset is preserved.
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

    events_.swap(temp);
    return Result();
  }

  // Same-length replacement: direct substitution, no positional adjustment
  // needed.  Later events' onsets are unchanged.
  if (new_dur == old_dur) {
    try {
      temp[*index] = std::move(event);
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    }

    const Result norm_result = normalize_temp(temp, target_length);
    if (!norm_result.ok())
      return norm_result;

    events_.swap(temp);
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
      // Shorten the final consumed rest.  Preserve the original Rest
      // NotationEntityId on the surviving remainder.
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
        // The split code has already removed the consumed rest and
        // spliced in the remainder; don't erase those remainder rests.
        erase_end = erase_start;
        break;
      }
    }
  }

  if (consumed < needed)
    return Result(ResultCode::kInvalidArgument);

  // Erase consumed rests, insert the replacement, normalize.
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

  events_.swap(temp);
  return Result();
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

  // Build a complete temp vector; swap only on success so failure leaves
  // events_ unchanged.
  std::vector<VoiceEvent> temp;
  try {
    temp.reserve(events_.size() + rests->size());
    temp = events_;
    for (const Rest& rest : *rests)
      temp.push_back(rest);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  events_.swap(temp);  // nonthrowing
  return Result();
}

Result VoiceContent::validate() const {
  return validate_ties(events_);
}

bool VoiceContent::marking_id_exists(NotationEntityId id) const {
  for (const VoiceEvent& event : events_) {
    if (event_id(event) == id)
      return true;
  }
  for (const DynamicMarking& m : dynamics_) {
    if (m.id == id)
      return true;
  }
  for (const Hairpin& m : hairpins_) {
    if (m.id == id)
      return true;
  }
  for (const Slur& m : slurs_) {
    if (m.id == id)
      return true;
  }
  for (const BeamOverride& m : beam_overrides_) {
    if (m.id == id)
      return true;
  }
  for (const GraceGroup& m : grace_groups_) {
    if (m.id == id)
      return true;
  }
  return false;
}

bool VoiceContent::marking_only_id_exists(NotationEntityId id) const {
  for (const DynamicMarking& m : dynamics_) {
    if (m.id == id)
      return true;
  }
  for (const Hairpin& m : hairpins_) {
    if (m.id == id)
      return true;
  }
  for (const Slur& m : slurs_) {
    if (m.id == id)
      return true;
  }
  for (const BeamOverride& m : beam_overrides_) {
    if (m.id == id)
      return true;
  }
  for (const GraceGroup& m : grace_groups_) {
    if (m.id == id)
      return true;
  }
  return false;
}

Result VoiceContent::add_dynamic(DynamicMarking marking) {
  if (marking_id_exists(marking.id))
    return Result(ResultCode::kInvalidArgument);
  try {
    dynamics_.push_back(marking);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::add_hairpin(Hairpin hairpin) {
  if (marking_id_exists(hairpin.id))
    return Result(ResultCode::kInvalidArgument);
  try {
    hairpins_.push_back(hairpin);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::add_slur(Slur slur) {
  if (marking_id_exists(slur.id))
    return Result(ResultCode::kInvalidArgument);
  try {
    slurs_.push_back(slur);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::add_beam_override(BeamOverride override) {
  if (marking_id_exists(override.id))
    return Result(ResultCode::kInvalidArgument);
  try {
    beam_overrides_.push_back(std::move(override));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::add_grace_group(GraceGroup group) {
  if (marking_id_exists(group.id))
    return Result(ResultCode::kInvalidArgument);
  try {
    grace_groups_.push_back(std::move(group));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::remove_dynamic(NotationEntityId id) {
  const auto it =
      std::find_if(dynamics_.begin(), dynamics_.end(),
                   [id](const DynamicMarking& m) { return m.id == id; });
  if (it == dynamics_.end())
    return Result(ResultCode::kInvalidArgument);
  try {
    dynamics_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::remove_hairpin(NotationEntityId id) {
  const auto it = std::find_if(hairpins_.begin(), hairpins_.end(),
                               [id](const Hairpin& m) { return m.id == id; });
  if (it == hairpins_.end())
    return Result(ResultCode::kInvalidArgument);
  try {
    hairpins_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::remove_slur(NotationEntityId id) {
  const auto it = std::find_if(slurs_.begin(), slurs_.end(),
                               [id](const Slur& m) { return m.id == id; });
  if (it == slurs_.end())
    return Result(ResultCode::kInvalidArgument);
  try {
    slurs_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::remove_beam_override(NotationEntityId id) {
  const auto it =
      std::find_if(beam_overrides_.begin(), beam_overrides_.end(),
                   [id](const BeamOverride& m) { return m.id == id; });
  if (it == beam_overrides_.end())
    return Result(ResultCode::kInvalidArgument);
  try {
    beam_overrides_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result VoiceContent::remove_grace_group(NotationEntityId id) {
  const auto it =
      std::find_if(grace_groups_.begin(), grace_groups_.end(),
                   [id](const GraceGroup& m) { return m.id == id; });
  if (it == grace_groups_.end())
    return Result(ResultCode::kInvalidArgument);
  try {
    grace_groups_.erase(it);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

namespace {

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

}  // namespace

std::optional<std::vector<Rest>> decompose_rest(Rational length) {
  if (length <= Rational(0))
    return std::nullopt;

  static const std::vector<Duration> kCandidates = plain_rest_candidates();
  constexpr int                      kMaxTerms   = 64;

  std::vector<Rest> rests;
  Rational          remaining = length;
  for (int term = 0; remaining > Rational(0); ++term) {
    if (term >= kMaxTerms)
      return std::nullopt;

    const Duration* chosen = nullptr;
    for (const Duration& candidate : kCandidates) {
      if (candidate.resolved() <= remaining) {
        chosen = &candidate;
        break;
      }
    }
    if (chosen == nullptr)
      return std::nullopt;

    rests.push_back(make_rest(*chosen));
    remaining = remaining - chosen->resolved();
  }

  return rests;
}

}  // namespace graphscore
