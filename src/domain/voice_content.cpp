// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/voice_content.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace graphscore {

Result VoiceContent::append(VoiceEvent event) {
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (chord->notes.size() < 2)
      return Result(ResultCode::kInvalidArgument);
  }
  events_.push_back(std::move(event));
  return Result();
}

Rational VoiceContent::total_length() const {
  Rational total(0);
  for (const VoiceEvent& event : events_)
    total = total + event_duration(event).resolved();
  return total;
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

  const std::optional<std::vector<Rest>> rests =
      decompose_rest(target_length - current);
  if (!rests.has_value())
    return Result(ResultCode::kInvalidArgument);

  for (const Rest& rest : *rests)
    events_.push_back(rest);
  return Result();
}

Result VoiceContent::validate() const {
  return validate_ties(events_);
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
      // The loop bound keeps dots <= kMaxDots, create's only failure mode.
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
