// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>
#include <vector>

namespace {

class SelectionMetrics final : public graphscore::GlyphMetrics {
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

[[nodiscard]] graphscore::GraphPosition world_center(
    const graphscore::CanvasNodeNotation& node,
    const graphscore::NotationRect&       bounds) {
  return {node.position.x + bounds.x + bounds.width / 2.0,
          node.position.y + bounds.y + bounds.height / 2.0};
}

struct CanvasFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Selection"};
  graphscore::NodeId  source      = project.add_node("Source");
  graphscore::NodeId  destination = project.add_node("Destination");
  graphscore::ConnectorId output = project.find_node(source)->add_output("Out");
  graphscore::ConnectorId input =
      project.find_node(destination)->add_input("In");
  SelectionMetrics                metrics;
  graphscore::NotePaletteState    palette;
  graphscore::CanvasNotationScene scene;

  CanvasFixture() {
    project.find_node(source)->set_position({100.0, 200.0});
    project.find_node(destination)->set_position({700.0, 200.0});
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(source, output, destination, input)
                    .ok());
    scene = graphscore::Canvas{}.layout_nodes(project, metrics);
  }
};

TEST(CanvasSingleClickSelectionTest, SelectsPortsControlsAndNodeBackground) {
  CanvasFixture fixture;
  ASSERT_EQ(fixture.scene.nodes.size(), 2U);
  const auto& source      = fixture.scene.nodes.front();
  const auto& destination = fixture.scene.nodes.back();
  ASSERT_FALSE(source.ports.empty());
  ASSERT_FALSE(destination.ports.empty());
  for (const auto* node : {&source, &destination}) {
    const auto port = graphscore::canvas_single_click_selection(
        fixture.project, fixture.scene, fixture.palette,
        world_center(*node, node->ports.front().bounds), 4.0);
    ASSERT_TRUE(port.has_value());
    EXPECT_EQ(std::get<graphscore::CanvasPortSelection>(*port),
              (graphscore::CanvasPortSelection{node->node_id,
                                               node->ports.front().connector_id,
                                               node->ports.front().direction}));
  }

  const std::array controls{&source.header.freeform_notes_button,
                            &source.header.tempo_lane_button,
                            &source.header.play_button};
  for (const graphscore::CanvasNodeHeaderButton* button : controls) {
    const auto control = graphscore::canvas_single_click_selection(
        fixture.project, fixture.scene, fixture.palette,
        world_center(source, button->bounds), 4.0);
    ASSERT_TRUE(control.has_value());
    EXPECT_EQ(
        std::get<graphscore::CanvasControlSelection>(*control),
        (graphscore::CanvasControlSelection{source.node_id, button->action}));
  }

  const graphscore::GraphPosition header_background{source.position.x + 8.0,
                                                    source.position.y + 8.0};
  const auto node = graphscore::canvas_single_click_selection(
      fixture.project, fixture.scene, fixture.palette, header_background, 4.0);
  ASSERT_TRUE(node.has_value());
  EXPECT_EQ(std::get<graphscore::CanvasNodeSelection>(*node),
            (graphscore::CanvasNodeSelection{source.node_id}));
}

TEST(CanvasSingleClickSelectionTest, SelectsConnectorPathAndExactSegment) {
  CanvasFixture fixture;
  ASSERT_EQ(fixture.scene.connectors.size(), 1U);
  const auto& connector = fixture.scene.connectors.front();
  ASSERT_GE(connector.route_points.size(), 4U);
  constexpr std::size_t kInteriorSegment = 1U;
  const auto            first  = connector.route_points[kInteriorSegment];
  const auto            second = connector.route_points[kInteriorSegment + 1U];
  const graphscore::GraphPosition midpoint{(first.x + second.x) / 2.0,
                                           (first.y + second.y) / 2.0};

  const auto selection = graphscore::canvas_single_click_selection(
      fixture.project, fixture.scene, fixture.palette, midpoint, 4.0);

  ASSERT_TRUE(selection.has_value());
  EXPECT_EQ(std::get<graphscore::CanvasConnectorPathSelection>(*selection),
            (graphscore::CanvasConnectorPathSelection{
                {fixture.source, fixture.output}, kInteriorSegment}));
}

TEST(CanvasSingleClickSelectionTest, NodesOccludeConnectorPaths) {
  CanvasFixture fixture;
  auto&         connector = fixture.scene.connectors.front();
  const auto&   source    = fixture.scene.nodes.front();
  connector.route_points  = {{source.position.x, source.position.y + 8.0},
                             {source.position.x + source.geometry.bounds.width,
                              source.position.y + 8.0}};

  const graphscore::GraphPosition pointer{source.position.x + 8.0,
                                          source.position.y + 8.0};
  const auto selection = graphscore::canvas_single_click_selection(
      fixture.project, fixture.scene, fixture.palette, pointer, 4.0);

  ASSERT_TRUE(selection.has_value());
  EXPECT_TRUE(
      std::holds_alternative<graphscore::CanvasNodeSelection>(*selection));
}

TEST(CanvasSingleClickSelectionTest, SelectsNotationUsingNodeLocalCoordinates) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Notation"};
  const graphscore::StaffLayout staff_layout =
      graphscore::StaffLayout::single_staff();
  const graphscore::TrackId track = *project.add_track(
      "Track", staff_layout, *graphscore::MidiChannel::create(0));
  const graphscore::StaveDefinition stave   = staff_layout.staves().front();
  const graphscore::NodeId          node_id = project.add_node("Music");
  graphscore::Node* const           node    = project.find_node(node_id);
  node->set_position({-250.0, 375.0});
  node->lane(track)->ensure_stave(stave.id);
  auto timeline = graphscore::NodeTimeline::create(
      std::vector<graphscore::Measure>{
          {*graphscore::TimeSignature::create(4, 4),
           graphscore::KeySignature{}}},
      {stave});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));
  graphscore::VoiceContent& voice =
      node->lane(track)->stave(stave.id)->voice(*graphscore::Voice::create(1));
  ASSERT_TRUE(
      voice
          .append(graphscore::make_note(
              *graphscore::SpelledPitch::create(graphscore::Letter::kC, 4),
              *graphscore::Duration::create(graphscore::NoteValue::kQuarter,
                                            0)))
          .ok());
  ASSERT_TRUE(voice.normalize(node->timeline()->node_end()).ok());
  const SelectionMetrics metrics;
  const auto scene = graphscore::Canvas{}.layout_nodes(project, metrics);
  ASSERT_EQ(scene.nodes.size(), 1U);
  ASSERT_TRUE(scene.nodes.front().layout.has_value());
  const auto& layout = *scene.nodes.front().layout;
  const auto  notehead =
      std::ranges::find(layout.hit_regions, graphscore::HitRole::kNotehead,
                        &graphscore::HitRegion::role);
  ASSERT_NE(notehead, layout.hit_regions.end());
  const graphscore::GraphPosition pointer{
      scene.nodes.front().position.x +
          scene.nodes.front().geometry.notation_bounds.x + notehead->bounds.x +
          notehead->bounds.width / 2.0,
      scene.nodes.front().position.y +
          scene.nodes.front().geometry.notation_bounds.y + notehead->bounds.y +
          notehead->bounds.height / 2.0};

  const auto selection = graphscore::canvas_single_click_selection(
      project, scene, graphscore::NotePaletteState{}, pointer, 4.0);

  ASSERT_TRUE(selection.has_value());
  const auto& notation =
      std::get<graphscore::CanvasNotationSelection>(*selection);
  EXPECT_EQ(notation.node_id, node_id);
  EXPECT_TRUE(
      std::holds_alternative<graphscore::NoteheadSet>(notation.selection));
}

TEST(CanvasSingleClickSelectionTest, RejectsMissesAndInvalidTolerance) {
  CanvasFixture fixture;
  EXPECT_FALSE(graphscore::canvas_single_click_selection(
                   fixture.project, fixture.scene, fixture.palette,
                   {-1000.0, -1000.0}, 4.0)
                   .has_value());
  EXPECT_FALSE(graphscore::canvas_single_click_selection(
                   fixture.project, fixture.scene, fixture.palette, {}, -1.0)
                   .has_value());
  EXPECT_FALSE(graphscore::canvas_single_click_selection(
                   fixture.project, fixture.scene, fixture.palette,
                   {std::numeric_limits<double>::infinity(), 0.0}, 4.0)
                   .has_value());
}

TEST(CanvasDoubleClickPlaybackActionTest,
     RequestsActionForTheSingleClickConnectorIdentity) {
  CanvasFixture fixture;
  ASSERT_EQ(fixture.scene.connectors.size(), 1U);
  const auto& connector = fixture.scene.connectors.front();
  ASSERT_GE(connector.route_points.size(), 4U);
  constexpr std::size_t kInteriorSegment = 1U;
  const auto            first  = connector.route_points[kInteriorSegment];
  const auto            second = connector.route_points[kInteriorSegment + 1U];
  const graphscore::GraphPosition midpoint{(first.x + second.x) / 2.0,
                                           (first.y + second.y) / 2.0};

  const auto selection = graphscore::canvas_single_click_selection(
      fixture.project, fixture.scene, fixture.palette, midpoint, 4.0);
  const auto request = graphscore::canvas_double_click_playback_action_request(
      fixture.project, fixture.scene, fixture.palette, midpoint, 4.0);

  ASSERT_TRUE(selection.has_value());
  ASSERT_TRUE(request.has_value());
  const auto& selected_path =
      std::get<graphscore::CanvasConnectorPathSelection>(*selection);
  EXPECT_EQ(request->connector, selected_path.connector);
  EXPECT_EQ(request->connector, (graphscore::CanvasConnectorSelection{
                                    fixture.source, fixture.output}));
}

TEST(CanvasDoubleClickPlaybackActionTest,
     DoesNotRequestActionsForOtherSingleClickTargets) {
  CanvasFixture fixture;
  ASSERT_EQ(fixture.scene.nodes.size(), 2U);
  const auto& source = fixture.scene.nodes.front();

  EXPECT_FALSE(graphscore::canvas_double_click_playback_action_request(
                   fixture.project, fixture.scene, fixture.palette,
                   world_center(source, source.header.play_button.bounds), 4.0)
                   .has_value());
  EXPECT_FALSE(graphscore::canvas_double_click_playback_action_request(
                   fixture.project, fixture.scene, fixture.palette,
                   {source.position.x + 8.0, source.position.y + 8.0}, 4.0)
                   .has_value());
  EXPECT_FALSE(graphscore::canvas_double_click_playback_action_request(
                   fixture.project, fixture.scene, fixture.palette,
                   {-1000.0, -1000.0}, 4.0)
                   .has_value());
}

TEST(CanvasActionCirclePlaybackActionTest,
     RetainsSmallVisualAtTheDestinationWithLargeInteractionTarget) {
  CanvasFixture fixture;
  ASSERT_EQ(fixture.scene.connectors.size(), 1U);
  const auto& connector = fixture.scene.connectors.front();

  EXPECT_EQ(connector.action_circle.center, connector.destination_leg.outer);
  EXPECT_EQ(graphscore::CanvasConnectorActionCircle::kDiameter, 16.0);
  EXPECT_EQ(graphscore::CanvasConnectorActionCircle::kInteractionDiameter,
            44.0);
  EXPECT_GT(graphscore::CanvasConnectorActionCircle::kInteractionDiameter,
            graphscore::CanvasConnectorActionCircle::kDiameter);
}

TEST(CanvasActionCirclePlaybackActionTest,
     RequestsTheSameConnectorActionOutsideTheVisualCircle) {
  CanvasFixture fixture;
  ASSERT_EQ(fixture.scene.connectors.size(), 1U);
  const auto& connector = fixture.scene.connectors.front();
  ASSERT_GE(connector.route_points.size(), 4U);
  constexpr std::size_t kInteriorSegment = 1U;
  const auto            first  = connector.route_points[kInteriorSegment];
  const auto            second = connector.route_points[kInteriorSegment + 1U];
  const graphscore::GraphPosition path_midpoint{(first.x + second.x) / 2.0,
                                                (first.y + second.y) / 2.0};
  const graphscore::GraphPosition expanded_target{
      connector.action_circle.center.x,
      connector.action_circle.center.y +
          graphscore::CanvasConnectorActionCircle::kDiameter / 2.0 + 1.0};

  const auto double_click =
      graphscore::canvas_double_click_playback_action_request(
          fixture.project, fixture.scene, fixture.palette, path_midpoint, 4.0);
  const auto action_circle =
      graphscore::canvas_action_circle_playback_action_request(fixture.scene,
                                                               expanded_target);

  ASSERT_TRUE(double_click.has_value());
  ASSERT_TRUE(action_circle.has_value());
  EXPECT_EQ(*action_circle, *double_click);
  EXPECT_EQ(action_circle->connector, (graphscore::CanvasConnectorSelection{
                                          fixture.source, fixture.output}));
}

TEST(CanvasActionCirclePlaybackActionTest, RejectsMissesAndNonFinitePointers) {
  CanvasFixture fixture;
  ASSERT_EQ(fixture.scene.connectors.size(), 1U);
  const auto center = fixture.scene.connectors.front().action_circle.center;
  const graphscore::GraphPosition miss{
      center.x +
          graphscore::CanvasConnectorActionCircle::kInteractionDiameter / 2.0 +
          1.0,
      center.y};

  EXPECT_FALSE(graphscore::canvas_action_circle_playback_action_request(
                   fixture.scene, miss)
                   .has_value());
  EXPECT_FALSE(
      graphscore::canvas_action_circle_playback_action_request(
          fixture.scene, {std::numeric_limits<double>::infinity(), center.y})
          .has_value());
}

}  // namespace
