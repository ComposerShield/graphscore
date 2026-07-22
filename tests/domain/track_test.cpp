// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <type_traits>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Clef;
using graphscore::MidiChannel;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::Track;
using graphscore::TrackId;
using graphscore::TrackIndex;
using graphscore::Uuid;

TEST(StaffLayoutTest, SingleStaffLayoutHasOneTrebleStave) {
  const StaffLayout layout = StaffLayout::single_staff();
  ASSERT_EQ(layout.stave_count(), 1u);
  EXPECT_EQ(layout.staves()[0].default_clef, Clef::kTreble);
}

TEST(StaffLayoutTest, SingleStaffAcceptsExplicitClef) {
  const StaffLayout layout = StaffLayout::single_staff(Clef::kBass);
  ASSERT_EQ(layout.stave_count(), 1u);
  EXPECT_EQ(layout.staves()[0].default_clef, Clef::kBass);
}

TEST(StaffLayoutTest, GrandStaffLayoutHasTrebleOverBass) {
  const StaffLayout layout = StaffLayout::grand_staff();
  ASSERT_EQ(layout.stave_count(), 2u);
  EXPECT_EQ(layout.staves()[0].default_clef, Clef::kTreble);
  EXPECT_EQ(layout.staves()[1].default_clef, Clef::kBass);
  EXPECT_NE(layout.staves()[0].id, layout.staves()[1].id);
}

TEST(StaffLayoutTest, CustomLayoutRejectsEmptyStaveList) {
  EXPECT_FALSE(StaffLayout::create({}).has_value());
}

TEST(StaffLayoutTest, CustomLayoutAcceptsMultipleSingleStaves) {
  const auto layout = StaffLayout::create({
      {StaveId::generate(), Clef::kTreble},
      {StaveId::generate(), Clef::kTenor},
      {StaveId::generate(), Clef::kBass},
  });
  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->stave_count(), 3u);
}

TEST(TrackTest, TrackHasFixedMidiChannel) {
  const auto channel = MidiChannel::create(9);
  ASSERT_TRUE(channel.has_value());
  const Track track(TrackId::generate(), TrackIndex(0), "Drums",
                    StaffLayout::single_staff(), *channel);
  EXPECT_EQ(track.channel(), *channel);
}

TEST(TrackTest, TrackHasGrandStaffLayout) {
  const Track track(TrackId::generate(), TrackIndex(0), "Piano",
                    StaffLayout::grand_staff(), *MidiChannel::create(0));
  EXPECT_EQ(track.layout().stave_count(), 2u);
}

TEST(TrackTest, IdAndIndexAreDistinctTypes) {
  static_assert(!std::is_same_v<TrackId, TrackIndex>);
  static_assert(!std::is_convertible_v<TrackId, TrackIndex>);
  static_assert(!std::is_convertible_v<TrackIndex, TrackId>);

  const TrackId    id(Uuid::generate());
  const TrackIndex index(3);
  const Track      track(id, index, "Track", StaffLayout::grand_staff(),
                         *MidiChannel::create(0));
  EXPECT_EQ(track.id(), id);
  EXPECT_EQ(track.index(), index);
}
