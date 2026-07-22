// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::EventId;
using graphscore::EventOccurrence;
using graphscore::EventQueue;
using graphscore::QueuePolicy;

namespace {

EventOccurrence make_occurrence(EventId event, std::uint64_t ordinal,
                                std::int64_t sample_offset = 0) {
  const auto occurrence =
      EventOccurrence::create(event, ordinal, sample_offset);
  return *occurrence;
}

}  // namespace

TEST(EventOccurrenceTest, CreateRejectsNegativeSampleOffset) {
  EXPECT_FALSE(EventOccurrence::create(EventId::generate(), 0, -1).has_value());
}

TEST(EventOccurrenceTest, CreateSucceedsAndStoresFields) {
  const EventId event      = EventId::generate();
  const auto    occurrence = EventOccurrence::create(event, 7, 42);
  ASSERT_TRUE(occurrence.has_value());
  EXPECT_EQ(occurrence->event(), event);
  EXPECT_EQ(occurrence->arrival_ordinal(), 7u);
  EXPECT_EQ(occurrence->sample_offset(), 42);
}

TEST(EventQueueTest, EmptyQueueHasNoPendingOccurrence) {
  EventQueue queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0u);
  EXPECT_FALSE(queue.peek().has_value());
}

TEST(EventQueueTest, FifoFillsToCapacityWithoutDropping) {
  EventQueue queue;
  const auto event = EventId::generate();

  queue.record(QueuePolicy::kFifo, make_occurrence(event, 0), 3);
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 1), 3);
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 2), 3);

  EXPECT_EQ(queue.size(), 3u);
  EXPECT_EQ(queue.dropped_count(), 0u);
  ASSERT_TRUE(queue.peek().has_value());
  EXPECT_EQ(queue.peek()->arrival_ordinal(), 0u);
}

TEST(EventQueueTest, FullFifoDropsOldestAndIncrementsDiagnosticCounter) {
  EventQueue queue;
  const auto event = EventId::generate();

  queue.record(QueuePolicy::kFifo, make_occurrence(event, 0), 2);
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 1), 2);
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 2), 2);

  EXPECT_EQ(queue.size(), 2u);
  EXPECT_EQ(queue.dropped_count(), 1u);
  ASSERT_TRUE(queue.peek().has_value());
  EXPECT_EQ(queue.peek()->arrival_ordinal(), 1u);

  queue.record(QueuePolicy::kFifo, make_occurrence(event, 3), 2);
  EXPECT_EQ(queue.dropped_count(), 2u);
}

TEST(EventQueueTest, DroppedCountIsPollableAndResettable) {
  EventQueue queue;
  const auto event = EventId::generate();
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 0), 1);
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 1), 1);
  ASSERT_EQ(queue.dropped_count(), 1u);

  queue.reset_dropped_count();
  EXPECT_EQ(queue.dropped_count(), 0u);
  // Resetting the counter does not disturb pending occurrences.
  ASSERT_TRUE(queue.peek().has_value());
  EXPECT_EQ(queue.peek()->arrival_ordinal(), 1u);
}

TEST(EventQueueTest, FifoConsumeRemovesOnlyTheEarliestLeavingRestQueued) {
  EventQueue queue;
  const auto event = EventId::generate();
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 0), 3);
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 1), 3);

  queue.consume();

  EXPECT_EQ(queue.size(), 1u);
  ASSERT_TRUE(queue.peek().has_value());
  EXPECT_EQ(queue.peek()->arrival_ordinal(), 1u);
}

TEST(EventQueueTest, FirstWinsKeepsEarliestAndDiscardsLaterDuplicates) {
  EventQueue queue;
  const auto event = EventId::generate();

  queue.record(QueuePolicy::kFirstWins, make_occurrence(event, 0), 1);
  queue.record(QueuePolicy::kFirstWins, make_occurrence(event, 1), 1);
  queue.record(QueuePolicy::kFirstWins, make_occurrence(event, 2), 1);

  EXPECT_EQ(queue.size(), 1u);
  EXPECT_EQ(queue.dropped_count(), 0u);
  ASSERT_TRUE(queue.peek().has_value());
  EXPECT_EQ(queue.peek()->arrival_ordinal(), 0u);

  queue.consume();
  EXPECT_TRUE(queue.empty());
}

TEST(EventQueueTest, LatestValidWinsKeepsNewestAndDiscardsOlderDuplicates) {
  EventQueue queue;
  const auto event = EventId::generate();

  queue.record(QueuePolicy::kLatestValidWins, make_occurrence(event, 0), 1);
  queue.record(QueuePolicy::kLatestValidWins, make_occurrence(event, 1), 1);
  queue.record(QueuePolicy::kLatestValidWins, make_occurrence(event, 2), 1);

  EXPECT_EQ(queue.size(), 1u);
  ASSERT_TRUE(queue.peek().has_value());
  EXPECT_EQ(queue.peek()->arrival_ordinal(), 2u);

  queue.consume();
  EXPECT_TRUE(queue.empty());
}

TEST(EventQueueTest, ClearDiscardsPendingOccurrencesButNotDroppedCount) {
  EventQueue queue;
  const auto event = EventId::generate();
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 0), 1);
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 1), 1);
  ASSERT_EQ(queue.dropped_count(), 1u);

  queue.clear();

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.dropped_count(), 1u);
}

TEST(EventQueueTest, ZeroCapacityFifoDropsArrivalsImmediately) {
  EventQueue queue;
  const auto event = EventId::generate();
  queue.record(QueuePolicy::kFifo, make_occurrence(event, 0), 0);

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.dropped_count(), 1u);
}
