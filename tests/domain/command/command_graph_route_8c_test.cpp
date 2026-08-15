// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8c-ii — ConnectCommand
// =========================================================================

TEST(CommandTest, ConnectRoundTrip) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  const auto* out = project.find_node(a_id)->find_output(out_id);
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->destination().has_value());

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);
}

TEST(CommandTest, ConnectMissingNodeIdFails) {
  Project    project = make_project();
  const auto b_id    = project.add_node("B");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(
      NodeId::generate(), ConnectorId::generate(), b_id, in_id);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectMissingConnectorIdFails) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id,
                                              ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectMissingIdDoesNotChangeProject) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, NodeId::generate(),
                                              ConnectorId::generate());
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_FALSE(
      project.find_node(a_id)->find_output(out_id)->destination().has_value());
}

TEST(CommandTest, ConnectDoubleExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectAlreadyConnectedOutputFails) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto c_id    = project.add_node("C");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto b_in    = project.find_node(b_id)->add_input("In");
  const auto c_in    = project.find_node(c_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, b_in).ok());

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, c_id, c_in);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);

  // Still fresh: undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);

  const auto* out = project.find_node(a_id)->find_output(out_id);
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
}

TEST(CommandTest, ConnectPreservesCustomRouteAcrossUndo) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  OutputConnector* out = project.find_node(a_id)->find_output(out_id);
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};
  ASSERT_TRUE(out->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_TRUE(out->destination().has_value());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->destination().has_value());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_TRUE(out->destination().has_value());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);
}

// =========================================================================
// Phase 8c-ii — DisconnectCommand
// =========================================================================

TEST(CommandTest, DisconnectRoundTrip) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);

  const auto* out = project.find_node(a_id)->find_output(out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(out->destination().has_value());

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(out->destination().has_value());
}

TEST(CommandTest, DisconnectNoDestinationFails) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectMissingIdsFail) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto missing_node =
      std::make_unique<DisconnectCommand>(NodeId::generate(), out_id);
  EXPECT_EQ(missing_node->execute(project).code(),
            ResultCode::kInvalidArgument);

  auto missing_connector =
      std::make_unique<DisconnectCommand>(a_id, ConnectorId::generate());
  EXPECT_EQ(missing_connector->execute(project).code(),
            ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectDoubleExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectPreservesCustomRouteAcrossUndo) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  OutputConnector* out = project.find_node(a_id)->find_output(out_id);
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {0.0, 5.0}};
  ASSERT_TRUE(out->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(out->destination().has_value());
  EXPECT_TRUE(out->route().is_automatic());

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);
}

// =========================================================================
// Phase 8c-ii — BindOutputEventCommand
// =========================================================================

TEST(CommandTest, BindOutputEventRoundTripDestroysAndRecreatesListener) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event);
  ASSERT_NE(node->find_listener(*event), nullptr);
  EXPECT_EQ(node->find_listener(*event)->policy(),
            QueuePolicy::kLatestValidWins);
  EXPECT_EQ(node->find_listener(*event)->capacity(), 1u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
  EXPECT_EQ(node->find_listener(*event), nullptr);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event);
  ASSERT_NE(node->find_listener(*event), nullptr);
  EXPECT_EQ(node->find_listener(*event)->policy(),
            QueuePolicy::kLatestValidWins);
  EXPECT_EQ(node->find_listener(*event)->capacity(), 1u);
}

TEST(CommandTest,
     BindOutputEventRebindDestroysSoleListenerUndoRestoresExactConfig) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
  const auto event_e = project.events().add_event("E");
  const auto event_f = project.events().add_event("F");
  ASSERT_TRUE(event_e.has_value());
  ASSERT_TRUE(event_f.has_value());

  ASSERT_TRUE(node->bind_output_event(out_id, event_e).ok());
  ASSERT_TRUE(node->set_listener_policy(*event_e, QueuePolicy::kFifo, 5).ok());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event_f);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event_f);
  EXPECT_EQ(node->find_listener(*event_e), nullptr);
  ASSERT_NE(node->find_listener(*event_f), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event_e);
  ASSERT_NE(node->find_listener(*event_e), nullptr);
  EXPECT_EQ(node->find_listener(*event_e)->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(node->find_listener(*event_e)->capacity(), 5u);
}

TEST(CommandTest, BindOutputEventRebindSurvivingListenerConfigUnchanged) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out1    = node->add_output("Out1", ConnectorType::kSequential);
  const auto out2    = node->add_output("Out2", ConnectorType::kSequential);
  const auto event_e = project.events().add_event("E");
  const auto event_f = project.events().add_event("F");
  ASSERT_TRUE(event_e.has_value());
  ASSERT_TRUE(event_f.has_value());

  ASSERT_TRUE(node->bind_output_event(out1, event_e).ok());
  ASSERT_TRUE(node->bind_output_event(out2, event_e).ok());
  ASSERT_TRUE(node->set_listener_policy(*event_e, QueuePolicy::kFifo, 3).ok());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out1, event_f);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out1)->event_binding(), event_f);
  EXPECT_EQ(node->find_output(out2)->event_binding(), event_e);
  ASSERT_NE(node->find_listener(*event_e), nullptr);
  EXPECT_EQ(node->find_listener(*event_e)->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(node->find_listener(*event_e)->capacity(), 3u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out1)->event_binding(), event_e);
  ASSERT_NE(node->find_listener(*event_e), nullptr);
  EXPECT_EQ(node->find_listener(*event_e)->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(node->find_listener(*event_e)->capacity(), 3u);
  EXPECT_EQ(node->find_listener(*event_f), nullptr);
}

TEST(CommandTest, BindOutputEventClearToNulloptUndoRestoresBinding) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd =
      std::make_unique<BindOutputEventCommand>(node_id, out_id, std::nullopt);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
  EXPECT_EQ(node->find_listener(*event), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event);
  ASSERT_NE(node->find_listener(*event), nullptr);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
}

TEST(CommandTest, BindOutputEventMissingNodeIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<BindOutputEventCommand>(
      NodeId::generate(), out_id, EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventMissingConnectorIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<BindOutputEventCommand>(
      node_id, ConnectorId::generate(), EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventUnregisteredEventFailsNoMutation) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id,
                                                      EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
}

TEST(CommandTest, BindOutputEventDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-ii — SetCustomRouteCommand
// =========================================================================

TEST(CommandTest, SetCustomRouteRoundTrip) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);

  ASSERT_TRUE(cmd->execute(project).ok());
  const auto* out = node->find_output(out_id);
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);
}

TEST(CommandTest, SetCustomRouteInvalidWaypointsRejectedNoMutation) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {5.0, 3.0}};  // Diagonal: not axis-aligned.

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);

  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  const auto* out = node->find_output(out_id);
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());

  // Still fresh: undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteUndoRestoresPreviouslyCustomizedRoute) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  OutputConnector*              out      = node->find_output(out_id);
  const std::vector<RoutePoint> original = {{0.0, 0.0}, {0.0, 5.0}};
  ASSERT_TRUE(out->route().set_custom_route(original).ok());

  const std::vector<RoutePoint> replacement = {{0.0, 0.0}, {20.0, 0.0}};
  auto                          cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, replacement);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(out->route().waypoints(), replacement);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), original);
}

TEST(CommandTest, SetCustomRouteMissingIdsFail) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto missing_node = std::make_unique<SetCustomRouteCommand>(
      NodeId::generate(), ConnectorId::generate(), waypoints);
  EXPECT_EQ(missing_node->execute(project).code(),
            ResultCode::kInvalidArgument);

  auto missing_connector = std::make_unique<SetCustomRouteCommand>(
      node_id, ConnectorId::generate(), waypoints);
  EXPECT_EQ(missing_connector->execute(project).code(),
            ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteDoubleExecuteRejected) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteUndoWithoutExecuteRejected) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteRedoWithoutUndoRejected) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-ii — ResetRouteCommand
// =========================================================================

TEST(CommandTest, ResetRouteRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  OutputConnector*              out       = node->find_output(out_id);
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}};
  ASSERT_TRUE(out->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());
}

TEST(CommandTest, ResetRouteMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto missing_node = std::make_unique<ResetRouteCommand>(
      NodeId::generate(), ConnectorId::generate());
  EXPECT_EQ(missing_node->execute(project).code(),
            ResultCode::kInvalidArgument);

  auto missing_connector =
      std::make_unique<ResetRouteCommand>(node_id, ConnectorId::generate());
  EXPECT_EQ(missing_connector->execute(project).code(),
            ResultCode::kInvalidArgument);
}

TEST(CommandTest, ResetRouteDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");
  ASSERT_TRUE(
      node->find_output(out_id)->route().set_custom_route({{0.0, 0.0}}).ok());

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ResetRouteUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ResetRouteRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");
  ASSERT_TRUE(
      node->find_output(out_id)->route().set_custom_route({{0.0, 0.0}}).ok());

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-ii — deterministic replay (Connect + BindOutputEvent +
// SetCustomRoute)
// =========================================================================

namespace {

struct GraphOutputSnapshot {
  ConnectorType type;
  bool          has_destination;
  bool          has_event_binding;
  RouteGeometry route;
  QueuePolicy   listener_policy;
  std::size_t   listener_capacity;
  ConnectorType listener_bound_type;
};

GraphOutputSnapshot graph_output_snapshot(const Project& project,
                                          NodeId         node_id,
                                          ConnectorId    output_id) {
  const Node*            node = project.find_node(node_id);
  const OutputConnector* out  = node->find_output(output_id);

  GraphOutputSnapshot snap{
      .type                = out->type(),
      .has_destination     = out->destination().has_value(),
      .has_event_binding   = out->event_binding().has_value(),
      .route               = out->route(),
      .listener_policy     = QueuePolicy::kLatestValidWins,
      .listener_capacity   = 0,
      .listener_bound_type = ConnectorType::kSequential,
  };

  if (out->event_binding().has_value()) {
    const EventListener* listener = node->find_listener(*out->event_binding());
    if (listener != nullptr) {
      snap.listener_policy     = listener->policy();
      snap.listener_capacity   = listener->capacity();
      snap.listener_bound_type = listener->bound_type();
    }
  }

  return snap;
}

bool operator==(const GraphOutputSnapshot& a, const GraphOutputSnapshot& b) {
  return a.type == b.type && a.has_destination == b.has_destination &&
         a.has_event_binding == b.has_event_binding && a.route == b.route &&
         a.listener_policy == b.listener_policy &&
         a.listener_capacity == b.listener_capacity &&
         a.listener_bound_type == b.listener_bound_type;
}

}  // namespace

TEST(CommandTest, DeterministicReplay8cii) {
  auto run_sequence = [](Project& project) {
    const auto a_id = project.add_node("A");
    const auto b_id = project.add_node("B");
    const auto out_id =
        project.find_node(a_id)->add_output("Out", ConnectorType::kSequential);
    const auto in_id = project.find_node(b_id)->add_input("In");
    const auto event = project.events().add_event("Attack");
    EXPECT_TRUE(event.has_value());

    CommandHistory history;
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<ConnectCommand>(a_id, out_id,
                                                                  b_id, in_id),
                                 project)
                    .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<BindOutputEventCommand>(
                                     a_id, out_id, event),
                                 project)
                    .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<SetCustomRouteCommand>(
                                     a_id, out_id,
                                     std::vector<RoutePoint>{
                                         {0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}}),
                                 project)
                    .ok());
    return std::pair{a_id, out_id};
  };

  Project first  = make_project();
  Project second = make_project();

  const auto [first_node, first_out]   = run_sequence(first);
  const auto [second_node, second_out] = run_sequence(second);

  EXPECT_TRUE(graph_output_snapshot(first, first_node, first_out) ==
              graph_output_snapshot(second, second_node, second_out));
}
