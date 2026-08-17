// SPDX-License-Identifier: Apache-2.0

#include "../clipboard/clipboard_test_support.hpp"

#include <variant>

namespace clipboard_test {

using graphscore::RangeEditCommand;
using graphscore::RangeTransposeKind;

TEST(RangeEditCommandTest, DeleteReplacesPartialRangeAndReverses) {
  Fixture fx;
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_note(pitch(Letter::kC), half()),
                   make_note(pitch(Letter::kD), half()), make_rest(whole()),
                   make_rest(whole()), make_rest(whole())}));
  const Selection selection = *ArbitraryRangeSet::create(
      {{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
        MusicalSpan{rat(1, 4), rat(3, 4)}}});
  const VoiceContent before =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  const TrackLane before_lane = fx.lane_of(fx.track_a);

  RangeEditCommand command = RangeEditCommand::make_delete(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const VoiceContent& edited =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_FALSE(edited.events().empty());
  EXPECT_EQ(edited.total_length(), before.total_length());
  EXPECT_TRUE(std::holds_alternative<Rest>(edited.events()[1]));
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_EQ(fx.node()
                ->lane(fx.track_a)
                ->stave(fx.stave_a_treble)
                ->voice(kVoice1)
                .total_length(),
            before.total_length());
}

TEST(RangeEditCommandTest, DiatonicTransposePreservesRhythmAndTies) {
  Fixture                fx;
  Note                   first  = make_note(pitch(Letter::kC), quarter(), true);
  Note                   second = make_note(pitch(Letter::kC), quarter());
  const NotationEntityId first_id  = first.id;
  const NotationEntityId second_id = second.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({first, second, make_rest(whole()), make_rest(whole()),
                         make_rest(whole()), make_rest(half())}));
  const Selection selection = *ArbitraryRangeSet::create(
      {{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
        MusicalSpan{rat(1, 4), rat(1, 2)}}});
  const VoiceContent before =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);

  RangeEditCommand command = RangeEditCommand::make_transpose(
      selection, RangeTransposeKind::kDiatonic, 2);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const VoiceContent& edited =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  const Note& first_after  = std::get<Note>(edited.events()[0]);
  const Note& second_after = std::get<Note>(edited.events()[1]);
  EXPECT_EQ(first_after.id, first_id);
  EXPECT_EQ(second_after.id, second_id);
  EXPECT_EQ(first_after.pitch, pitch(Letter::kE));
  EXPECT_EQ(second_after.pitch, pitch(Letter::kE));
  EXPECT_TRUE(first_after.tied_to_next);
  EXPECT_EQ(edited.total_length(), before.total_length());
  EXPECT_EQ(edited.events().size(), before.events().size());
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1) ==
      before);
}

TEST(RangeEditCommandTest, ChromaticTransposeUsesStableEnharmonicSpelling) {
  Fixture fx;
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_note(pitch(Letter::kC), quarter()),
                   make_note(pitch(Letter::kD), quarter()), make_rest(whole()),
                   make_rest(whole()), make_rest(whole()), make_rest(half())}));
  const Selection selection = *ArbitraryRangeSet::create(
      {{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
        MusicalSpan{Rational(0), rat(1, 2)}}});

  RangeEditCommand command = RangeEditCommand::make_transpose(
      selection, RangeTransposeKind::kChromatic, 1);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const VoiceContent& edited =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  EXPECT_EQ(std::get<Note>(edited.events()[0]).pitch.accidental(),
            Accidental::kSharp);
  EXPECT_EQ(std::get<Note>(edited.events()[1]).pitch.accidental(),
            Accidental::kSharp);
  EXPECT_EQ(edited.total_length(), fx.node_end());
}

TEST(RangeEditCommandTest, FullMeasureTransposeSkipsRestOnlyVoices) {
  Fixture fx;
  fx.assign(
      fx.track_a, fx.stave_a_treble, kVoice1,
      build_voice({make_note(pitch(Letter::kC), whole()), make_rest(whole()),
                   make_rest(whole()), make_rest(whole())}));
  const Selection selection =
      *FullMeasureSet::create({{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});

  RangeEditCommand command = RangeEditCommand::make_transpose(
      selection, RangeTransposeKind::kDiatonic, 1);
  ASSERT_TRUE(command.execute(fx.project).ok());
  EXPECT_EQ(std::get<Note>(fx.node()
                               ->lane(fx.track_a)
                               ->stave(fx.stave_a_treble)
                               ->voice(kVoice1)
                               .events()[0])
                .pitch,
            pitch(Letter::kD));
}

TEST(RangeEditCommandTest, RejectsIncoherentRangeItemsAtomically) {
  Fixture         fx;
  const TrackLane before    = fx.lane_of(fx.track_a);
  const Selection selection = *ArbitraryRangeSet::create(
      {{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
        MusicalSpan{Rational(0), rat(1, 4)}},
       {fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
        MusicalSpan{rat(1, 2), rat(3, 4)}}});

  RangeEditCommand command = RangeEditCommand::make_delete(selection);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(RangeEditCommandTest, RejectsTransposeWhenTheWholeRangeIsRest) {
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_a);
  const Selection selection =
      *FullMeasureSet::create({{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});

  RangeEditCommand command = RangeEditCommand::make_transpose(
      selection, RangeTransposeKind::kChromatic, 1);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

}  // namespace clipboard_test
