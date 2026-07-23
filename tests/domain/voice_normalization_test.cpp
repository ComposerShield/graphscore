// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Duration;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_note;
using graphscore::Measure;
using graphscore::NodeTimeline;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::StaveVoices;
using graphscore::TimeSignature;
using graphscore::TrackLane;
using graphscore::Voice;
using graphscore::VoiceContent;

namespace {

SpelledPitch pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

Duration whole() {
  return *Duration::create(NoteValue::kWhole, 0);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Measure common_time_measure() {
  return Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)};
}

}  // namespace

TEST(StaveVoicesTest, FourVoicesAreIndependent) {
  StaveVoices voices;
  ASSERT_TRUE(voices.voice(*Voice::create(1))
                  .append(make_note(pitch(Letter::kC), quarter()))
                  .ok());
  ASSERT_TRUE(voices.voice(*Voice::create(2))
                  .append(make_note(pitch(Letter::kE), quarter()))
                  .ok());

  EXPECT_EQ(voices.voice(*Voice::create(1)).total_length(),
            *Rational::create(1, 4));
  EXPECT_EQ(voices.voice(*Voice::create(2)).total_length(),
            *Rational::create(1, 4));
  EXPECT_EQ(voices.voice(*Voice::create(3)).total_length(), Rational(0));
  EXPECT_EQ(voices.voice(*Voice::create(4)).total_length(), Rational(0));
}

TEST(TrackLaneTest, EnsureStaveCreatesEmptyFourVoiceSlot) {
  TrackLane     lane;
  const StaveId stave_id = StaveId::generate();
  EXPECT_FALSE(lane.has_stave(stave_id));

  lane.ensure_stave(stave_id);
  EXPECT_TRUE(lane.has_stave(stave_id));
  ASSERT_NE(lane.stave(stave_id), nullptr);
  EXPECT_EQ(lane.stave(stave_id)->voice(*Voice::create(1)).total_length(),
            Rational(0));
}

TEST(TrackLaneTest, EnsureStaveNeverOverwritesExistingContent) {
  TrackLane     lane;
  const StaveId stave_id = StaveId::generate();
  lane.ensure_stave(stave_id);
  ASSERT_TRUE(lane.stave(stave_id)
                  ->voice(*Voice::create(1))
                  .append(make_note(pitch(Letter::kC), whole()))
                  .ok());

  lane.ensure_stave(stave_id);
  EXPECT_EQ(lane.stave(stave_id)->voice(*Voice::create(1)).total_length(),
            Rational(1));
}

TEST(TrackLaneTest, GrandStaffStavesHoldIndependentVoices) {
  const StaffLayout layout = StaffLayout::grand_staff();
  TrackLane         lane;
  for (const StaveDefinition& stave : layout.staves()) {
    lane.ensure_stave(stave.id);
  }
  EXPECT_EQ(lane.stave_count(), 2u);

  const StaveId treble_id = layout.staves()[0].id;
  const StaveId bass_id   = layout.staves()[1].id;

  ASSERT_TRUE(lane.stave(treble_id)
                  ->voice(*Voice::create(1))
                  .append(make_note(pitch(Letter::kC), whole()))
                  .ok());
  ASSERT_TRUE(lane.stave(bass_id)
                  ->voice(*Voice::create(1))
                  .append(make_note(pitch(Letter::kC), quarter()))
                  .ok());

  EXPECT_EQ(lane.stave(treble_id)->voice(*Voice::create(1)).total_length(),
            Rational(1));
  EXPECT_EQ(lane.stave(bass_id)->voice(*Voice::create(1)).total_length(),
            *Rational::create(1, 4));
}

class RhythmicCompletenessTest : public ::testing::Test {
 protected:
  std::optional<NodeTimeline> make_two_measure_timeline() {
    return NodeTimeline::create({common_time_measure(), common_time_measure()},
                                {});
  }
};

TEST_F(RhythmicCompletenessTest, VoiceExactlyFillingNodeEndPasses) {
  auto timeline = make_two_measure_timeline();
  ASSERT_TRUE(timeline.has_value());

  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), whole())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kD), whole())).ok());

  EXPECT_TRUE(voice.check_complete(timeline->node_end()).ok());
}

TEST_F(RhythmicCompletenessTest, GappedVoiceNormalizesToFullCoverage) {
  auto timeline = make_two_measure_timeline();
  ASSERT_TRUE(timeline.has_value());

  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());

  EXPECT_FALSE(voice.check_complete(timeline->node_end()).ok());
  ASSERT_TRUE(voice.normalize(timeline->node_end()).ok());
  EXPECT_TRUE(voice.check_complete(timeline->node_end()).ok());
}

TEST_F(RhythmicCompletenessTest, OverfilledVoiceIsFlaggedNotSilentlyFixed) {
  auto timeline = make_two_measure_timeline();
  ASSERT_TRUE(timeline.has_value());

  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), whole())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kD), whole())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kE), quarter())).ok());

  EXPECT_FALSE(voice.check_complete(timeline->node_end()).ok());
  EXPECT_FALSE(voice.normalize(timeline->node_end()).ok());
}

TEST_F(RhythmicCompletenessTest, PickdownExtendsTheTargetLength) {
  auto timeline = NodeTimeline::create({common_time_measure()}, {});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  EXPECT_EQ(timeline->node_end(), *Rational::create(5, 4));

  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), whole())).ok());
  EXPECT_FALSE(voice.check_complete(timeline->node_end()).ok());

  ASSERT_TRUE(voice.normalize(timeline->node_end()).ok());
  EXPECT_TRUE(voice.check_complete(timeline->node_end()).ok());
}

TEST(FourVoiceGrandStaffTest, AllVoicesNormalizeIndependentlyToNodeEnd) {
  auto timeline =
      NodeTimeline::create({common_time_measure(), common_time_measure()}, {});
  ASSERT_TRUE(timeline.has_value());
  const Rational target = timeline->node_end();

  const StaffLayout layout = StaffLayout::grand_staff();
  TrackLane         lane;
  for (const StaveDefinition& stave : layout.staves()) {
    lane.ensure_stave(stave.id);
  }

  for (const StaveDefinition& stave : layout.staves()) {
    StaveVoices* voices = lane.stave(stave.id);
    ASSERT_NE(voices, nullptr);
    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
      VoiceContent& voice = voices->voice(*Voice::create(v));
      ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());
      ASSERT_TRUE(voice.normalize(target).ok());
      EXPECT_TRUE(voice.check_complete(target).ok());
    }
  }
}

TEST(RepresentativeScaleTest,
     SixtyFourMeasureGrandStaffConstructsValidatesAndMutates) {
  constexpr int              kMeasureCount = 64;
  const std::vector<Measure> measures(kMeasureCount, common_time_measure());
  auto                       timeline = NodeTimeline::create(measures, {});
  ASSERT_TRUE(timeline.has_value());
  const Rational target = timeline->node_end();
  EXPECT_EQ(target, Rational(kMeasureCount));

  const StaffLayout layout = StaffLayout::grand_staff();
  TrackLane         lane;
  for (const StaveDefinition& stave : layout.staves()) {
    lane.ensure_stave(stave.id);
  }

  for (const StaveDefinition& stave : layout.staves()) {
    StaveVoices* voices = lane.stave(stave.id);
    ASSERT_NE(voices, nullptr);
    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
      VoiceContent& voice = voices->voice(*Voice::create(v));
      for (int measure = 0; measure < kMeasureCount; ++measure) {
        ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), whole())).ok());
      }
      EXPECT_TRUE(voice.check_complete(target).ok());
      EXPECT_TRUE(voice.validate().ok());
    }
  }

  // Copy the whole lane and mutate the copy without disturbing the
  // original.
  TrackLane    lane_copy     = lane;
  StaveVoices* copied_voices = lane_copy.stave(layout.staves()[0].id);
  ASSERT_NE(copied_voices, nullptr);
  VoiceContent& copied_voice = copied_voices->voice(*Voice::create(1));
  copied_voice.clear();
  EXPECT_EQ(copied_voice.total_length(), Rational(0));

  StaveVoices* original_voices = lane.stave(layout.staves()[0].id);
  ASSERT_NE(original_voices, nullptr);
  EXPECT_EQ(original_voices->voice(*Voice::create(1)).total_length(), target);
}
