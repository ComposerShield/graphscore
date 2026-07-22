// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::EventId;
using graphscore::EventRegistry;

TEST(EventRegistryTest, AddEventSucceeds) {
  EventRegistry registry;
  const auto    id = registry.add_event("Attack");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(registry.size(), 1u);
}

TEST(EventRegistryTest, DuplicateNameIsRejectedCaseSensitively) {
  EventRegistry registry;
  ASSERT_TRUE(registry.add_event("Attack").has_value());
  EXPECT_FALSE(registry.add_event("Attack").has_value());
  EXPECT_TRUE(registry.add_event("attack").has_value());
  EXPECT_EQ(registry.size(), 2u);
}

TEST(EventRegistryTest, LookupByIdAndName) {
  EventRegistry registry;
  const auto    id = registry.add_event("Release");
  ASSERT_TRUE(id.has_value());

  const auto* by_id = registry.find_by_id(*id);
  ASSERT_NE(by_id, nullptr);
  EXPECT_EQ(by_id->name, "Release");

  const auto* by_name = registry.find_by_name("Release");
  ASSERT_NE(by_name, nullptr);
  EXPECT_EQ(by_name->id, *id);

  EXPECT_EQ(registry.find_by_name("release"), nullptr);
}

TEST(EventRegistryTest, RenameSucceedsToUnusedName) {
  EventRegistry registry;
  const auto    id = registry.add_event("Legato");
  ASSERT_TRUE(id.has_value());

  EXPECT_TRUE(registry.rename_event(*id, "Sustain").ok());
  EXPECT_EQ(registry.find_by_name("Legato"), nullptr);
  const auto* renamed = registry.find_by_name("Sustain");
  ASSERT_NE(renamed, nullptr);
  EXPECT_EQ(renamed->id, *id);
}

TEST(EventRegistryTest, RenameRejectsCollision) {
  EventRegistry registry;
  const auto    first  = registry.add_event("A");
  const auto    second = registry.add_event("B");
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  EXPECT_FALSE(registry.rename_event(*second, "A").ok());
  ASSERT_NE(registry.find_by_name("B"), nullptr);
  EXPECT_EQ(registry.find_by_name("B")->id, *second);
}

TEST(EventRegistryTest, RenameUnknownIdFails) {
  EventRegistry registry;
  EXPECT_FALSE(registry.rename_event(EventId::generate(), "X").ok());
}

TEST(EventRegistryTest, RemoveEventSucceeds) {
  EventRegistry registry;
  const auto    id = registry.add_event("Temp");
  ASSERT_TRUE(id.has_value());

  EXPECT_TRUE(registry.remove_event(*id).ok());
  EXPECT_EQ(registry.find_by_id(*id), nullptr);
  EXPECT_EQ(registry.size(), 0u);
}

TEST(EventRegistryTest, RemoveUnknownIdFails) {
  EventRegistry registry;
  EXPECT_FALSE(registry.remove_event(EventId::generate()).ok());
}

TEST(EventRegistryTest, RemovedNameCanBeReused) {
  EventRegistry registry;
  const auto    id = registry.add_event("Reuse");
  ASSERT_TRUE(id.has_value());
  ASSERT_TRUE(registry.remove_event(*id).ok());
  EXPECT_TRUE(registry.add_event("Reuse").has_value());
}
