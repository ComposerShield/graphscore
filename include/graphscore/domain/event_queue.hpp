// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/event_occurrence.hpp>

namespace graphscore {

// The bounded, persistent runtime storage backing one (node, event) pair's
// EventListener (event_listener.hpp). EventStateMachine
// (event_state_machine.hpp) owns exactly one EventQueue per (node, event)
// pair and drives it across a node boundary; this class is storage and
// per-arrival-policy bookkeeping only.
//
// EventQueue holds no policy state of its own -- every call is told the
// *current* QueuePolicy explicitly, since a listener's configured policy
// can change over its life (Node::set_listener_policy) and arrivals always
// honor whatever policy is current at the moment they are recorded:
//
//  - kFifo: bounded to `fifo_capacity` pending occurrences. An arrival
//    while already full drops the oldest pending occurrence and increments
//    dropped_count() (the diagnostic counter product decisions call for on
//    "a full FIFO"), then appends the new one. consume() removes only the
//    single oldest pending occurrence ("FIFO consumes only the earliest"),
//    leaving the rest queued for a later boundary.
//  - kFirstWins: bounded to a single pending occurrence, always the
//    earliest not yet consumed. A later arrival while one is already
//    pending is discarded outright and does not affect dropped_count() --
//    that counter is specifically the kFifo overflow diagnostic, and a
//    first-wins duplicate is normal interpretation, not overflow.
//    consume() therefore always empties the queue.
//  - kLatestValidWins (default): bounded to a single pending occurrence,
//    always the newest. Every arrival unconditionally replaces whatever
//    was previously pending. consume() therefore always empties the
//    queue.
//
// Capacity is bounded to a single slot for kFirstWins/kLatestValidWins
// regardless of the caller-supplied `fifo_capacity` argument, matching
// EventListener::capacity()'s own documentation that the stored capacity
// is meaningful only for kFifo.
//
// Every occurrence lives in a std::vector, bounded to at most `fifo_capacity`
// (or a single slot for kFirstWins/kLatestValidWins) -- record()/consume()
// never let it grow past that bound. This is a bounded FIFO whose capacity
// invariant a realtime implementation would satisfy with a preallocated
// fixed-size ring buffer, mapping record/peek/consume/clear directly onto
// it with no allocation, locking, or unbounded work on the audio thread
// (see AGENTS.md's realtime rules); the std::vector here does not itself
// have that O(1)/no-allocation property (erase() shifts, and nothing is
// reserve()d). This type is the toolkit-independent specification of the
// capacity/policy contract, not the realtime implementation itself.
class EventQueue {
 public:
  EventQueue() = default;

  void record(QueuePolicy policy, EventOccurrence occurrence,
              std::size_t fifo_capacity);

  // The occurrence a boundary would consume right now: the sole pending
  // occurrence for kFirstWins/kLatestValidWins, or the oldest still-queued
  // occurrence for kFifo. nullopt if nothing is pending.
  [[nodiscard]] std::optional<EventOccurrence> peek() const;

  // Removes exactly the earliest pending occurrence; see the policy
  // breakdown above for what that leaves behind.
  void consume();

  // Discards every pending occurrence unconditionally, independent of
  // policy. EventStateMachine calls this for stop/reset/node-play
  // (event_state_machine.hpp) -- never for pause, which must leave
  // pending occurrences untouched.
  void clear() noexcept;

  [[nodiscard]] std::size_t size() const noexcept {
    return occurrences_.size();
  }

  [[nodiscard]] bool empty() const noexcept { return occurrences_.empty(); }

  // Diagnostic count of kFifo overflow drops only. Pollable and
  // independently resettable of clear(): stop/reset/node-play do not
  // implicitly reset it, since it tracks session-lifetime overflow
  // history rather than pending playback state.
  [[nodiscard]] std::uint64_t dropped_count() const noexcept {
    return dropped_count_;
  }

  void reset_dropped_count() noexcept { dropped_count_ = 0; }

  [[nodiscard]] bool operator==(const EventQueue&) const = default;

 private:
  std::vector<EventOccurrence> occurrences_;
  std::uint64_t                dropped_count_ = 0;
};

}  // namespace graphscore
