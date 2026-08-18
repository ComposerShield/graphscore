// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace {

class OperationsMetrics final : public graphscore::GlyphMetrics {
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

struct OperationsFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Operations"};
  graphscore::NodeId  first  = project.add_node("First");
  graphscore::NodeId  second = project.add_node("Second");
  graphscore::NodeId  other  = project.add_node("Other");
  graphscore::ConnectorId first_output =
      project.find_node(first)->add_output("To second");
  graphscore::ConnectorId second_input =
      project.find_node(second)->add_input("From first");
  graphscore::ConnectorId second_output =
      project.find_node(second)->add_output("To other");
  graphscore::ConnectorId other_input =
      project.find_node(other)->add_input("From second");
  graphscore::CommandHistory      history;
  OperationsMetrics               metrics;
  graphscore::CanvasNotationScene scene;

  OperationsFixture() {
    project.find_node(first)->set_position({0.0, 0.0});
    project.find_node(second)->set_position({500.0, 0.0});
    project.find_node(other)->set_position({1000.0, 0.0});
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(first, first_output, second, second_input)
                    .ok());
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(second, second_output, other, other_input)
                    .ok());
    scene = graphscore::Canvas{}.layout_nodes(project, metrics);
  }
};

TEST(CanvasNodeOperationsTest, MultiSelectionUsesStableProjectOrder) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);

  ASSERT_TRUE(controller.select(fixture.second));
  ASSERT_TRUE(controller.select(fixture.first, true));
  EXPECT_EQ(controller.selection(),
            (std::vector{fixture.first, fixture.second}));

  ASSERT_TRUE(controller.select(fixture.first, true));
  EXPECT_EQ(controller.selection(), (std::vector{fixture.second}));
}

TEST(CanvasNodeOperationsTest, MovesSelectedNodesAsOneUndoableGesture) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);
  ASSERT_TRUE(controller.select(fixture.first));
  ASSERT_TRUE(controller.select(fixture.second, true));

  ASSERT_TRUE(controller.begin_move(fixture.first, {10.0, 20.0}));
  ASSERT_TRUE(controller.update_move({50.0, 80.0}));
  EXPECT_EQ(fixture.scene.nodes[0].position,
            (graphscore::GraphPosition{40.0, 60.0}));
  EXPECT_EQ(fixture.scene.nodes[1].position,
            (graphscore::GraphPosition{540.0, 60.0}));
  EXPECT_EQ(fixture.project.find_node(fixture.first)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));

  ASSERT_TRUE(controller.finish_move().ok());
  EXPECT_EQ(fixture.history.undo_stack_size(), 1U);
  ASSERT_TRUE(fixture.history.undo(fixture.project).ok());
  EXPECT_EQ(fixture.project.find_node(fixture.first)->position(),
            (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(fixture.project.find_node(fixture.second)->position(),
            (graphscore::GraphPosition{500.0, 0.0}));
}

TEST(CanvasNodeOperationsTest,
     DuplicateRemapsInternalEdgesAndDropsEdgesLeavingSelection) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);
  ASSERT_TRUE(controller.select(fixture.first));
  ASSERT_TRUE(controller.select(fixture.second, true));

  ASSERT_TRUE(controller.duplicate_selected({25.0, 30.0}).ok());
  ASSERT_EQ(controller.selection().size(), 2U);
  const graphscore::Node* duplicate_first =
      fixture.project.find_node(controller.selection()[0]);
  const graphscore::Node* duplicate_second =
      fixture.project.find_node(controller.selection()[1]);
  ASSERT_NE(duplicate_first, nullptr);
  ASSERT_NE(duplicate_second, nullptr);
  ASSERT_EQ(duplicate_first->outputs().size(), 1U);
  ASSERT_TRUE(duplicate_first->outputs()[0].destination().has_value());
  EXPECT_EQ(duplicate_first->outputs()[0].destination()->node,
            duplicate_second->id());
  EXPECT_TRUE(duplicate_first->outputs()[0].route().is_automatic());
  ASSERT_EQ(duplicate_second->outputs().size(), 1U);
  EXPECT_FALSE(duplicate_second->outputs()[0].destination().has_value());
  EXPECT_EQ(duplicate_first->position(),
            (graphscore::GraphPosition{25.0, 30.0}));
  EXPECT_EQ(fixture.scene.nodes.size(), fixture.project.nodes().size());
}

TEST(CanvasNodeOperationsTest, ClipboardSnapshotsSurviveSourceDeletion) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);
  ASSERT_TRUE(controller.select(fixture.first));
  ASSERT_TRUE(controller.select(fixture.second, true));
  ASSERT_TRUE(controller.copy_selected().ok());
  ASSERT_TRUE(controller.has_clipboard());

  fixture.project.find_node(fixture.first)->set_name("Edited after copy");
  ASSERT_TRUE(controller.delete_selected().ok());
  EXPECT_EQ(fixture.project.find_node(fixture.first), nullptr);
  EXPECT_EQ(fixture.project.find_node(fixture.second), nullptr);

  ASSERT_TRUE(controller.paste({100.0, 100.0}).ok());
  ASSERT_EQ(controller.selection().size(), 2U);
  const graphscore::Node* pasted_first =
      fixture.project.find_node(controller.selection()[0]);
  const graphscore::Node* pasted_second =
      fixture.project.find_node(controller.selection()[1]);
  ASSERT_NE(pasted_first, nullptr);
  ASSERT_NE(pasted_second, nullptr);
  EXPECT_EQ(pasted_first->name(), "First");
  ASSERT_TRUE(pasted_first->outputs()[0].destination().has_value());
  EXPECT_EQ(pasted_first->outputs()[0].destination()->node,
            pasted_second->id());
}

TEST(CanvasNodeOperationsTest, DeleteSelectedIsOneUndoableOperation) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);
  ASSERT_TRUE(controller.select(fixture.first));
  ASSERT_TRUE(controller.select(fixture.second, true));

  ASSERT_TRUE(controller.delete_selected().ok());
  EXPECT_TRUE(controller.selection().empty());
  EXPECT_EQ(fixture.project.nodes().size(), 1U);
  EXPECT_EQ(fixture.scene.nodes.size(), 1U);
  ASSERT_TRUE(fixture.history.undo(fixture.project).ok());
  const graphscore::Node* first = fixture.project.find_node(fixture.first);
  ASSERT_NE(first, nullptr);
  ASSERT_TRUE(
      first->find_output(fixture.first_output)->destination().has_value());
  EXPECT_EQ(first->find_output(fixture.first_output)->destination()->node,
            fixture.second);
  ASSERT_EQ(fixture.project.nodes().size(), 3U);
  EXPECT_EQ(fixture.project.nodes()[0].id(), fixture.first);
  EXPECT_EQ(fixture.project.nodes()[1].id(), fixture.second);
  EXPECT_EQ(fixture.project.nodes()[2].id(), fixture.other);
}

TEST(CanvasNodeOperationsTest, RejectsMoveWhenConnectorTopologyIsStale) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);
  ASSERT_TRUE(controller.select(fixture.first));
  ASSERT_TRUE(graphscore::Graph(fixture.project)
                  .disconnect(fixture.first, fixture.first_output)
                  .ok());

  EXPECT_FALSE(controller.begin_move(fixture.first, {}));
}

TEST(CanvasNodeOperationsTest, PasteAlignsTracksAddedAfterCopy) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);
  ASSERT_TRUE(controller.select(fixture.first));
  ASSERT_TRUE(controller.copy_selected().ok());
  const graphscore::StaffLayout layout =
      graphscore::StaffLayout::single_staff();
  const graphscore::TrackId track = *fixture.project.add_track(
      "New track", layout, *graphscore::MidiChannel::create(2));
  fixture.scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, fixture.metrics);

  ASSERT_TRUE(controller.paste().ok());
  ASSERT_EQ(controller.selection().size(), 1U);
  const graphscore::Node* pasted =
      fixture.project.find_node(controller.selection().front());
  ASSERT_NE(pasted, nullptr);
  const graphscore::TrackLane* lane = pasted->lane(track);
  ASSERT_NE(lane, nullptr);
  EXPECT_TRUE(lane->has_stave(layout.staves().front().id));
}

TEST(CanvasNodeOperationsTest, RejectsOverflowingDuplicateAndGroupMove) {
  OperationsFixture                          fixture;
  graphscore::CanvasNodeOperationsController controller(
      fixture.project, fixture.history, fixture.scene, fixture.metrics);
  ASSERT_TRUE(controller.select(fixture.first));
  ASSERT_TRUE(controller.select(fixture.second, true));
  EXPECT_EQ(
      controller.duplicate_selected({std::numeric_limits<double>::max(), 0.0})
          .code(),
      graphscore::ResultCode::kInvalidArgument);
  EXPECT_EQ(fixture.project.nodes().size(), 3U);

  ASSERT_TRUE(controller.begin_move(fixture.first, {}));
  EXPECT_FALSE(
      controller.update_move({std::numeric_limits<double>::max(), 0.0}));
  controller.cancel_move();
  EXPECT_EQ(fixture.scene.nodes[0].position,
            (graphscore::GraphPosition{0.0, 0.0}));
  EXPECT_EQ(fixture.scene.nodes[1].position,
            (graphscore::GraphPosition{500.0, 0.0}));
}

}  // namespace
