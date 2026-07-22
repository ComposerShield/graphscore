// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// One recorded instance of an event firing: either an occurrence about to
// enter an EventQueue for a kSequential listener (event_queue.hpp), or a
// transient candidate passed directly to vertical-match arbitration
// (event_state_machine.hpp), which never persists it.
//
// `arrival_ordinal` is assigned by EventStateMachine from a single,
// project-wide monotonic counter at the exact moment the occurrence is
// recorded, so "earliest" and "newest" comparisons between two occurrences
// are exact and total -- including when two occurrences share the same
// sample_offset -- without relying on wall-clock time or container
// iteration order (see docs/plan/M02_HANDOFF.md's note on nondeterministic
// unordered-map iteration; this design avoids that class of bug entirely).
//
// `sample_offset` is the runtime sample position, within the node whose
// boundary or vertical jump this occurrence relates to, at which the
// underlying event actually fired. It is carried for diagnostic and
// ownership purposes; it plays no role in first/latest/FIFO interpretation
// or in cross-event/simultaneous-vertical arbitration, both of which are
// governed entirely by arrival_ordinal and connector priority/order (see
// event_state_machine.hpp). Negative sample offsets are not meaningful and
// are rejected by create().
class EventOccurrence {
 public:
  [[nodiscard]] static std::optional<EventOccurrence> create(
      EventId event, std::uint64_t arrival_ordinal,
      std::int64_t sample_offset) {
    if (sample_offset < 0)
      return std::nullopt;
    return EventOccurrence(event, arrival_ordinal, sample_offset);
  }

  [[nodiscard]] EventId event() const noexcept { return event_; }

  [[nodiscard]] std::uint64_t arrival_ordinal() const noexcept {
    return arrival_ordinal_;
  }

  [[nodiscard]] std::int64_t sample_offset() const noexcept {
    return sample_offset_;
  }

  [[nodiscard]] bool operator==(const EventOccurrence&) const = default;

 private:
  EventOccurrence(EventId event, std::uint64_t arrival_ordinal,
                  std::int64_t sample_offset) noexcept
      : event_(event),
        arrival_ordinal_(arrival_ordinal),
        sample_offset_(sample_offset) {}

  EventId       event_;
  std::uint64_t arrival_ordinal_;
  std::int64_t  sample_offset_;
};

}  // namespace graphscore
