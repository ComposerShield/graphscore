// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

class GeometryMetrics final : public graphscore::GlyphMetrics {
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

[[nodiscard]] graphscore::Measure measure() {
  return {*graphscore::TimeSignature::create(4, 4), graphscore::KeySignature{}};
}

struct GeometryFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Geometry"};
  graphscore::TrackId single_track;
  graphscore::TrackId grand_track;
  graphscore::NodeId  node_id;

  explicit GeometryFixture(std::size_t measure_count = 1) {
    single_track =
        *project.add_track("Single", graphscore::StaffLayout::single_staff(),
                           *graphscore::MidiChannel::create(0));
    grand_track =
        *project.add_track("Grand", graphscore::StaffLayout::grand_staff(),
                           *graphscore::MidiChannel::create(1));
    node_id = project.add_node("Node");

    graphscore::Node* const node = project.find_node(node_id);
    node->set_position({125.0, -75.0});
    std::vector<graphscore::StaveDefinition> staves;
    for (const graphscore::Track& track : project.active_tracks()) {
      for (const graphscore::StaveDefinition& stave : track.layout().staves()) {
        node->lane(track.id())->ensure_stave(stave.id);
        staves.push_back(stave);
      }
    }
    auto timeline = graphscore::NodeTimeline::create(
        std::vector<graphscore::Measure>(measure_count, measure()), staves);
    EXPECT_TRUE(timeline.has_value());
    node->set_timeline(std::move(*timeline));
  }
};

TEST(CanvasNodeGeometryTest, PartitionsHeaderAndCompleteNotationWithoutCrop) {
  GeometryFixture                       fixture;
  const GeometryMetrics                 metrics;
  const graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, metrics);

  ASSERT_EQ(scene.nodes.size(), 1U);
  const graphscore::CanvasNodeNotation& node = scene.nodes[0];
  ASSERT_TRUE(node.layout.has_value());
  const double expected_width = std::max(
      graphscore::CanvasNodeGeometry::kMinimumWidth, node.layout->bounds.width);
  EXPECT_EQ(node.geometry.bounds.origin, node.position);
  EXPECT_DOUBLE_EQ(node.geometry.bounds.width, expected_width);
  EXPECT_DOUBLE_EQ(node.geometry.bounds.height,
                   graphscore::CanvasNodeGeometry::kHeaderHeight +
                       node.layout->bounds.height);
  EXPECT_EQ(node.geometry.header_bounds,
            (graphscore::NotationRect{
                0.0, 0.0, expected_width,
                graphscore::CanvasNodeGeometry::kHeaderHeight}));
  EXPECT_EQ(node.geometry.content_bounds,
            (graphscore::NotationRect{
                0.0, graphscore::CanvasNodeGeometry::kHeaderHeight,
                expected_width, node.layout->bounds.height}));
  EXPECT_EQ(node.geometry.notation_bounds,
            (graphscore::NotationRect{
                0.0, graphscore::CanvasNodeGeometry::kHeaderHeight,
                node.layout->bounds.width, node.layout->bounds.height}));
}

TEST(CanvasNodeGeometryTest, ExpandsForAContentWiderThanTheRequestedSystem) {
  GeometryFixture                   fixture;
  graphscore::NotationLayoutOptions options;
  options.system_width          = 200.0;
  options.minimum_measure_width = 500.0;
  const GeometryMetrics metrics;

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, metrics, options);

  ASSERT_EQ(scene.nodes.size(), 1U);
  ASSERT_TRUE(scene.nodes[0].layout.has_value());
  EXPECT_GT(scene.nodes[0].layout->bounds.width, options.system_width);
  EXPECT_DOUBLE_EQ(scene.nodes[0].geometry.bounds.width,
                   scene.nodes[0].layout->bounds.width);
  EXPECT_DOUBLE_EQ(scene.nodes[0].geometry.notation_bounds.width,
                   scene.nodes[0].layout->bounds.width);
}

TEST(CanvasNodeGeometryTest, HeightTracksWrappedMeasuresAndActiveStaves) {
  GeometryFixture                   fixture(5);
  graphscore::NotationLayoutOptions options;
  options.system_width          = 320.0;
  options.minimum_measure_width = 120.0;
  const GeometryMetrics metrics;

  const auto full =
      graphscore::Canvas{}.layout_nodes(fixture.project, metrics, options);
  ASSERT_TRUE(full.nodes[0].layout.has_value());
  ASSERT_GT(full.nodes[0].layout->systems.size(), 1U);

  ASSERT_TRUE(fixture.project.archive_track(fixture.grand_track).ok());
  const auto fewer_staves =
      graphscore::Canvas{}.layout_nodes(fixture.project, metrics, options);
  ASSERT_TRUE(fewer_staves.nodes[0].layout.has_value());
  EXPECT_LT(fewer_staves.nodes[0].geometry.bounds.height,
            full.nodes[0].geometry.bounds.height);
  EXPECT_DOUBLE_EQ(full.nodes[0].geometry.content_bounds.height,
                   full.nodes[0].layout->bounds.height);
}

TEST(CanvasNodeGeometryTest, FailedLayoutUsesDeterministicFallbackBody) {
  GeometryFixture fixture;
  fixture.project.find_node(fixture.node_id)->clear_timeline();
  const GeometryMetrics metrics;

  const auto scene =
      graphscore::Canvas{}.layout_nodes(fixture.project, metrics);

  ASSERT_EQ(scene.nodes.size(), 1U);
  const graphscore::CanvasNodeNotation& node = scene.nodes[0];
  EXPECT_FALSE(node.layout.has_value());
  EXPECT_DOUBLE_EQ(node.geometry.bounds.width,
                   graphscore::CanvasNodeGeometry::kMinimumWidth);
  EXPECT_DOUBLE_EQ(node.geometry.bounds.height,
                   graphscore::CanvasNodeGeometry::kHeaderHeight +
                       graphscore::CanvasNodeGeometry::kFallbackContentHeight);
  EXPECT_DOUBLE_EQ(node.geometry.content_bounds.height,
                   graphscore::CanvasNodeGeometry::kFallbackContentHeight);
  EXPECT_DOUBLE_EQ(node.geometry.notation_bounds.width, 0.0);
  EXPECT_DOUBLE_EQ(node.geometry.notation_bounds.height, 0.0);
}

}  // namespace
