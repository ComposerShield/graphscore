// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <type_traits>
#include <unordered_set>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::ConnectorId;
using graphscore::ConnectorIndex;
using graphscore::NodeId;
using graphscore::NodeIndex;
using graphscore::ProjectId;
using graphscore::StaveId;
using graphscore::TrackId;
using graphscore::TrackIndex;
using graphscore::Uuid;

static_assert(!std::is_same_v<ProjectId, TrackId>);
static_assert(!std::is_same_v<TrackId, StaveId>);

TEST(StrongIdTest, DefaultsAreEqual) {
  EXPECT_EQ(TrackId(), TrackId());
}

TEST(StrongIdTest, GenerateProducesDistinctIds) {
  EXPECT_NE(TrackId::generate(), TrackId::generate());
}

TEST(StrongIdTest, WrapsSameUuidEqually) {
  const Uuid    raw = Uuid::generate();
  const TrackId a(raw);
  const TrackId b(raw);
  EXPECT_EQ(a, b);
}

TEST(StrongIdTest, ToStringMatchesUnderlyingUuid) {
  const Uuid    raw = Uuid::generate();
  const TrackId id(raw);
  EXPECT_EQ(id.to_string(), raw.to_string());
}

TEST(StrongIdTest, HashableInUnorderedSet) {
  std::unordered_set<TrackId> ids;
  ids.insert(TrackId::generate());
  ids.insert(TrackId::generate());
  EXPECT_EQ(ids.size(), 2u);
}

TEST(StrongIdTest, DistinctEntityKindsAreNotInterconvertible) {
  static_assert(!std::is_convertible_v<TrackId, NodeId>);
  static_assert(!std::is_constructible_v<TrackId, NodeId>);
  static_assert(!std::is_same_v<ConnectorId, ProjectId>);
  SUCCEED();
}

TEST(StrongIndexTest, DefaultIsZero) {
  EXPECT_EQ(TrackIndex().value(), 0u);
}

TEST(StrongIndexTest, ValueRoundTrips) {
  const TrackIndex index(7);
  EXPECT_EQ(index.value(), 7u);
}

TEST(StrongIndexTest, OrdersByValue) {
  EXPECT_LT(TrackIndex(1), TrackIndex(2));
}

TEST(StrongIndexTest, DistinctEntityKindsAreNotInterconvertible) {
  static_assert(!std::is_same_v<TrackIndex, NodeIndex>);
  static_assert(!std::is_convertible_v<TrackIndex, ConnectorIndex>);
  SUCCEED();
}

TEST(StrongIndexTest, IndexAndIdAreDistinctTypes) {
  static_assert(!std::is_same_v<TrackId, TrackIndex>);
  static_assert(!std::is_convertible_v<TrackId, TrackIndex>);
  static_assert(!std::is_convertible_v<TrackIndex, TrackId>);
  static_assert(!std::is_constructible_v<TrackIndex, TrackId>);
  static_assert(!std::is_constructible_v<TrackId, TrackIndex>);
  SUCCEED();
}
