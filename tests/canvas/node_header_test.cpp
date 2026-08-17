// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class HeaderMetrics final : public graphscore::GlyphMetrics {
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

[[nodiscard]] graphscore::NodeTimeline timeline_with_tempo(
    const graphscore::StaveDefinition& stave) {
  std::vector<graphscore::Measure> measures{
      {*graphscore::TimeSignature::create(4, 4), graphscore::KeySignature{}}};
  auto timeline = graphscore::NodeTimeline::create(measures, {stave});
  EXPECT_TRUE(timeline.has_value());
  const graphscore::TempoPoint point{
      graphscore::Rational{},
      *graphscore::Tempo::create(graphscore::Rational(120),
                                 graphscore::NoteValue::kQuarter),
      graphscore::TempoSegmentKind::kStep};
  EXPECT_TRUE(timeline->set_tempo({point}).ok());
  return std::move(*timeline);
}

struct HeaderFixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Headers"};
  graphscore::TrackId track_id;
  graphscore::StaveDefinition stave;
  graphscore::NodeId          node_id;

  HeaderFixture() {
    const graphscore::StaffLayout layout =
        graphscore::StaffLayout::single_staff();
    const graphscore::MidiChannel channel = *graphscore::MidiChannel::create(0);
    stave                                 = layout.staves()[0];
    track_id = *project.add_track("Track", layout, channel);
    node_id  = add_complete_node("Opening");
    project.find_node(node_id)->set_color(0x12345678);
  }

  graphscore::NodeId add_complete_node(std::string name) {
    const graphscore::NodeId added = project.add_node(std::move(name));
    graphscore::Node* const  node  = project.find_node(added);
    node->lane(track_id)->ensure_stave(stave.id);
    node->set_timeline(timeline_with_tempo(stave));
    graphscore::StaveVoices* const voices =
        node->lane(track_id)->stave(stave.id);
    for (std::uint8_t number = graphscore::Voice::kMin;
         number <= graphscore::Voice::kMax; ++number) {
      EXPECT_TRUE(voices->voice(*graphscore::Voice::create(number))
                      .normalize(node->timeline()->node_end())
                      .ok());
    }
    return added;
  }
};

TEST(CanvasNodeHeaderTest, RetainsMetadataStateAndDedicatedHeaderButtons) {
  HeaderFixture           fixture;
  graphscore::Node* const node = fixture.project.find_node(fixture.node_id);
  node->set_notes("Enter softly");
  const HeaderMetrics      metrics;
  const graphscore::Canvas canvas;

  const auto scene = canvas.layout_nodes(fixture.project, metrics);

  ASSERT_EQ(scene.nodes.size(), 1U);
  const graphscore::CanvasNodeHeader& header = scene.nodes[0].header;
  EXPECT_EQ(header.name, "Opening");
  EXPECT_EQ(header.color, 0x12345678U);
  EXPECT_TRUE(header.has_freeform_notes);
  EXPECT_EQ(header.validation, graphscore::CanvasNodeValidationState::kValid);
  EXPECT_TRUE(header.has_tempo_lane);
  EXPECT_EQ(header.freeform_notes_button.action,
            graphscore::CanvasNodeHeaderAction::kEditFreeformNotes);
  EXPECT_EQ(header.tempo_lane_button.action,
            graphscore::CanvasNodeHeaderAction::kOpenTempoLane);
  EXPECT_EQ(header.play_button.action,
            graphscore::CanvasNodeHeaderAction::kPlay);
}

TEST(CanvasNodeHeaderTest, EmptyNotesAndTempoStillRetainTheirAffordances) {
  HeaderFixture           fixture;
  graphscore::Node* const node = fixture.project.find_node(fixture.node_id);
  node->timeline()->clear_tempo();
  const HeaderMetrics      metrics;
  const graphscore::Canvas canvas;

  const auto scene = canvas.layout_nodes(fixture.project, metrics);

  ASSERT_EQ(scene.nodes.size(), 1U);
  const graphscore::CanvasNodeHeader& header = scene.nodes[0].header;
  EXPECT_FALSE(header.has_freeform_notes);
  EXPECT_FALSE(header.has_tempo_lane);
  EXPECT_EQ(header.freeform_notes_button.action,
            graphscore::CanvasNodeHeaderAction::kEditFreeformNotes);
  EXPECT_EQ(header.tempo_lane_button.action,
            graphscore::CanvasNodeHeaderAction::kOpenTempoLane);
}

TEST(CanvasNodeHeaderTest, SummarizesNodeScopedValidationErrors) {
  HeaderFixture            fixture;
  const graphscore::NodeId unaffected = fixture.add_complete_node("Unaffected");
  [[maybe_unused]] const graphscore::ConnectorId dangling =
      fixture.project.find_node(fixture.node_id)->add_output("Dangling");
  const HeaderMetrics      metrics;
  const graphscore::Canvas canvas;

  const auto scene = canvas.layout_nodes(fixture.project, metrics);

  ASSERT_EQ(scene.nodes.size(), 2U);
  EXPECT_EQ(scene.nodes[0].header.validation,
            graphscore::CanvasNodeValidationState::kError);
  EXPECT_EQ(scene.nodes[1].node_id, unaffected);
  EXPECT_EQ(scene.nodes[1].header.validation,
            graphscore::CanvasNodeValidationState::kValid);
}

}  // namespace
