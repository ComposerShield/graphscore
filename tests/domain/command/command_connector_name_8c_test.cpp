// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8c-i — SetOutputWeightCommand
// =========================================================================

TEST(CommandTest, SetOutputWeightRoundTrip) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);
  EXPECT_EQ(output->weight(), Rational(1));

  const Rational new_weight = *Rational::create(1, 3);
  auto           cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, new_weight);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(output->weight(), new_weight);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(output->weight(), Rational(1));

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(output->weight(), new_weight);
}

TEST(CommandTest, SetOutputWeightNegativeRejectedNoMutation) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(-1));
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(output->weight(), Rational(1));

  // Still kFresh -- undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputWeightMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd1 = std::make_unique<SetOutputWeightCommand>(NodeId::generate(),
                                                       out_id, Rational(1));
  EXPECT_FALSE(cmd1->execute(project).ok());

  auto cmd2 = std::make_unique<SetOutputWeightCommand>(
      node_id, ConnectorId::generate(), Rational(1));
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetOutputWeightDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(2));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputWeightUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(2));
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputWeightRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(2));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputExportEnabledCommand
// =========================================================================

TEST(CommandTest, SetOutputExportEnabledRoundTrip) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);
  EXPECT_TRUE(output->export_enabled());

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(output->export_enabled());

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_TRUE(output->export_enabled());

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(output->export_enabled());
}

TEST(CommandTest, SetOutputExportEnabledMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd1 = std::make_unique<SetOutputExportEnabledCommand>(
      NodeId::generate(), out_id, false);
  EXPECT_FALSE(cmd1->execute(project).ok());

  auto cmd2 = std::make_unique<SetOutputExportEnabledCommand>(
      node_id, ConnectorId::generate(), false);
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetOutputExportEnabledDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputExportEnabledUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputExportEnabledRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetInputConnectorNameCommand
// =========================================================================

TEST(CommandTest, SetInputConnectorNameRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "Renamed");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "In");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "Renamed");
}

TEST(CommandTest, SetInputConnectorNameEmptyLongAndUtf8RoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  const std::string kLong(4096, 'x');
  const std::string kUtf8 = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xCE\xA9";

  for (const std::string& name : {std::string(), kLong, kUtf8}) {
    auto cmd =
        std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, name);
    ASSERT_TRUE(cmd->execute(project).ok());
    EXPECT_EQ(node->find_input(in_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
    EXPECT_EQ(node->find_input(in_id)->name(), "In");
    ASSERT_TRUE(cmd->redo(project).ok());
    EXPECT_EQ(node->find_input(in_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
  }
}

TEST(CommandTest, SetInputConnectorNameUnrelatedConnectorsUntouched) {
  Project    project     = make_project();
  const auto node_id     = project.add_node("Node");
  Node*      node        = project.find_node(node_id);
  const auto in_id       = node->add_input("In");
  const auto other_in_id = node->add_input("Other In");
  const auto out_id      = node->add_output("Out");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_input(other_in_id)->name(), "Other In");
  EXPECT_EQ(node->find_output(out_id)->name(), "Out");
}

TEST(CommandTest, SetInputConnectorNameMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd1 = std::make_unique<SetInputConnectorNameCommand>(NodeId::generate(),
                                                             in_id, "Renamed");
  EXPECT_FALSE(cmd1->execute(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "In");

  auto cmd2 = std::make_unique<SetInputConnectorNameCommand>(
      node_id, ConnectorId::generate(), "Renamed");
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetInputConnectorNameDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetInputConnectorNameUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetInputConnectorNameRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputConnectorNameCommand
// =========================================================================

TEST(CommandTest, SetOutputConnectorNameRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Renamed");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Out");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Renamed");
}

TEST(CommandTest, SetOutputConnectorNameEmptyLongAndUtf8RoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  const std::string kLong(4096, 'y');
  const std::string kUtf8 = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xCE\xA9";

  for (const std::string& name : {std::string(), kLong, kUtf8}) {
    auto cmd =
        std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id, name);
    ASSERT_TRUE(cmd->execute(project).ok());
    EXPECT_EQ(node->find_output(out_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
    EXPECT_EQ(node->find_output(out_id)->name(), "Out");
    ASSERT_TRUE(cmd->redo(project).ok());
    EXPECT_EQ(node->find_output(out_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
  }
}

TEST(CommandTest, SetOutputConnectorNameUnrelatedConnectorsUntouched) {
  Project    project      = make_project();
  const auto node_id      = project.add_node("Node");
  Node*      node         = project.find_node(node_id);
  const auto out_id       = node->add_output("Out");
  const auto other_out_id = node->add_output("Other Out");
  const auto in_id        = node->add_input("In");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(other_out_id)->name(), "Other Out");
  EXPECT_EQ(node->find_input(in_id)->name(), "In");
}

TEST(CommandTest, SetOutputConnectorNameMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd1 = std::make_unique<SetOutputConnectorNameCommand>(
      NodeId::generate(), out_id, "Renamed");
  EXPECT_FALSE(cmd1->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Out");

  auto cmd2 = std::make_unique<SetOutputConnectorNameCommand>(
      node_id, ConnectorId::generate(), "Renamed");
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetOutputConnectorNameDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputConnectorNameUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputConnectorNameRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — deterministic replay
// =========================================================================

namespace {

struct ProjectSnapshot {
  std::size_t                active_track_count;
  std::size_t                archived_track_count;
  std::string                node_name;
  std::uint32_t              node_color;
  std::vector<int>           output_priorities;
  std::vector<Rational>      output_weights;
  std::vector<bool>          output_export_enabled;
  std::vector<ConnectorType> output_types;
  std::vector<std::string>   output_names;
  std::vector<std::string>   input_names;
};

ProjectSnapshot snapshot(const Project& project, NodeId node_id) {
  const Node*     node = project.find_node(node_id);
  ProjectSnapshot snap{
      .active_track_count    = project.active_tracks().size(),
      .archived_track_count  = project.archived_tracks().size(),
      .node_name             = node->name(),
      .node_color            = node->color(),
      .output_priorities     = {},
      .output_weights        = {},
      .output_export_enabled = {},
      .output_types          = {},
      .output_names          = {},
      .input_names           = {},
  };
  for (const auto& output : node->outputs()) {
    snap.output_priorities.push_back(output.priority());
    snap.output_weights.push_back(output.weight());
    snap.output_export_enabled.push_back(output.export_enabled());
    snap.output_types.push_back(output.type());
    snap.output_names.push_back(output.name());
  }
  for (const auto& input : node->inputs()) {
    snap.input_names.push_back(input.name());
  }
  return snap;
}

bool operator==(const ProjectSnapshot& a, const ProjectSnapshot& b) {
  return a.active_track_count == b.active_track_count &&
         a.archived_track_count == b.archived_track_count &&
         a.node_name == b.node_name && a.node_color == b.node_color &&
         a.output_priorities == b.output_priorities &&
         a.output_weights == b.output_weights &&
         a.output_export_enabled == b.output_export_enabled &&
         a.output_types == b.output_types && a.output_names == b.output_names &&
         a.input_names == b.input_names;
}

}  // namespace

TEST(CommandTest, DeterministicReplayProducesEqualProjects) {
  auto run_sequence = [](Project& project) {
    const auto track_id = project.add_track(
        "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
    const auto node_id = project.add_node("Node");
    Node*      node    = project.find_node(node_id);
    const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
    (void)node->add_input("In");

    CommandHistory history;
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<ArchiveTrackCommand>(*track_id),
                         project)
            .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<SetOutputPriorityCommand>(
                                     node_id, out_id, 4),
                                 project)
                    .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<SetOutputWeightCommand>(
                                     node_id, out_id, *Rational::create(1, 3)),
                                 project)
                    .ok());
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<SetOutputExportEnabledCommand>(
                             node_id, out_id, false),
                         project)
            .ok());
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<SetOutputConnectorNameCommand>(
                             node_id, out_id, "Renamed Out"),
                         project)
            .ok());
    return node_id;
  };

  Project first  = make_project();
  Project second = make_project();

  const NodeId first_node_id  = run_sequence(first);
  const NodeId second_node_id = run_sequence(second);

  EXPECT_TRUE(snapshot(first, first_node_id) ==
              snapshot(second, second_node_id));
}
