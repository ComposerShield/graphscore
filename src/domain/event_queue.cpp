// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/event_queue.hpp>

#include <cstddef>

namespace graphscore {

void EventQueue::record(QueuePolicy policy, EventOccurrence occurrence,
                        std::size_t fifo_capacity) {
  if (policy == QueuePolicy::kFirstWins) {
    if (occurrences_.empty())
      occurrences_.push_back(occurrence);
    return;
  }

  if (policy == QueuePolicy::kLatestValidWins) {
    occurrences_.clear();
    occurrences_.push_back(occurrence);
    return;
  }

  // kFifo.
  if (fifo_capacity == 0) {
    ++dropped_count_;
    return;
  }
  if (occurrences_.size() >= fifo_capacity) {
    occurrences_.erase(occurrences_.begin());
    ++dropped_count_;
  }
  occurrences_.push_back(occurrence);
}

std::optional<EventOccurrence> EventQueue::peek() const {
  if (occurrences_.empty())
    return std::nullopt;
  return occurrences_.front();
}

void EventQueue::consume() {
  if (!occurrences_.empty())
    occurrences_.erase(occurrences_.begin());
}

void EventQueue::clear() noexcept {
  occurrences_.clear();
}

}  // namespace graphscore
