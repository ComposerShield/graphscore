// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>

namespace {

class ConnectorInspectorMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return {{0.0, 0.0, staff_space, staff_space}, staff_space};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

struct ConnectorInspectorFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Inspector"};
  graphscore::NodeId  source_id  = project.add_node("Source");
  graphscore::NodeId  target_id  = project.add_node("Target");
  graphscore::Node*   source     = project.find_node(source_id);
  graphscore::Node*   target     = project.find_node(target_id);
  graphscore::ConnectorId output = source->add_output("Exit");
  graphscore::ConnectorId input  = target->add_input("Entry");
  graphscore::EventId     event = *project.events().add_event("Combat started");
  ConnectorInspectorMetrics metrics;

  ConnectorInspectorFixture() {
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(source_id, output, target_id, input)
                    .ok());
    EXPECT_TRUE(graphscore::Graph(project)
                    .bind_output_event(source_id, output, event)
                    .ok());
    EXPECT_TRUE(
        source->set_listener_policy(event, graphscore::QueuePolicy::kFifo, 7U)
            .ok());
    source->find_output(output)->set_priority(23);
    EXPECT_TRUE(source->find_output(output)
                    ->set_weight(*graphscore::Rational::create(2, 5))
                    .ok());
  }
};

TEST(CanvasConnectorInspectorTest, ProjectsConnectorAndLinkedListenerFields) {
  ConnectorInspectorFixture fixture;

  const auto inspector = graphscore::Canvas{}.inspect_connector(
      fixture.project, fixture.source_id, fixture.output);

  ASSERT_TRUE(inspector.has_value());
  EXPECT_EQ(inspector->source_node_id, fixture.source_id);
  EXPECT_EQ(inspector->source_node_name, "Source");
  EXPECT_EQ(inspector->output_id, fixture.output);
  EXPECT_EQ(inspector->name, "Exit");
  EXPECT_EQ(inspector->priority, 23);
  EXPECT_EQ(inspector->random_weight, *graphscore::Rational::create(2, 5));
  ASSERT_TRUE(inspector->event.has_value());
  EXPECT_EQ(inspector->event->event_id, fixture.event);
  ASSERT_TRUE(inspector->event->event_name.has_value());
  EXPECT_EQ(*inspector->event->event_name, "Combat started");
  ASSERT_TRUE(inspector->listener.has_value());
  EXPECT_EQ(inspector->listener->policy, graphscore::QueuePolicy::kFifo);
  EXPECT_EQ(inspector->listener->capacity, 7U);
  ASSERT_TRUE(inspector->destination.has_value());
  EXPECT_EQ(inspector->destination->node_id, fixture.target_id);
  EXPECT_EQ(inspector->destination->node_name,
            std::optional<std::string>("Target"));
  EXPECT_EQ(inspector->destination->input_id, fixture.input);
  EXPECT_EQ(inspector->destination->input_name,
            std::optional<std::string>("Entry"));
}

TEST(CanvasConnectorInspectorTest, ExposesUnboundAndUnconnectedOutputFields) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Free"};
  const auto          node_id = project.add_node("Node");
  const auto output = project.find_node(node_id)->add_output("Free output");

  const auto inspector =
      graphscore::Canvas{}.inspect_connector(project, node_id, output);

  ASSERT_TRUE(inspector.has_value());
  EXPECT_FALSE(inspector->event.has_value());
  EXPECT_FALSE(inspector->listener.has_value());
  EXPECT_FALSE(inspector->destination.has_value());
  EXPECT_TRUE(std::ranges::any_of(
      inspector->diagnostics, [](const graphscore::Diagnostic& diagnostic) {
        return diagnostic.code ==
               graphscore::DiagnosticCode::kExportDestinationRequired;
      }));
}

TEST(CanvasConnectorInspectorTest,
     AuthorsFieldsUndoablyAndRefreshesRetainedPortName) {
  ConnectorInspectorFixture  fixture;
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasConnectorInspectorController controller{fixture.project,
                                                            history, scene};
  const auto second_event = fixture.project.events().add_event("Combat ended");
  ASSERT_TRUE(second_event.has_value());

  EXPECT_TRUE(
      controller.set_name(fixture.source_id, fixture.output, "Outro").ok());
  EXPECT_TRUE(
      controller.set_priority(fixture.source_id, fixture.output, 41).ok());
  EXPECT_TRUE(controller
                  .set_random_weight(fixture.source_id, fixture.output,
                                     *graphscore::Rational::create(1, 3))
                  .ok());
  EXPECT_TRUE(
      controller
          .set_event_binding(fixture.source_id, fixture.output, *second_event)
          .ok());
  EXPECT_TRUE(controller
                  .set_listener(fixture.source_id, fixture.output,
                                graphscore::QueuePolicy::kFirstWins, 4U)
                  .ok());

  const auto* output = fixture.source->find_output(fixture.output);
  EXPECT_EQ(output->name(), "Outro");
  EXPECT_EQ(output->priority(), 41);
  EXPECT_EQ(output->weight(), *graphscore::Rational::create(1, 3));
  EXPECT_EQ(output->event_binding(), second_event);
  const auto* listener = fixture.source->find_listener(*second_event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), graphscore::QueuePolicy::kFirstWins);
  EXPECT_EQ(listener->capacity(), 4U);
  const auto& port =
      *std::ranges::find(scene.nodes[0].ports, fixture.output,
                         &graphscore::CanvasNodePort::connector_id);
  EXPECT_EQ(port.name, "Outro");
  EXPECT_EQ(port.accessibility_label, "Outro, output port");
  EXPECT_EQ(history.undo_stack_size(), 5U);

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_EQ(fixture.source->find_listener(*second_event)->policy(),
            graphscore::QueuePolicy::kLatestValidWins);
}

TEST(CanvasConnectorInspectorTest, ListenerFieldsAreSharedByMatchingOutputs) {
  ConnectorInspectorFixture fixture;
  const auto second_output = fixture.source->add_output("Alternate");
  ASSERT_TRUE(
      graphscore::Graph(fixture.project)
          .bind_output_event(fixture.source_id, second_output, fixture.event)
          .ok());
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasConnectorInspectorController controller{fixture.project,
                                                            history, scene};

  ASSERT_TRUE(controller
                  .set_listener(fixture.source_id, second_output,
                                graphscore::QueuePolicy::kFirstWins, 9U)
                  .ok());

  const auto first = graphscore::Canvas{}.inspect_connector(
      fixture.project, fixture.source_id, fixture.output);
  const auto second = graphscore::Canvas{}.inspect_connector(
      fixture.project, fixture.source_id, second_output);
  ASSERT_TRUE(first->listener.has_value());
  ASSERT_TRUE(second->listener.has_value());
  EXPECT_EQ(first->listener, second->listener);
  EXPECT_EQ(first->listener->policy, graphscore::QueuePolicy::kFirstWins);
  EXPECT_EQ(first->listener->capacity, 9U);
}

TEST(CanvasConnectorInspectorTest,
     RejectsEditsAfterUndoStalesRetainedPortName) {
  ConnectorInspectorFixture  fixture;
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasConnectorInspectorController controller{fixture.project,
                                                            history, scene};
  ASSERT_TRUE(
      controller.set_name(fixture.source_id, fixture.output, "Outro").ok());
  ASSERT_TRUE(history.undo(fixture.project).ok());

  EXPECT_EQ(
      controller.set_priority(fixture.source_id, fixture.output, 99).code(),
      graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(
      controller.set_name(fixture.source_id, fixture.output, "Exit").code(),
      graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(fixture.source->find_output(fixture.output)->priority(), 23);
  EXPECT_EQ(scene.nodes[0].ports[0].name, "Outro");
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

TEST(CanvasConnectorInspectorTest,
     ExcludesDiagnosticsFromDuplicateConnectorOccurrenceOnAnotherNode) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Duplicates"};
  const auto          first_node  = project.add_node("First");
  const auto          second_node = project.add_node("Second");
  graphscore::Node* const first   = project.find_node(first_node);
  graphscore::Node* const second  = project.find_node(second_node);
  const auto              output  = first->add_output("Shared ID");
  ASSERT_TRUE(
      second->restore_output(*first->find_output(output), std::nullopt).ok());

  const auto inspector =
      graphscore::Canvas{}.inspect_connector(project, first_node, output);

  ASSERT_TRUE(inspector.has_value());
  EXPECT_EQ(std::ranges::count_if(
                inspector->diagnostics,
                [](const graphscore::Diagnostic& diagnostic) {
                  return diagnostic.code ==
                         graphscore::DiagnosticCode::kExportDestinationRequired;
                }),
            1);
  EXPECT_TRUE(std::ranges::all_of(
      inspector->diagnostics, [&](const graphscore::Diagnostic& diagnostic) {
        return !std::holds_alternative<graphscore::ConnectorId>(
                   diagnostic.entity) ||
               !diagnostic.node.has_value() || diagnostic.node == first_node;
      }));
}

TEST(CanvasConnectorInspectorTest,
     PreservesDanglingEventIdentityAndDiagnostic) {
  ConnectorInspectorFixture fixture;
  const auto                dangling_event = graphscore::EventId::generate();
  ASSERT_TRUE(
      fixture.source->bind_output_event(fixture.output, dangling_event).ok());

  const auto inspector = graphscore::Canvas{}.inspect_connector(
      fixture.project, fixture.source_id, fixture.output);

  ASSERT_TRUE(inspector->event.has_value());
  EXPECT_EQ(inspector->event->event_id, dangling_event);
  EXPECT_FALSE(inspector->event->event_name.has_value());
  EXPECT_TRUE(inspector->listener.has_value());
  EXPECT_TRUE(std::ranges::any_of(
      inspector->diagnostics, [](const graphscore::Diagnostic& diagnostic) {
        return diagnostic.code ==
               graphscore::DiagnosticCode::kDanglingEventBinding;
      }));
}

TEST(CanvasConnectorInspectorTest, RejectsInvalidListenerAndNegativeWeight) {
  ConnectorInspectorFixture  fixture;
  graphscore::CommandHistory history;
  auto                       scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);
  graphscore::CanvasConnectorInspectorController controller{fixture.project,
                                                            history, scene};

  EXPECT_EQ(controller
                .set_listener(fixture.source_id, fixture.output,
                              graphscore::QueuePolicy::kFifo, 0U)
                .code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(controller
                .set_random_weight(fixture.source_id, fixture.output,
                                   *graphscore::Rational::create(-1, 2))
                .code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(controller
                .set_listener(fixture.source_id, fixture.output,
                              static_cast<graphscore::QueuePolicy>(0xFFU), 1U)
                .code(),
            graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(history.undo_stack_size(), 0U);
}

}  // namespace
