// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8d-i — AddInputConnectorCommand
// =========================================================================

TEST(CommandTest, AddInputConnectorRoundTripPreservesId) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");

  ASSERT_TRUE(cmd->execute(project).ok());
  Node* node = project.find_node(node_id);
  ASSERT_EQ(node->inputs().size(), 1u);
  const ConnectorId created_id = node->inputs().front().id();
  EXPECT_EQ(node->find_input(created_id)->name(), "In");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_input(created_id), nullptr);
  EXPECT_TRUE(node->inputs().empty());

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_EQ(node->inputs().size(), 1u);
  EXPECT_EQ(node->inputs().front().id(), created_id);
  EXPECT_EQ(node->find_input(created_id)->name(), "In");
}

TEST(CommandTest, AddInputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd =
      std::make_unique<AddInputConnectorCommand>(NodeId::generate(), "In");
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddInputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->inputs().size(), 1u);
}

TEST(CommandTest, AddInputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(project.find_node(node_id)->inputs().empty());
}

TEST(CommandTest, AddInputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->inputs().size(), 1u);
}

// =========================================================================
// Phase 8d-i — AddOutputConnectorCommand
// =========================================================================

TEST(CommandTest, AddOutputConnectorRoundTripPreservesId) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kVertical);

  ASSERT_TRUE(cmd->execute(project).ok());
  Node* node = project.find_node(node_id);
  ASSERT_EQ(node->outputs().size(), 1u);
  const ConnectorId created_id = node->outputs().front().id();
  EXPECT_EQ(node->find_output(created_id)->name(), "Out");
  EXPECT_EQ(node->find_output(created_id)->type(), ConnectorType::kVertical);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(created_id), nullptr);
  EXPECT_TRUE(node->outputs().empty());

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_EQ(node->outputs().size(), 1u);
  EXPECT_EQ(node->outputs().front().id(), created_id);
  EXPECT_EQ(node->find_output(created_id)->name(), "Out");
  EXPECT_EQ(node->find_output(created_id)->type(), ConnectorType::kVertical);
}

TEST(CommandTest, AddOutputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      NodeId::generate(), "Out", ConnectorType::kSequential);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddOutputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kSequential);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->outputs().size(), 1u);
}

TEST(CommandTest, AddOutputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kSequential);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(project.find_node(node_id)->outputs().empty());
}

TEST(CommandTest, AddOutputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kSequential);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->outputs().size(), 1u);
}

// =========================================================================
// Phase 8d-i — RemoveOutputConnectorCommand
// =========================================================================

TEST(CommandTest, RemoveOutputConnectorRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored = node->find_output(out_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->name(), "Out");
  EXPECT_EQ(restored->type(), ConnectorType::kSequential);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);
}

TEST(CommandTest, RemoveOutputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(
      NodeId::generate(), ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveOutputConnectorMissingConnectorIdFailsNoMutation) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(
      node_id, ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveOutputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto out_id  = project.find_node(node_id)->add_output("Out");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveOutputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto out_id  = project.find_node(node_id)->add_output("Out");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id)->find_output(out_id), nullptr);
}

TEST(CommandTest, RemoveOutputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto out_id  = project.find_node(node_id)->add_output("Out");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest,
     RemoveOutputConnectorRestoresDestroyedListenerAcrossUndoRedo) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto event   = EventId::generate();
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());
  ASSERT_TRUE(node->set_listener_policy(event, QueuePolicy::kFifo, 5).ok());

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);
  EXPECT_EQ(node->find_listener(event), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored = node->find_output(out_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->event_binding(), event);
  const auto* listener = node->find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(listener->capacity(), 5u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);
  EXPECT_EQ(node->find_listener(event), nullptr);
}

TEST(CommandTest, RemoveOutputConnectorSurvivingListenerCaseUnaffected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto event   = EventId::generate();
  const auto first   = node->add_output("First", ConnectorType::kVertical);
  const auto second  = node->add_output("Second", ConnectorType::kVertical);
  ASSERT_TRUE(node->bind_output_event(first, event).ok());
  ASSERT_TRUE(node->bind_output_event(second, event).ok());
  ASSERT_TRUE(node->set_listener_policy(event, QueuePolicy::kFifo, 3).ok());

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, first);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(first), nullptr);
  const auto* still_listening = node->find_listener(event);
  ASSERT_NE(still_listening, nullptr);
  EXPECT_EQ(still_listening->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(still_listening->capacity(), 3u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_NE(node->find_output(first), nullptr);
  const auto* after_undo = node->find_listener(event);
  ASSERT_NE(after_undo, nullptr);
  EXPECT_EQ(after_undo->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(after_undo->capacity(), 3u);
}

// =========================================================================
// Phase 8d-i — RemoveInputConnectorCommand
// =========================================================================

TEST(CommandTest, RemoveInputConnectorRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_input(in_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored = node->find_input(in_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->name(), "In");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_input(in_id), nullptr);
}

TEST(CommandTest, RemoveInputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(
      NodeId::generate(), ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveInputConnectorMissingConnectorIdFailsNoMutation) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(
      node_id, ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveInputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto in_id   = project.find_node(node_id)->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveInputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto in_id   = project.find_node(node_id)->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id)->find_input(in_id), nullptr);
}

TEST(CommandTest, RemoveInputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto in_id   = project.find_node(node_id)->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest,
     RemoveInputConnectorCrossNodeCascadeRestoresDestinationAndRoute) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  Node*      a       = project.find_node(a_id);
  Node*      b       = project.find_node(b_id);
  const auto in_x    = a->add_input("X");
  const auto b_out   = b->add_output("Out");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, in_x).ok());

  OutputConnector*              b_output  = b->find_output(b_out);
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {5.0, 0.0}};
  ASSERT_TRUE(b_output->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(a_id, in_x);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored_input = a->find_input(in_x);
  ASSERT_NE(restored_input, nullptr);
  EXPECT_EQ(restored_input->name(), "X");
  ASSERT_TRUE(b_output->destination().has_value());
  EXPECT_EQ(b_output->destination()->node, a_id);
  EXPECT_EQ(b_output->destination()->connector, in_x);
  EXPECT_FALSE(b_output->route().is_automatic());
  EXPECT_EQ(b_output->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());
}

TEST(CommandTest, RemoveInputConnectorMultiOutputFanInRestoredOnUndo) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto c_id    = project.add_node("C");
  Node*      a       = project.find_node(a_id);
  Node*      b       = project.find_node(b_id);
  Node*      c       = project.find_node(c_id);
  const auto in_x    = a->add_input("X");
  const auto b_out   = b->add_output("Out");
  const auto c_out   = c->add_output("Out");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, in_x).ok());
  ASSERT_TRUE(graph.connect(c_id, c_out, a_id, in_x).ok());

  OutputConnector*              b_output    = b->find_output(b_out);
  OutputConnector*              c_output    = c->find_output(c_out);
  const std::vector<RoutePoint> b_waypoints = {{0.0, 0.0}, {5.0, 0.0}};
  const std::vector<RoutePoint> c_waypoints = {{0.0, 0.0}, {0.0, 7.0}};
  ASSERT_TRUE(b_output->route().set_custom_route(b_waypoints).ok());
  ASSERT_TRUE(c_output->route().set_custom_route(c_waypoints).ok());

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(a_id, in_x);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_NE(a->find_input(in_x), nullptr);
  ASSERT_TRUE(b_output->destination().has_value());
  EXPECT_EQ(b_output->destination()->connector, in_x);
  EXPECT_FALSE(b_output->route().is_automatic());
  EXPECT_EQ(b_output->route().waypoints(), b_waypoints);
  ASSERT_TRUE(c_output->destination().has_value());
  EXPECT_EQ(c_output->destination()->connector, in_x);
  EXPECT_FALSE(c_output->route().is_automatic());
  EXPECT_EQ(c_output->route().waypoints(), c_waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());
}

// =========================================================================
// Phase 8d-i — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8di) {
  auto run_sequence = [](Project& project) {
    const auto node_id = project.add_node("Node");

    CommandHistory history;
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<AddInputConnectorCommand>(
                                     node_id, "In"),
                                 project)
                    .ok());
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<AddOutputConnectorCommand>(
                             node_id, "Out", ConnectorType::kSequential),
                         project)
            .ok());

    const Node* node   = project.find_node(node_id);
    const auto  in_id  = node->inputs().front().id();
    const auto  out_id = node->outputs().front().id();

    EXPECT_TRUE(history
                    .execute_new(std::make_unique<RemoveInputConnectorCommand>(
                                     node_id, in_id),
                                 project)
                    .ok());

    return std::pair{node_id, out_id};
  };

  Project first  = make_project();
  Project second = make_project();

  const auto [first_node, first_out]   = run_sequence(first);
  const auto [second_node, second_out] = run_sequence(second);

  const Node* first_node_ptr  = first.find_node(first_node);
  const Node* second_node_ptr = second.find_node(second_node);

  EXPECT_TRUE(first_node_ptr->inputs().empty());
  EXPECT_TRUE(second_node_ptr->inputs().empty());
  EXPECT_EQ(first_node_ptr->outputs().size(), 1u);
  EXPECT_EQ(second_node_ptr->outputs().size(), 1u);
  EXPECT_EQ(first_node_ptr->find_output(first_out)->name(), "Out");
  EXPECT_EQ(second_node_ptr->find_output(second_out)->name(), "Out");
  EXPECT_EQ(first_node_ptr->find_output(first_out)->type(),
            second_node_ptr->find_output(second_out)->type());
}

// =========================================================================
// Phase 8d-ii — RegisterEventCommand
// =========================================================================

TEST(CommandTest, RegisterEventRoundTripPreservesId) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");

  ASSERT_TRUE(cmd->execute(project).ok());
  const auto* registered = project.events().find_by_name("Attack");
  ASSERT_NE(registered, nullptr);
  const EventId created_id = registered->id;
  EXPECT_EQ(project.events().size(), 1u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.events().find_by_id(created_id), nullptr);
  EXPECT_EQ(project.events().size(), 0u);

  ASSERT_TRUE(cmd->redo(project).ok());
  const auto* restored = project.events().find_by_id(created_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->name, "Attack");
  EXPECT_EQ(project.events().size(), 1u);
}

TEST(CommandTest, RegisterEventDuplicateNameFailsNoMutation) {
  Project project = make_project();
  ASSERT_TRUE(project.events().add_event("Attack").has_value());

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 1u);
}

TEST(CommandTest, RegisterEventDoubleExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 1u);
}

TEST(CommandTest, RegisterEventUndoWithoutExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 0u);
}

TEST(CommandTest, RegisterEventRedoWithoutUndoRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 1u);
}

// =========================================================================
// Phase 8d-ii — RemoveEventCommand
// =========================================================================

TEST(CommandTest, RemoveEventRoundTripRestoresBindingsAndListeners) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  const auto node_a = project.add_node("A");
  const auto node_b = project.add_node("B");
  Node*      a      = project.find_node(node_a);
  Node*      b      = project.find_node(node_b);
  const auto out_a  = a->add_output("Out", ConnectorType::kSequential);
  const auto out_b  = b->add_output("Out", ConnectorType::kSequential);

  Graph graph(project);
  ASSERT_TRUE(graph.bind_output_event(node_a, out_a, event_id).ok());
  ASSERT_TRUE(graph.bind_output_event(node_b, out_b, event_id).ok());
  ASSERT_TRUE(a->set_listener_policy(event_id, QueuePolicy::kFifo, 4).ok());
  ASSERT_TRUE(
      b->set_listener_policy(event_id, QueuePolicy::kFirstWins, 1).ok());

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.events().find_by_id(event_id), nullptr);
  EXPECT_FALSE(a->find_output(out_a)->event_binding().has_value());
  EXPECT_FALSE(b->find_output(out_b)->event_binding().has_value());
  EXPECT_EQ(a->find_listener(event_id), nullptr);
  EXPECT_EQ(b->find_listener(event_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored_def = project.events().find_by_id(event_id);
  ASSERT_NE(restored_def, nullptr);
  EXPECT_EQ(restored_def->name, "Attack");
  ASSERT_TRUE(a->find_output(out_a)->event_binding().has_value());
  EXPECT_EQ(*a->find_output(out_a)->event_binding(), event_id);
  ASSERT_TRUE(b->find_output(out_b)->event_binding().has_value());
  EXPECT_EQ(*b->find_output(out_b)->event_binding(), event_id);
  const auto* a_listener = a->find_listener(event_id);
  ASSERT_NE(a_listener, nullptr);
  EXPECT_EQ(a_listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(a_listener->capacity(), 4u);
  const auto* b_listener = b->find_listener(event_id);
  ASSERT_NE(b_listener, nullptr);
  EXPECT_EQ(b_listener->policy(), QueuePolicy::kFirstWins);
  EXPECT_EQ(b_listener->capacity(), 1u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.events().find_by_id(event_id), nullptr);
  EXPECT_FALSE(a->find_output(out_a)->event_binding().has_value());
  EXPECT_FALSE(b->find_output(out_b)->event_binding().has_value());
  EXPECT_EQ(a->find_listener(event_id), nullptr);
  EXPECT_EQ(b->find_listener(event_id), nullptr);
}

TEST(CommandTest, RemoveEventMissingIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<RemoveEventCommand>(EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveEventDoubleExecuteRejected) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveEventUndoWithoutExecuteRejected) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.events().find_by_id(event_id), nullptr);
}

TEST(CommandTest, RemoveEventRedoWithoutUndoRejected) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8d-ii — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8dii) {
  auto run_sequence = [](Project& project) {
    CommandHistory history;

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<RegisterEventCommand>("Attack"),
                         project)
            .ok());
    const EventId first_event = project.events().find_by_name("Attack")->id;

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<RegisterEventCommand>("Release"),
                         project)
            .ok());

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<RemoveEventCommand>(first_event),
                         project)
            .ok());

    return project.events().find_by_name("Release")->id;
  };

  Project first  = make_project();
  Project second = make_project();

  const EventId first_release  = run_sequence(first);
  const EventId second_release = run_sequence(second);

  EXPECT_EQ(first.events().size(), 1u);
  EXPECT_EQ(second.events().size(), 1u);
  EXPECT_EQ(first.events().find_by_name("Attack"), nullptr);
  EXPECT_EQ(second.events().find_by_name("Attack"), nullptr);
  EXPECT_NE(first.events().find_by_id(first_release), nullptr);
  EXPECT_NE(second.events().find_by_id(second_release), nullptr);
}
