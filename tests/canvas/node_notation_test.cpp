// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

class FixedMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return {{-staff_space * 0.25, -staff_space * 0.5, staff_space * 1.5,
             staff_space * 2.0},
            staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

[[nodiscard]] graphscore::Measure measure() {
  return {*graphscore::TimeSignature::create(4, 4), graphscore::KeySignature{}};
}

struct Fixture {
  graphscore::Project project{graphscore::ProjectId::generate(), "Canvas"};
  graphscore::TrackId single_track;
  graphscore::TrackId grand_track;
  std::vector<graphscore::NodeId> node_ids;

  Fixture() {
    single_track =
        *project.add_track("Single", graphscore::StaffLayout::single_staff(),
                           *graphscore::MidiChannel::create(0));
    grand_track =
        *project.add_track("Grand", graphscore::StaffLayout::grand_staff(),
                           *graphscore::MidiChannel::create(1));

    std::vector<graphscore::StaveDefinition> staves;
    for (const auto& track : project.active_tracks()) {
      for (const auto& stave : track.layout().staves()) {
        staves.push_back(stave);
      }
    }

    for (std::size_t node_index = 0; node_index < 2; ++node_index) {
      const graphscore::NodeId node_id = project.add_node("Node");
      node_ids.push_back(node_id);
      graphscore::Node* const node = project.find_node(node_id);
      node->set_position({100.0 * static_cast<double>(node_index),
                          -50.0 * static_cast<double>(node_index)});
      for (const auto& track : project.active_tracks()) {
        graphscore::TrackLane* const lane = node->lane(track.id());
        for (const auto& stave : track.layout().staves()) {
          lane->ensure_stave(stave.id);
        }
      }
      auto timeline = graphscore::NodeTimeline::create(
          std::vector<graphscore::Measure>(2, measure()), staves);
      if (!timeline.has_value()) {
        ADD_FAILURE() << "fixture timeline creation failed";
        return;
      }
      node->set_timeline(std::move(*timeline));
    }
  }
};

TEST(CanvasNodeNotationTest,
     RetainsEveryNodeWithEveryActiveStaffOnItsCommonTimeline) {
  Fixture                  fixture;
  const FixedMetrics       metrics;
  const graphscore::Canvas canvas;

  const graphscore::CanvasNotationScene scene =
      canvas.layout_nodes(fixture.project, metrics);

  ASSERT_TRUE(scene.complete());
  ASSERT_EQ(scene.nodes.size(), fixture.node_ids.size());
  for (std::size_t node_index = 0; node_index < scene.nodes.size();
       ++node_index) {
    const auto& node = scene.nodes[node_index];
    EXPECT_EQ(node.node_id, fixture.node_ids[node_index]);
    EXPECT_EQ(node.position,
              fixture.project.find_node(node.node_id)->position());
    ASSERT_TRUE(node.layout.has_value());
    ASSERT_EQ(node.layout->systems.size(), 1U);
    const auto& system = node.layout->systems[0];
    ASSERT_EQ(system.staves.size(), 3U);
    ASSERT_EQ(system.measures.size(), 2U);
    for (const auto& staff : system.staves) {
      ASSERT_EQ(staff.measure_bounds.size(), system.measures.size());
      for (std::size_t measure_index = 0;
           measure_index < system.measures.size(); ++measure_index) {
        EXPECT_EQ(staff.measure_bounds[measure_index].x,
                  system.measures[measure_index].bounds.x);
        EXPECT_EQ(staff.measure_bounds[measure_index].width,
                  system.measures[measure_index].bounds.width);
      }
    }
    EXPECT_EQ(system.staves[0].track_id, fixture.single_track);
    EXPECT_EQ(system.staves[1].track_id, fixture.grand_track);
    EXPECT_EQ(system.staves[2].track_id, fixture.grand_track);
    EXPECT_FALSE(node.layout->commands.empty());
  }
}

TEST(CanvasNodeNotationTest, ExcludesArchivedTracksFromEveryNode) {
  Fixture fixture;
  ASSERT_TRUE(fixture.project.archive_track(fixture.single_track).ok());
  const FixedMetrics       metrics;
  const graphscore::Canvas canvas;

  const graphscore::CanvasNotationScene scene =
      canvas.layout_nodes(fixture.project, metrics);

  ASSERT_TRUE(scene.complete());
  for (const auto& node : scene.nodes) {
    ASSERT_TRUE(node.layout.has_value());
    ASSERT_EQ(node.layout->systems[0].staves.size(), 2U);
    EXPECT_EQ(node.layout->systems[0].staves[0].track_id, fixture.grand_track);
    EXPECT_EQ(node.layout->systems[0].staves[1].track_id, fixture.grand_track);
  }
}

TEST(CanvasNodeNotationTest,
     TrackAddArchiveAndRestoreRefreshesEveryNodeWithoutLosingMusic) {
  Fixture                    fixture;
  graphscore::CommandHistory history;
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<graphscore::AddTrackCommand>(
                           "Added", graphscore::StaffLayout::single_staff(),
                           *graphscore::MidiChannel::create(2)),
                       fixture.project)
          .ok());
  const graphscore::Track&   added    = fixture.project.active_tracks().back();
  const graphscore::TrackId  added_id = added.id();
  const graphscore::StaveId  stave_id = added.layout().staves().front().id;
  const graphscore::Duration quarter =
      *graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  const graphscore::NotationEntityId note_id =
      graphscore::NotationEntityId::generate();

  for (const graphscore::NodeId node_id : fixture.node_ids) {
    graphscore::Node* const node = fixture.project.find_node(node_id);
    ASSERT_NE(node, nullptr);
    graphscore::TrackLane* const lane = node->lane(added_id);
    ASSERT_NE(lane, nullptr);
    ASSERT_TRUE(lane->has_stave(stave_id));
    ASSERT_NE(node->timeline()->clef_lane(stave_id), nullptr);
  }
  graphscore::VoiceContent& voice =
      fixture.project.find_node(fixture.node_ids.front())
          ->lane(added_id)
          ->stave(stave_id)
          ->voice(*graphscore::Voice::create(1));
  graphscore::Note note = graphscore::make_note(
      *graphscore::SpelledPitch::create(graphscore::Letter::kC, 4), quarter);
  note.id = note_id;
  ASSERT_TRUE(voice.append(note).ok());

  const FixedMetrics       metrics;
  const graphscore::Canvas canvas;
  const auto added_scene = canvas.layout_nodes(fixture.project, metrics);
  ASSERT_TRUE(added_scene.complete());
  for (const auto& node : added_scene.nodes) {
    ASSERT_TRUE(node.layout.has_value());
    EXPECT_EQ(node.layout->systems[0].staves.size(), 4U);
    EXPECT_EQ(node.layout->systems[0].staves.back().track_id, added_id);
  }

  ASSERT_TRUE(
      history
          .execute_new(
              std::make_unique<graphscore::ArchiveTrackCommand>(added_id),
              fixture.project)
          .ok());
  const auto archived_scene = canvas.layout_nodes(fixture.project, metrics);
  ASSERT_TRUE(archived_scene.complete());
  for (const auto& node : archived_scene.nodes) {
    ASSERT_TRUE(node.layout.has_value());
    EXPECT_EQ(node.layout->systems[0].staves.size(), 3U);
  }
  const graphscore::VoiceContent& archived_voice =
      fixture.project.find_node(fixture.node_ids.front())
          ->lane(added_id)
          ->stave(stave_id)
          ->voice(*graphscore::Voice::create(1));
  ASSERT_EQ(archived_voice.events().size(), 1U);
  EXPECT_EQ(graphscore::event_id(archived_voice.events().front()), note_id);

  ASSERT_TRUE(history.undo(fixture.project).ok());
  const auto restored_scene = canvas.layout_nodes(fixture.project, metrics);
  ASSERT_TRUE(restored_scene.complete());
  for (const auto& node : restored_scene.nodes) {
    ASSERT_TRUE(node.layout.has_value());
    EXPECT_EQ(node.layout->systems[0].staves.size(), 4U);
  }
  const graphscore::VoiceContent& restored_voice =
      fixture.project.find_node(fixture.node_ids.front())
          ->lane(added_id)
          ->stave(stave_id)
          ->voice(*graphscore::Voice::create(1));
  ASSERT_EQ(restored_voice.events().size(), 1U);
  EXPECT_EQ(graphscore::event_id(restored_voice.events().front()), note_id);
}

TEST(CanvasNodeNotationTest, RetainsFailuresWithoutHidingOtherNodes) {
  Fixture fixture;
  fixture.project.find_node(fixture.node_ids[1])->clear_timeline();
  const FixedMetrics       metrics;
  const graphscore::Canvas canvas;

  const graphscore::CanvasNotationScene scene =
      canvas.layout_nodes(fixture.project, metrics);

  ASSERT_FALSE(scene.complete());
  ASSERT_EQ(scene.nodes.size(), 2U);
  EXPECT_TRUE(scene.nodes[0]);
  EXPECT_EQ(scene.nodes[0].error, graphscore::NotationLayoutError::kNone);
  EXPECT_FALSE(scene.nodes[1]);
  EXPECT_EQ(scene.nodes[1].node_id, fixture.node_ids[1]);
  EXPECT_EQ(scene.nodes[1].error,
            graphscore::NotationLayoutError::kTimelineMissing);
}

}  // namespace
