// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/persistence/graphscore_persistence.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace {

[[nodiscard]] graphscore::NodeId node_id(std::uint8_t suffix) {
  std::array<std::uint8_t, graphscore::Uuid::kSize> bytes{};
  bytes.back() = suffix;
  return graphscore::NodeId(graphscore::Uuid(bytes));
}

TEST(PersistenceTest, CapturesCustomColorsAndFreeformNotesInProjectOrder) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Metadata"};
  const graphscore::NodeId first  = node_id(1U);
  const graphscore::NodeId second = node_id(2U);
  ASSERT_TRUE(project.add_node_with_id(first, "First").ok());
  ASSERT_TRUE(project.add_node_with_id(second, "Second").ok());
  project.find_node(first)->set_color(0x12345678U);
  project.find_node(first)->set_notes("Cue \xF0\x9F\x8E\xB5\nthen fade");
  project.find_node(second)->set_color(0x00FF80A5U);
  project.find_node(second)->set_notes(std::string{"embedded\0null", 13U});

  const graphscore::ProjectNodeMetadataSaveModel saved =
      graphscore::Persistence{}.capture_node_metadata(project);

  ASSERT_EQ(saved.nodes.size(), 2U);
  EXPECT_EQ(saved.nodes[0],
            (graphscore::PersistedNodeMetadata{
                first, 0x12345678U, "Cue \xF0\x9F\x8E\xB5\nthen fade"}));
  EXPECT_EQ(saved.nodes[1],
            (graphscore::PersistedNodeMetadata{
                second, 0x00FF80A5U, std::string{"embedded\0null", 13U}}));
}

TEST(PersistenceTest, RestoresMetadataByStableNodeIdentity) {
  graphscore::Project      source{graphscore::ProjectId::generate(), "Source"};
  const graphscore::NodeId first  = node_id(1U);
  const graphscore::NodeId second = node_id(2U);
  ASSERT_TRUE(source.add_node_with_id(first, "First").ok());
  ASSERT_TRUE(source.add_node_with_id(second, "Second").ok());
  source.find_node(first)->set_color(0x01020304U);
  source.find_node(first)->set_notes("first notes");
  source.find_node(second)->set_color(0xA0B0C0D0U);
  source.find_node(second)->set_notes("second notes");
  const graphscore::ProjectNodeMetadataSaveModel saved =
      graphscore::Persistence{}.capture_node_metadata(source);

  graphscore::Project destination{graphscore::ProjectId::generate(),
                                  "Destination"};
  ASSERT_TRUE(destination.add_node_with_id(second, "Second").ok());
  ASSERT_TRUE(destination.add_node_with_id(first, "First").ok());

  const graphscore::Result result =
      graphscore::Persistence{}.restore_node_metadata(saved, destination);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(destination.find_node(first)->color(), 0x01020304U);
  EXPECT_EQ(destination.find_node(first)->notes(), "first notes");
  EXPECT_EQ(destination.find_node(second)->color(), 0xA0B0C0D0U);
  EXPECT_EQ(destination.find_node(second)->notes(), "second notes");
}

TEST(PersistenceTest, RejectsIncompleteOrDuplicateMetadataAtomically) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Destination"};
  const graphscore::NodeId first  = node_id(1U);
  const graphscore::NodeId second = node_id(2U);
  ASSERT_TRUE(project.add_node_with_id(first, "First").ok());
  ASSERT_TRUE(project.add_node_with_id(second, "Second").ok());
  project.find_node(first)->set_color(0x11111111U);
  project.find_node(first)->set_notes("unchanged first");
  project.find_node(second)->set_color(0x22222222U);
  project.find_node(second)->set_notes("unchanged second");
  const graphscore::ProjectNodeMetadataSaveModel duplicate{{
      {first, 0xAAAAAAAAU, "replacement"},
      {first, 0xBBBBBBBBU, "duplicate"},
  }};

  const graphscore::Result duplicate_result =
      graphscore::Persistence{}.restore_node_metadata(duplicate, project);
  const graphscore::ProjectNodeMetadataSaveModel incomplete{{
      {first, 0xAAAAAAAAU, "replacement"},
  }};
  const graphscore::Result                       incomplete_result =
      graphscore::Persistence{}.restore_node_metadata(incomplete, project);

  EXPECT_EQ(duplicate_result.code(), graphscore::ResultCode::kCorruptedData);
  EXPECT_EQ(incomplete_result.code(), graphscore::ResultCode::kCorruptedData);
  EXPECT_EQ(project.find_node(first)->color(), 0x11111111U);
  EXPECT_EQ(project.find_node(first)->notes(), "unchanged first");
  EXPECT_EQ(project.find_node(second)->color(), 0x22222222U);
  EXPECT_EQ(project.find_node(second)->notes(), "unchanged second");
}

}  // namespace
