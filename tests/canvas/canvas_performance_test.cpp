// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

class PerformanceMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return {{0.0, -staff_space, staff_space, staff_space * 2.0}, staff_space};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

constexpr std::size_t kNodeCount   = 1000U;
constexpr std::size_t kFrameCount  = 120U;
constexpr auto        kFrameBudget = std::chrono::duration<double>(1.0 / 60.0);

template <typename Operation>
[[nodiscard]] std::chrono::duration<double> measure_frames(
    Operation&& operation) {
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t frame = 0; frame < kFrameCount; ++frame) {
    operation(frame);
  }
  return std::chrono::steady_clock::now() - start;
}

void expect_within_frame_budget(std::chrono::duration<double> elapsed,
                                const char*                   operation) {
  const auto budget = kFrameBudget * kFrameCount;
  EXPECT_LT(elapsed, budget)
      << operation << " averaged "
      << elapsed.count() * 1000.0 / static_cast<double>(kFrameCount)
      << " ms per frame; the 60 fps budget is "
      << std::chrono::duration<double, std::milli>(kFrameBudget).count()
      << " ms";
}

struct RepresentativeCanvasFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Performance"};
  PerformanceMetrics  metrics;
  std::vector<graphscore::NodeId> node_ids;
  graphscore::ConnectorId         output;
  graphscore::CanvasNotationScene scene;

  RepresentativeCanvasFixture() {
    const graphscore::StaffLayout staff_layout =
        graphscore::StaffLayout::single_staff();
    const graphscore::TrackId track = *project.add_track(
        "Track", staff_layout, *graphscore::MidiChannel::create(0));
    const graphscore::StaveDefinition stave = staff_layout.staves().front();
    const graphscore::Measure measure{*graphscore::TimeSignature::create(4, 4),
                                      graphscore::KeySignature{}};
    const auto                timeline = graphscore::NodeTimeline::create(
        std::vector<graphscore::Measure>{measure},
        std::vector<graphscore::StaveDefinition>{stave});
    if (!timeline.has_value()) {
      ADD_FAILURE() << "representative timeline creation failed";
      return;
    }

    node_ids.reserve(kNodeCount);
    for (std::size_t index = 0; index < kNodeCount; ++index) {
      const graphscore::NodeId node_id =
          project.add_node("Node " + std::to_string(index));
      node_ids.push_back(node_id);
      graphscore::Node* const node = project.find_node(node_id);
      node->set_position({static_cast<double>(index % 40U) * 500.0,
                          static_cast<double>(index / 40U) * 500.0});
      node->lane(track)->ensure_stave(stave.id);
      node->set_timeline(*timeline);
    }

    graphscore::Node* const source      = project.find_node(node_ids[0]);
    graphscore::Node* const destination = project.find_node(node_ids[1]);
    output                              = source->add_output("Out");
    const graphscore::ConnectorId input = destination->add_input("In");
    EXPECT_TRUE(source->find_output(output)->route().set_custom_route({}).ok());
    EXPECT_TRUE(graphscore::Graph(project)
                    .connect(node_ids[0], output, node_ids[1], input)
                    .ok());
    scene = graphscore::Canvas{}.layout_nodes(project, metrics);
  }
};

TEST(CanvasPerformanceTest,
     InteractiveOperationsMeetSixtyFpsAtOneThousandNodes) {
  RepresentativeCanvasFixture fixture;
  ASSERT_EQ(fixture.scene.nodes.size(), kNodeCount);
  ASSERT_TRUE(fixture.scene.complete());
  ASSERT_EQ(fixture.scene.connectors.size(), 1U);
  ASSERT_GE(fixture.scene.connectors.front().route_points.size(), 3U);

  graphscore::ViewportTransform          transform;
  graphscore::CanvasNavigationController navigation(transform);
  bool                                   navigation_succeeded = true;
  const auto pan_elapsed = measure_frames([&](std::size_t frame) {
    const double direction = frame % 2U == 0U ? 1.0 : -1.0;
    navigation_succeeded =
        navigation.pan({direction * 3.0, direction * -2.0}) &&
        navigation_succeeded;
  });
  ASSERT_TRUE(navigation_succeeded);
  expect_within_frame_budget(pan_elapsed, "pan");

  const auto zoom_elapsed = measure_frames([&](std::size_t frame) {
    navigation_succeeded =
        (frame % 2U == 0U ? navigation.zoom_in({400.0, 300.0})
                          : navigation.zoom_out({400.0, 300.0})) &&
        navigation_succeeded;
  });
  ASSERT_TRUE(navigation_succeeded);
  expect_within_frame_budget(zoom_elapsed, "zoom");

  graphscore::NotePaletteState    palette;
  const graphscore::GraphPosition selection_point{
      fixture.scene.nodes.front().position.x + 8.0,
      fixture.scene.nodes.front().position.y + 8.0};
  std::size_t selections        = 0U;
  const auto  selection_elapsed = measure_frames([&](std::size_t /*frame*/) {
    selections += static_cast<std::size_t>(
        graphscore::canvas_single_click_selection(
            fixture.project, fixture.scene, palette, selection_point, 4.0)
            .has_value());
  });
  ASSERT_EQ(selections, kFrameCount);
  expect_within_frame_budget(selection_elapsed, "selection");

  graphscore::CommandHistory           history;
  graphscore::CanvasNodeDragController node_drag(fixture.project, history,
                                                 fixture.scene);
  ASSERT_TRUE(node_drag.begin(fixture.node_ids.front(), {0.0, 0.0}));
  bool       node_drag_succeeded = true;
  const auto node_drag_elapsed   = measure_frames([&](std::size_t frame) {
    const double offset = frame % 2U == 0U ? 4.0 : 8.0;
    node_drag_succeeded =
        node_drag.update({offset, offset}) && node_drag_succeeded;
  });
  ASSERT_TRUE(node_drag_succeeded);
  expect_within_frame_budget(node_drag_elapsed, "node drag");
  node_drag.cancel();

  graphscore::CanvasConnectorSegmentDragController connector_drag(
      fixture.project, history, fixture.scene);
  const graphscore::GraphPosition segment_start =
      fixture.scene.connectors.front().route_points[1U];
  ASSERT_TRUE(connector_drag.begin(fixture.node_ids[0], fixture.output, 1U,
                                   segment_start));
  bool       connector_drag_succeeded = true;
  const auto connector_drag_elapsed   = measure_frames([&](std::size_t frame) {
    const double offset = frame % 2U == 0U ? 20.0 : 40.0;
    connector_drag_succeeded =
        connector_drag.update({segment_start.x, segment_start.y + offset}) &&
        connector_drag_succeeded;
  });
  ASSERT_TRUE(connector_drag_succeeded);
  expect_within_frame_budget(connector_drag_elapsed, "connector segment drag");
  connector_drag.cancel();
}

TEST(CanvasPerformanceTest, OffscreenWorkIsCulledAcrossEveryRetainedItemClass) {
  constexpr std::array kKinds{
      graphscore::CanvasItemKind::kNode,
      graphscore::CanvasItemKind::kLabel,
      graphscore::CanvasItemKind::kControl,
      graphscore::CanvasItemKind::kConnectorSegment,
      graphscore::CanvasItemKind::kHitRegion,
  };
  graphscore::CanvasScene scene;
  for (std::size_t kind_index = 0; kind_index < kKinds.size(); ++kind_index) {
    ASSERT_TRUE(scene.insert({
        {kKinds[kind_index], 0U},
        {{10.0 + static_cast<double>(kind_index) * 10.0, 10.0}, 4.0, 4.0},
    }));
    for (std::uint64_t item_index = 1U; item_index < kNodeCount; ++item_index) {
      ASSERT_TRUE(scene.insert({
          {kKinds[kind_index], item_index},
          {{100000.0 + static_cast<double>(item_index) * 20.0,
            100000.0 + static_cast<double>(kind_index) * 20.0},
           4.0,
           4.0},
      }));
    }
  }

  graphscore::ViewportTransform transform;
  const auto                    result =
      scene.index().query_viewport(transform, 100.0, 100.0, 8.0);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->items.size(), kKinds.size());
  EXPECT_LT(result->statistics.candidates_tested, 20U);
  EXPECT_LT(result->statistics.nodes_visited, 100U);
  EXPECT_EQ(scene.index().size(), kKinds.size() * kNodeCount);
}

}  // namespace
