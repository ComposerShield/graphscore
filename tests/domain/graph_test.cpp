// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ConnectorId;
using graphscore::ConnectorType;
using graphscore::EventId;
using graphscore::Graph;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::OutputConnector;
using graphscore::Project;
using graphscore::ProjectId;

namespace {

Project make_project() {
  return Project(ProjectId::generate(), "Graph");
}

}  // namespace

TEST(GraphTest, ConnectSucceedsBetweenExistingNodesAndConnectors) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");

  const ConnectorId out_id = project.find_node(a_id)->add_output("Out");
  const ConnectorId in_id  = project.find_node(b_id)->add_input("In");

  EXPECT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  const auto* output = project.find_node(a_id)->find_output(out_id);
  ASSERT_NE(output, nullptr);
  ASSERT_TRUE(output->destination().has_value());
  EXPECT_EQ(output->destination()->node, b_id);
  EXPECT_EQ(output->destination()->connector, in_id);
}

TEST(GraphTest, ConnectFailsForUnknownNodeOrConnector) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId      a_id   = project.add_node("A");
  const NodeId      b_id   = project.add_node("B");
  const ConnectorId out_id = project.find_node(a_id)->add_output("Out");
  const ConnectorId in_id  = project.find_node(b_id)->add_input("In");

  EXPECT_FALSE(graph.connect(NodeId::generate(), out_id, b_id, in_id).ok());
  EXPECT_FALSE(graph.connect(a_id, ConnectorId::generate(), b_id, in_id).ok());
  EXPECT_FALSE(graph.connect(a_id, out_id, NodeId::generate(), in_id).ok());
  EXPECT_FALSE(graph.connect(a_id, out_id, b_id, ConnectorId::generate()).ok());
}

TEST(GraphTest, ConnectRejectsSecondDestinationWithoutDisconnectFirst) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");
  const NodeId c_id = project.add_node("C");

  const ConnectorId out_id  = project.find_node(a_id)->add_output("Out");
  const ConnectorId in_b_id = project.find_node(b_id)->add_input("In");
  const ConnectorId in_c_id = project.find_node(c_id)->add_input("In");

  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_b_id).ok());
  EXPECT_FALSE(graph.connect(a_id, out_id, c_id, in_c_id).ok());

  ASSERT_TRUE(graph.disconnect(a_id, out_id).ok());
  EXPECT_TRUE(graph.connect(a_id, out_id, c_id, in_c_id).ok());
}

TEST(GraphTest, DisconnectFailsWhenNoDestinationExists) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId      a_id   = project.add_node("A");
  const ConnectorId out_id = project.find_node(a_id)->add_output("Out");

  EXPECT_FALSE(graph.disconnect(a_id, out_id).ok());
}

TEST(GraphTest, CyclesIncludingSelfLoopsAreAccepted) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");

  const ConnectorId a_out = project.find_node(a_id)->add_output("Out");
  const ConnectorId a_in  = project.find_node(a_id)->add_input("In");
  const ConnectorId b_out = project.find_node(b_id)->add_output("Out");
  const ConnectorId b_in  = project.find_node(b_id)->add_input("In");

  // A -> B -> A is a two-node cycle.
  EXPECT_TRUE(graph.connect(a_id, a_out, b_id, b_in).ok());
  EXPECT_TRUE(graph.connect(b_id, b_out, a_id, a_in).ok());

  // A single node can also loop back to itself.
  const ConnectorId self_out = project.find_node(a_id)->add_output("Self Out");
  const ConnectorId self_in  = project.find_node(a_id)->add_input("Self In");
  EXPECT_TRUE(graph.connect(a_id, self_out, a_id, self_in).ok());
}

TEST(GraphTest, ExportReadyRequiresDestinationOnlyForEnabledOutputs) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");

  Node*             a      = project.find_node(a_id);
  const ConnectorId out_id = a->add_output("Out");
  const ConnectorId in_id  = project.find_node(b_id)->add_input("In");

  // A fresh export-enabled, unconnected output is not export-ready.
  EXPECT_FALSE(graph.is_export_ready());

  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());
  EXPECT_TRUE(graph.is_export_ready());

  // A second output, disabled for export, does not have to be connected.
  const ConnectorId stub_out = a->add_output("Stub");
  OutputConnector*  stub     = a->find_output(stub_out);
  ASSERT_NE(stub, nullptr);
  stub->set_export_enabled(false);
  EXPECT_TRUE(graph.is_export_ready());

  // Re-enabling it without a destination breaks export readiness again.
  stub->set_export_enabled(true);
  EXPECT_FALSE(graph.is_export_ready());
}

TEST(GraphTest, BindOutputEventFailsForUnregisteredEvent) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId      node_id = project.add_node("Node");
  const ConnectorId out_id =
      project.find_node(node_id)->add_output("Out", ConnectorType::kVertical);

  EXPECT_FALSE(
      graph.bind_output_event(node_id, out_id, EventId::generate()).ok());
}

TEST(GraphTest, BindOutputEventSucceedsForRegisteredEvent) {
  Project project = make_project();
  Graph   graph(project);

  const auto event_id = project.events().add_event("Attack");
  ASSERT_TRUE(event_id.has_value());

  const NodeId      node_id = project.add_node("Node");
  const ConnectorId out_id =
      project.find_node(node_id)->add_output("Out", ConnectorType::kVertical);

  EXPECT_TRUE(graph.bind_output_event(node_id, out_id, event_id).ok());
  const auto* output = project.find_node(node_id)->find_output(out_id);
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(output->event_binding(), *event_id);
}

TEST(GraphTest, RemoveInputClearsDestinationsAcrossOtherNodes) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");

  const ConnectorId out_id = project.find_node(a_id)->add_output("Out");
  const ConnectorId in_id  = project.find_node(b_id)->add_input("In");

  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());
  ASSERT_TRUE(graph.remove_input(b_id, in_id).ok());

  const auto* output = project.find_node(a_id)->find_output(out_id);
  ASSERT_NE(output, nullptr);
  EXPECT_FALSE(output->destination().has_value());
  EXPECT_EQ(project.find_node(b_id)->find_input(in_id), nullptr);
}

TEST(GraphTest, RemoveInputFailsForUnknownNodeOrConnector) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId node_id = project.add_node("Node");
  EXPECT_FALSE(
      graph.remove_input(NodeId::generate(), ConnectorId::generate()).ok());
  EXPECT_FALSE(graph.remove_input(node_id, ConnectorId::generate()).ok());
}

TEST(GraphTest, RemoveInputClearsDestinationsIncludingASelfLoop) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id = project.add_node("A");

  const ConnectorId self_out = project.find_node(a_id)->add_output("Out");
  const ConnectorId self_in  = project.find_node(a_id)->add_input("In");

  ASSERT_TRUE(graph.connect(a_id, self_out, a_id, self_in).ok());
  ASSERT_TRUE(graph.remove_input(a_id, self_in).ok());

  const auto* output = project.find_node(a_id)->find_output(self_out);
  ASSERT_NE(output, nullptr);
  EXPECT_FALSE(output->destination().has_value());
  EXPECT_EQ(project.find_node(a_id)->find_input(self_in), nullptr);
}

TEST(GraphTest, RemoveEventFailsForUnregisteredEvent) {
  Project project = make_project();
  Graph   graph(project);

  EXPECT_FALSE(graph.remove_event(EventId::generate()).ok());
}

TEST(GraphTest,
     RemoveEventUnbindsEveryOutputAcrossEveryNodeAndRemovesRegistration) {
  Project project = make_project();
  Graph   graph(project);

  const auto event_id = project.events().add_event("Attack");
  ASSERT_TRUE(event_id.has_value());

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");

  const ConnectorId a_out =
      project.find_node(a_id)->add_output("Out", ConnectorType::kVertical);
  const ConnectorId b_out =
      project.find_node(b_id)->add_output("Out", ConnectorType::kVertical);

  ASSERT_TRUE(graph.bind_output_event(a_id, a_out, event_id).ok());
  ASSERT_TRUE(graph.bind_output_event(b_id, b_out, event_id).ok());
  ASSERT_NE(project.find_node(a_id)->find_listener(*event_id), nullptr);
  ASSERT_NE(project.find_node(b_id)->find_listener(*event_id), nullptr);

  ASSERT_TRUE(graph.remove_event(*event_id).ok());

  EXPECT_EQ(project.events().find_by_id(*event_id), nullptr);

  const auto* a_output = project.find_node(a_id)->find_output(a_out);
  ASSERT_NE(a_output, nullptr);
  EXPECT_FALSE(a_output->event_binding().has_value());
  const auto* b_output = project.find_node(b_id)->find_output(b_out);
  ASSERT_NE(b_output, nullptr);
  EXPECT_FALSE(b_output->event_binding().has_value());

  EXPECT_EQ(project.find_node(a_id)->find_listener(*event_id), nullptr);
  EXPECT_EQ(project.find_node(b_id)->find_listener(*event_id), nullptr);
}
