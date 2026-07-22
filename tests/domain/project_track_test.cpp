// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Dynamic;
using graphscore::MidiChannel;
using graphscore::NodeId;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::StaffLayout;
using graphscore::Tempo;
using graphscore::TrackId;

namespace {

Project make_project() {
  return Project(ProjectId::generate(), "Test Project");
}

}  // namespace

TEST(ProjectTrackTest, AddTrackAssignsSequentialIndices) {
  Project    project = make_project();
  const auto first   = project.add_track("First", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  const auto second  = project.add_track("Second", StaffLayout::single_staff(),
                                         *MidiChannel::create(1));
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  EXPECT_EQ(project.find_active_track(*first)->index().value(), 0u);
  EXPECT_EQ(project.find_active_track(*second)->index().value(), 1u);
}

TEST(ProjectTrackTest, SixtyFourthTrackSucceedsSixtyFifthFails) {
  Project project = make_project();
  for (int i = 0; i < 64; ++i) {
    const auto id = project.add_track(
        "Track " + std::to_string(i), StaffLayout::single_staff(),
        *MidiChannel::create(static_cast<std::uint8_t>(i % 16)));
    ASSERT_TRUE(id.has_value()) << "track " << i;
  }
  EXPECT_EQ(project.active_tracks().size(), 64u);

  const auto overflow = project.add_track(
      "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_FALSE(overflow.has_value());
  EXPECT_EQ(project.active_tracks().size(), 64u);
}

TEST(ProjectTrackTest, ArchiveMovesTrackOutOfActiveSet) {
  Project    project = make_project();
  const auto id      = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  ASSERT_TRUE(id.has_value());

  EXPECT_TRUE(project.archive_track(*id).ok());
  EXPECT_EQ(project.find_active_track(*id), nullptr);
  ASSERT_NE(project.find_archived_track(*id), nullptr);
  EXPECT_EQ(project.find_archived_track(*id)->name(), "Track");
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);
}

TEST(ProjectTrackTest, ArchiveUnknownTrackFails) {
  Project project = make_project();
  EXPECT_FALSE(project.archive_track(TrackId::generate()).ok());
}

TEST(ProjectTrackTest, RestoreReturnsTrackToActiveSet) {
  Project    project = make_project();
  const auto id      = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  ASSERT_TRUE(id.has_value());
  ASSERT_TRUE(project.archive_track(*id).ok());

  EXPECT_TRUE(project.restore_track(*id).ok());
  EXPECT_NE(project.find_active_track(*id), nullptr);
  EXPECT_EQ(project.find_archived_track(*id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);
}

TEST(ProjectTrackTest, RestoreFailsWhenActiveSetIsFull) {
  Project    project     = make_project();
  const auto archived_id = project.add_track(
      "Archived", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(archived_id.has_value());
  ASSERT_TRUE(project.archive_track(*archived_id).ok());

  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }

  EXPECT_FALSE(project.restore_track(*archived_id).ok());
  EXPECT_NE(project.find_archived_track(*archived_id), nullptr);
}

TEST(ProjectTrackTest, RestoreUnknownTrackFails) {
  Project project = make_project();
  EXPECT_FALSE(project.restore_track(TrackId::generate()).ok());
}

TEST(ProjectTrackTest, StartNodeMustBeOwnedByProject) {
  Project project = make_project();
  EXPECT_FALSE(project.set_start_node(NodeId::generate()).ok());

  const NodeId node_id = project.add_node("Start");
  EXPECT_TRUE(project.set_start_node(node_id).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node_id);
}

TEST(ProjectTrackTest, DefaultTempoAndDynamicAreSettable) {
  Project    project = make_project();
  const auto tempo   = Tempo::create(Rational(96), NoteValue::kQuarter);
  ASSERT_TRUE(tempo.has_value());
  project.set_default_tempo(*tempo);
  EXPECT_EQ(project.default_tempo(), *tempo);

  project.set_default_dynamic(Dynamic::kPpp);
  EXPECT_EQ(project.default_dynamic(), Dynamic::kPpp);
}
