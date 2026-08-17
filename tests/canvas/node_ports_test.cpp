// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <set>
#include <string>

namespace {

class PortMetrics final : public graphscore::GlyphMetrics {
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

TEST(CanvasNodePortTest, RetainsEveryNamedPortInStableDomainOrder) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Ports"};
  const graphscore::NodeId node_id            = project.add_node("Node");
  graphscore::Node* const  node               = project.find_node(node_id);
  const graphscore::ConnectorId first_input   = node->add_input("Entry α");
  const graphscore::ConnectorId second_input  = node->add_input("");
  const graphscore::ConnectorId first_output  = node->add_output("Verse");
  const graphscore::ConnectorId second_output = node->add_output("Chorus");

  const auto scene = graphscore::Canvas{}.layout_nodes(project, PortMetrics{});

  ASSERT_EQ(scene.nodes.size(), 1U);
  const auto& ports = scene.nodes[0].ports;
  ASSERT_EQ(ports.size(), 4U);
  EXPECT_EQ(ports[0].connector_id, first_input);
  EXPECT_EQ(ports[1].connector_id, second_input);
  EXPECT_EQ(ports[2].connector_id, first_output);
  EXPECT_EQ(ports[3].connector_id, second_output);
  EXPECT_EQ(ports[0].name, "Entry α");
  EXPECT_EQ(ports[0].accessibility_label, "Entry α, input port");
  EXPECT_EQ(ports[1].accessibility_label, "Unnamed input port");
  EXPECT_EQ(ports[2].accessibility_label, "Verse, output port");
  EXPECT_EQ(ports[0].direction, graphscore::CanvasPortDirection::kInput);
  EXPECT_EQ(ports[2].direction, graphscore::CanvasPortDirection::kOutput);
  EXPECT_DOUBLE_EQ(ports[0].bounds.x,
                   -graphscore::CanvasNodePort::kDiameter / 2.0);
  EXPECT_DOUBLE_EQ(ports[2].bounds.x,
                   scene.nodes[0].geometry.bounds.width -
                       graphscore::CanvasNodePort::kDiameter / 2.0);
}

TEST(CanvasNodePortTest, RepresentsArbitraryCountsWithUniqueStableIdentities) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Many ports"};
  const graphscore::NodeId node_id    = project.add_node("Node");
  graphscore::Node* const  node       = project.find_node(node_id);
  constexpr std::size_t    kPortCount = 128;
  for (std::size_t index = 0; index < kPortCount; ++index) {
    static_cast<void>(node->add_input("Input " + std::to_string(index)));
    static_cast<void>(node->add_output("Output " + std::to_string(index)));
  }

  const auto scene = graphscore::Canvas{}.layout_nodes(project, PortMetrics{});

  ASSERT_EQ(scene.nodes[0].ports.size(), kPortCount * 2U);
  std::set<std::string> accessibility_ids;
  for (const graphscore::CanvasNodePort& port : scene.nodes[0].ports) {
    EXPECT_TRUE(accessibility_ids.insert(port.accessibility_id).second);
    EXPECT_TRUE(std::isfinite(port.bounds.x));
    EXPECT_TRUE(std::isfinite(port.bounds.y));
  }
}

TEST(CanvasNodePortTest, ConnectedLegsAttachToTheirSpecificPortPositions) {
  graphscore::Project      project{graphscore::ProjectId::generate(), "Edges"};
  const graphscore::NodeId source_id = project.add_node("Source");
  const graphscore::NodeId target_id = project.add_node("Target");
  graphscore::Node* const  source    = project.find_node(source_id);
  graphscore::Node* const  target    = project.find_node(target_id);
  source->set_position({10.0, 20.0});
  target->set_position({500.0, 40.0});
  static_cast<void>(source->add_output("First"));
  const graphscore::ConnectorId connected_output = source->add_output("Second");
  static_cast<void>(target->add_input("First"));
  const graphscore::ConnectorId connected_input = target->add_input("Second");
  ASSERT_TRUE(
      graphscore::Graph(project)
          .connect(source_id, connected_output, target_id, connected_input)
          .ok());

  const auto scene = graphscore::Canvas{}.layout_nodes(project, PortMetrics{});

  ASSERT_EQ(scene.connectors.size(), 1U);
  const double source_center_y = scene.nodes[0].ports[1].bounds.y +
                                 graphscore::CanvasNodePort::kDiameter / 2.0;
  const double target_center_y = scene.nodes[1].ports[1].bounds.y +
                                 graphscore::CanvasNodePort::kDiameter / 2.0;
  EXPECT_EQ(scene.connectors[0].source_leg.attachment,
            (graphscore::GraphPosition{
                source->position().x + scene.nodes[0].geometry.bounds.width,
                source->position().y + source_center_y}));
  EXPECT_EQ(scene.connectors[0].destination_leg.attachment,
            (graphscore::GraphPosition{
                target->position().x, target->position().y + target_center_y}));
}

}  // namespace
