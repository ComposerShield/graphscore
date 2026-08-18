// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/persistence/graphscore_persistence.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

TEST(PersistenceTest, GraphOperationsUndoRedoAndSurviveSaveReopen) {
  const graphscore::ProjectId project_id = graphscore::ProjectId::generate();
  graphscore::Project         project{project_id, "Graph"};
  const graphscore::NodeId    source      = project.add_node("Source");
  const graphscore::NodeId    destination = project.add_node("Destination");
  const graphscore::EventId   event = *project.events().add_event("Advance");
  graphscore::CommandHistory  history;

  auto execute = [&](std::unique_ptr<graphscore::Command> command) {
    ASSERT_TRUE(history.execute_new(std::move(command), project).ok());
  };
  execute(std::make_unique<graphscore::SetNodePositionCommand>(
      source, graphscore::GraphPosition{120.0, -45.0}));
  execute(std::make_unique<graphscore::SetNodeNameCommand>(source,
                                                           "Edited source"));
  execute(
      std::make_unique<graphscore::SetNodeColorCommand>(source, 0x12345678U));
  execute(
      std::make_unique<graphscore::SetNodeNotesCommand>(source, "Graph notes"));
  execute(std::make_unique<graphscore::AddOutputConnectorCommand>(
      source, "Output", graphscore::ConnectorType::kSequential));
  execute(std::make_unique<graphscore::AddInputConnectorCommand>(destination,
                                                                 "Input"));
  const graphscore::ConnectorId output =
      project.find_node(source)->outputs().back().id();
  const graphscore::ConnectorId input =
      project.find_node(destination)->inputs().back().id();
  execute(std::make_unique<graphscore::SetOutputConnectorNameCommand>(
      source, output, "Renamed output"));
  execute(std::make_unique<graphscore::SetInputConnectorNameCommand>(
      destination, input, "Renamed input"));
  execute(std::make_unique<graphscore::BindOutputEventCommand>(source, output,
                                                               event));
  execute(std::make_unique<graphscore::SetListenerPolicyCommand>(
      source, event, graphscore::QueuePolicy::kFifo, 7U));
  execute(std::make_unique<graphscore::SetOutputPriorityCommand>(source, output,
                                                                 12));
  execute(std::make_unique<graphscore::SetOutputWeightCommand>(
      source, output, *graphscore::Rational::create(1, 3)));
  execute(std::make_unique<graphscore::ConnectCommand>(source, output,
                                                       destination, input));
  const std::vector<graphscore::RoutePoint> route = {
      {130.0, -20.0}, {300.0, -20.0}, {300.0, 40.0}};
  execute(std::make_unique<graphscore::SetCustomRouteCommand>(source, output,
                                                              route));
  execute(std::make_unique<graphscore::SetStartNodeCommand>(source));

  const graphscore::ProjectGraphSaveModel edited =
      graphscore::Persistence{}.capture_graph(project);
  const std::size_t command_count = history.undo_stack_size();
  for (std::size_t index = 0; index < command_count; ++index)
    ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(source)->name(), "Source");
  EXPECT_TRUE(project.find_node(source)->outputs().empty());
  EXPECT_TRUE(project.find_node(destination)->inputs().empty());
  EXPECT_FALSE(project.start_node().has_value());

  for (std::size_t index = 0; index < command_count; ++index)
    ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(graphscore::Persistence{}.capture_graph(project), edited);

  graphscore::Project reopened = project;
  ASSERT_TRUE(reopened.remove_node(source).ok());
  ASSERT_TRUE(reopened.remove_node(destination).ok());
  ASSERT_TRUE(graphscore::Persistence{}.restore_graph(edited, reopened).ok());
  EXPECT_EQ(graphscore::Persistence{}.capture_graph(reopened), edited);
}

TEST(PersistenceTest, RejectsMalformedGraphAtomically) {
  const graphscore::ProjectId   project_id = graphscore::ProjectId::generate();
  graphscore::Project           project{project_id, "Graph"};
  const graphscore::NodeId      source      = project.add_node("Source");
  const graphscore::NodeId      destination = project.add_node("Destination");
  const graphscore::ConnectorId output =
      project.find_node(source)->add_output("Output");
  const graphscore::ConnectorId input =
      project.find_node(destination)->add_input("Input");
  ASSERT_TRUE(graphscore::Graph(project)
                  .connect(source, output, destination, input)
                  .ok());
  const graphscore::ProjectGraphSaveModel before =
      graphscore::Persistence{}.capture_graph(project);
  graphscore::ProjectGraphSaveModel malformed = before;
  malformed.nodes.pop_back();

  const graphscore::Result result =
      graphscore::Persistence{}.restore_graph(malformed, project);

  EXPECT_EQ(result.code(), graphscore::ResultCode::kCorruptedData);
  EXPECT_EQ(graphscore::Persistence{}.capture_graph(project), before);
}

}  // namespace
