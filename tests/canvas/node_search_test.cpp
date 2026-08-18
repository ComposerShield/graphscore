// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string>

namespace {

class SearchMetrics final : public graphscore::GlyphMetrics {
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

[[nodiscard]] graphscore::NodeId known_node_id() {
  return graphscore::NodeId(graphscore::Uuid(std::array<std::uint8_t, 16>{
      0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x40, 0x11, 0x82, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88}));
}

[[nodiscard]] std::string uppercase(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char value) {
    return static_cast<char>(std::toupper(value));
  });
  return text;
}

TEST(CanvasNodeSearchTest, MatchesNamesAndCanonicalUuidsInProjectOrder) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Search"};
  const graphscore::NodeId first = project.add_node("Boss Intro");
  const graphscore::NodeId known = known_node_id();
  ASSERT_TRUE(project.add_node_with_id(known, "Quiet bridge").ok());
  const graphscore::NodeId last = project.add_node("Boss Coda");

  const auto names = graphscore::canvas_search_nodes(project, "BOSS");
  ASSERT_EQ(names.size(), 2U);
  EXPECT_EQ(names[0].node_id, first);
  EXPECT_EQ(names[0].name, "Boss Intro");
  EXPECT_EQ(names[1].node_id, last);

  const std::string uuid_query = uppercase(known.to_string().substr(0U, 13U));
  const auto uuids = graphscore::canvas_search_nodes(project, uuid_query);
  ASSERT_EQ(uuids.size(), 1U);
  EXPECT_EQ(uuids.front(), (graphscore::CanvasNodeSearchResult{
                               known, "Quiet bridge", known.to_string()}));
}

TEST(CanvasNodeSearchTest, EmptyQueryReturnsAllAndNoMatchReturnsNone) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Search"};
  const graphscore::NodeId first  = project.add_node("First");
  const graphscore::NodeId second = project.add_node("Second");

  const auto all = graphscore::canvas_search_nodes(project, "");
  ASSERT_EQ(all.size(), 2U);
  EXPECT_EQ(all[0].node_id, first);
  EXPECT_EQ(all[1].node_id, second);
  EXPECT_TRUE(graphscore::canvas_search_nodes(project, "missing").empty());
}

TEST(CanvasNodeSearchTest, FocusCentersRetainedNodeAndPreservesZoom) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Search"};
  const graphscore::NodeId node_id = project.add_node("Far away");
  project.find_node(node_id)->set_position({1000.0, -500.0});
  SearchMetrics                         metrics;
  const graphscore::CanvasNotationScene scene =
      graphscore::Canvas{}.layout_nodes(project, metrics);
  graphscore::ViewportTransform          transform;
  constexpr graphscore::ViewportPosition kViewportFocus{400.0, 300.0};
  ASSERT_TRUE(transform.zoom_to(2.0, {}));

  ASSERT_TRUE(
      graphscore::canvas_focus_node(scene, node_id, kViewportFocus, transform));

  const auto focused_world = transform.to_world(kViewportFocus);
  ASSERT_TRUE(focused_world.has_value());
  EXPECT_EQ(
      *focused_world,
      (graphscore::GraphPosition{
          1000.0 + graphscore::CanvasNodeGeometry::kMinimumWidth / 2.0,
          -500.0 + (graphscore::CanvasNodeGeometry::kHeaderHeight +
                    graphscore::CanvasNodeGeometry::kFallbackContentHeight) /
                       2.0}));
  EXPECT_DOUBLE_EQ(transform.zoom(), 2.0);
}

TEST(CanvasNodeSearchTest, FocusRejectsMissingOrInvalidTargetsAtomically) {
  graphscore::CanvasNotationScene scene;
  const graphscore::NodeId        node_id = known_node_id();
  scene.nodes.push_back(graphscore::CanvasNodeNotation{
      node_id,
      {},
      {},
      graphscore::CanvasNodeGeometry{
          {{0.0, 0.0}, std::numeric_limits<double>::infinity(), 10.0},
          {},
          {},
          {}},
      {},
      graphscore::NotationLayoutError::kNone,
      std::nullopt});
  graphscore::ViewportTransform transform;
  ASSERT_TRUE(transform.set_anchor({5.0, 6.0}, {7.0, 8.0}));
  const graphscore::GraphPosition    world_before = transform.world_anchor();
  const graphscore::ViewportPosition view_before  = transform.viewport_anchor();

  EXPECT_FALSE(graphscore::canvas_focus_node(scene, node_id, {}, transform));
  EXPECT_FALSE(graphscore::canvas_focus_node(
      scene, graphscore::NodeId::generate(), {}, transform));
  EXPECT_FALSE(graphscore::canvas_focus_node(
      scene, node_id, {std::numeric_limits<double>::infinity(), 0.0},
      transform));
  EXPECT_EQ(transform.world_anchor(), world_before);
  EXPECT_EQ(transform.viewport_anchor(), view_before);
}

}  // namespace
