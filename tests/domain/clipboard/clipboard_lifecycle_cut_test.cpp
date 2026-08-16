// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace clipboard_test {

TEST(ClipboardCommandTest, PasteExecuteUndoRedoRestoresExactlyViaLaneEquality) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), eighth()),
                       make_note(pitch(Letter::kD), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  const TrackLane before = fx.lane_of(fx.track_a);

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane after_execute = fx.lane_of(fx.track_a);
  EXPECT_FALSE(after_execute == before);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_execute);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteIntoOccupiedRangeReplacesOnlyThatRange) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter()),
                         make_note(pitch(Letter::kE), quarter()),
                         make_note(pitch(Letter::kF), quarter())}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 2)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  // node_end() (4) exceeds the destination's own pre-paste content (1), so
  // region3 pads with extra rests after the fourth event.
  ASSERT_GE(content.events().size(), 4u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kD));
  EXPECT_EQ(std::get<Note>(content.events()[2]).pitch, pitch(Letter::kB));
  EXPECT_EQ(std::get<Note>(content.events()[3]).pitch, pitch(Letter::kF));
}

TEST(ClipboardCommandTest, PasteIntoEmptyDefaultVoicePadsBothSides) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  EXPECT_TRUE(content.check_complete(fx.node_end()).ok());

  bool     saw_note = false;
  Rational total(0);
  for (const VoiceEvent& event : content.events()) {
    if (const auto* note = std::get_if<Note>(&event)) {
      EXPECT_EQ(note->pitch, pitch(Letter::kC));
      saw_note = true;
    }
    total = total + graphscore::event_duration(event).resolved();
  }
  EXPECT_TRUE(saw_note);
  EXPECT_EQ(total, fx.node_end());
}

TEST(ClipboardCommandTest, PasteOverflowingNodeEndFailsModelUnchanged) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      Rational(1), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, build_voice({make_rest(whole())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(7, 2)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteNegativePositionFailsModelUnchanged) {
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(-1)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, CutFragmentMatchesExtractFragmentStructurally) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  const FragmentExtraction expected = extract_fragment(fx.project, selection);
  ASSERT_TRUE(expected.fragment.has_value());

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  EXPECT_TRUE(
      fragments_structurally_equal(*command.fragment(), *expected.fragment));
}

TEST(ClipboardCommandTest, CutRangeBecomesNormalizedRests) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_TRUE(content.check_complete(fx.node_end()).ok());
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[0]));
  EXPECT_EQ(std::get<Rest>(content.events()[0]).duration, half());
}

TEST(ClipboardCommandTest, CutUndoRestoresExactlyAndRedoDoesNotReExtract) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});
  const TrackLane before = fx.lane_of(fx.track_a);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  std::vector<NotationEntityId> first_ids;
  collect_ids(command.fragment()->parts()[0].content, first_ids);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  std::vector<NotationEntityId> second_ids;
  collect_ids(command.fragment()->parts()[0].content, second_ids);
  EXPECT_EQ(first_ids, second_ids);
}

TEST(ClipboardCommandTest, CutThenPasteElsewhereInOneTransaction) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction = extract_fragment(fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  const PasteAnchor destination{fx.node_id, fx.track_b, fx.stave_b,
                                Rational(0)};

  CommandTransaction txn;
  ASSERT_TRUE(
      txn.add_command(std::make_unique<CutFragmentCommand>(selection)).ok());
  ASSERT_TRUE(txn.add_command(std::make_unique<PasteFragmentCommand>(
                                  *extraction.fragment, destination))
                  .ok());

  ASSERT_TRUE(txn.execute(fx.project).ok());

  const VoiceContent& cut_side =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(cut_side.events().size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Rest>(cut_side.events()[0]));

  const VoiceContent& paste_side =
      fx.node()->lane(fx.track_b)->stave(fx.stave_b)->voice(kVoice1);
  ASSERT_GE(paste_side.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(paste_side.events()[0]).pitch, pitch(Letter::kC));

  const TrackLane a_after = fx.lane_of(fx.track_a);
  const TrackLane b_after = fx.lane_of(fx.track_b);
  ASSERT_TRUE(txn.undo(fx.project).ok());
  EXPECT_FALSE(fx.lane_of(fx.track_a) == a_after);
  EXPECT_FALSE(fx.lane_of(fx.track_b) == b_after);
}

TEST(ClipboardCommandTest, TransactionRollsBackCutWhenPasteOverflows) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});
  const TrackLane before = fx.lane_of(fx.track_a);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction = extract_fragment(fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  // Overflowing anchor: position + span_length() exceeds node_end().
  const PasteAnchor bad_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                               fx.node_end()};

  CommandTransaction txn;
  ASSERT_TRUE(
      txn.add_command(std::make_unique<CutFragmentCommand>(selection)).ok());
  ASSERT_TRUE(txn.add_command(std::make_unique<PasteFragmentCommand>(
                                  *extraction.fragment, bad_anchor))
                  .ok());

  const Result result = txn.execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteUndoRejectsStaleContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane post_execute = fx.lane_of(fx.track_a);

  *fx.node()->lane(fx.track_a) = TrackLane{};
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);

  *fx.node()->lane(fx.track_a) = post_execute;
  EXPECT_TRUE(command.undo(fx.project).ok());
}

TEST(ClipboardCommandTest, PasteRedoRejectsStaleContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.undo(fx.project).ok());
  const TrackLane post_undo = fx.lane_of(fx.track_a);

  *fx.node()->lane(fx.track_a) = TrackLane{};
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);

  *fx.node()->lane(fx.track_a) = post_undo;
  EXPECT_TRUE(command.redo(fx.project).ok());
}

TEST(ClipboardCommandTest, CutUndoRejectsStaleContextThenRetries) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane post_execute = fx.lane_of(fx.track_a);

  *fx.node()->lane(fx.track_a) = TrackLane{};
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);

  *fx.node()->lane(fx.track_a) = post_execute;
  EXPECT_TRUE(command.undo(fx.project).ok());
}

TEST(ClipboardCommandTest, PasteStateGuardsRejectDoubleExecuteAndOutOfOrder) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand fresh(fragment, anchor);
  EXPECT_EQ(fresh.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fresh.redo(fx.project).code(), ResultCode::kInvalidArgument);

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest, CutStateGuardsRejectDoubleExecuteAndOutOfOrder) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 4)}}});

  CutFragmentCommand fresh(selection);
  EXPECT_EQ(fresh.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fresh.redo(fx.project).code(), ResultCode::kInvalidArgument);

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest, PasteIntoEntirelyDefaultEmptyVoicesSucceeds) {
  // Finding 1: untouched voices must stay raw-empty (default-constructed),
  // not be forcibly normalized.  Only the voice actually touched by the
  // paste (voice 1) becomes rhythmically complete.
  UnfilledFixture        fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  // Voice 1 was touched and must be complete.
  EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
  // Voices 2-4 were untouched and must remain raw-empty.
  for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
    EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
  }
}

TEST(ClipboardCommandTest,
     PasteUntouchedVoicesRemainRawAndUndoRestoresExactPreState) {
  // Finding 1: untouched voices must stay in their raw pre-paste state
  // (default-constructed, empty), and undo must restore the exact raw
  // pre-state, not a normalized version.
  UnfilledFixture    fx;
  const VoiceContent raw_voice1_pre =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  const VoiceContent raw_voice2_pre =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1,
                         build_voice({make_note(pitch(Letter::kC), half())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // After paste: voice 1 (touched) must be complete.
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    // Voices 2-4: untouched, remain raw-empty.
    for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
      EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
    }
  }

  // Undo restores the exact raw pre-state (all voices raw-empty).
  ASSERT_TRUE(command.undo(fx.project).ok());
  {
    const auto*  stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    VoiceContent expect_empty;
    EXPECT_TRUE(stave->voice(kVoice1) == raw_voice1_pre);
    EXPECT_TRUE(stave->voice(kVoice2) == raw_voice2_pre);
    EXPECT_EQ(stave->voice(kVoice1).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice2).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice3).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice4).total_length(), Rational(0));
  }

  // Redo re-applies: voice 1 complete again.
  ASSERT_TRUE(command.redo(fx.project).ok());
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
      EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
    }
  }
}

TEST(ClipboardCommandTest,
     CutOnDefaultEmptyVoicesSucceedsAndUndoRestoresRawState) {
  // Finding 1: after a cut on a lane with raw-empty untouched voices, those
  // voices must stay raw-empty.  Undo must restore the exact raw pre-state.
  UnfilledFixture fx;
  // Put one note into voice 1 so there is something to cut.
  fx.assign_note(fx.track_a, fx.stave_a_treble, kVoice1,
                 make_note(pitch(Letter::kC), whole()));

  const VoiceContent pre_voice2 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());

  // Voice 1: cut range became rests, then padded to node_end (complete).
  // Voices 2-4: untouched, remain raw-empty.
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
      EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
    }
  }

  // Undo: exact raw pre-state restored (all voices back to pre-cut state).
  ASSERT_TRUE(command.undo(fx.project).ok());
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    // Voice 2 must be byte-identical to its raw pre-cut (empty) state.
    EXPECT_TRUE(stave->voice(kVoice2) == pre_voice2);
    EXPECT_EQ(stave->voice(kVoice2).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice3).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice4).total_length(), Rational(0));
  }
}

TEST(ClipboardCommandTest, PasteFailureLeavesModelUnchangedAndCommandFresh) {
  // Overflow failure: model and lane must be unchanged, command must be
  // retryable (not in kFaulted or kDone state).
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      Rational(1), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, build_voice({make_rest(whole())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           fx.node_end()};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  // Command must be retryable: undo/redo reject, but execute with a valid
  // anchor succeeds.
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);

  const PasteAnchor    good_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                                Rational(0)};
  PasteFragmentCommand retry(fragment, good_anchor);
  ASSERT_TRUE(retry.execute(fx.project).ok());
}

TEST(ClipboardCommandTest, CutFailureLeavesModelUnchangedAndFragmentEmpty) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  // Cut range that straddles a tuplet → failure.
  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 24)}}});

  CutFragmentCommand command(selection);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(command.fragment().has_value());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  // Undo/redo must reject when command is still fresh.
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest, MultiLaneCutLifecycleAndAtomicStaleUndoRejection) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole())});
  fx.assign_and_complete(fx.track_b, fx.stave_b, kVoice1,
                         {make_note(pitch(Letter::kG), whole())});
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_b)
                  ->add_pedal_span(fx.stave_b,
                                   make_pedal_span(Rational(0), Rational(1)))
                  .ok());
  const TrackLane before_a  = fx.lane_of(fx.track_a);
  const TrackLane before_b  = fx.lane_of(fx.track_b);
  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0},
       FullMeasureItem{fx.node_id, fx.track_b, fx.stave_b, 0}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  const TrackLane after_a = fx.lane_of(fx.track_a);
  const TrackLane after_b = fx.lane_of(fx.track_b);

  *fx.node()->lane(fx.track_b) = TrackLane{};
  const TrackLane stale_a      = fx.lane_of(fx.track_a);
  const TrackLane stale_b      = fx.lane_of(fx.track_b);
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == stale_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == stale_b);

  *fx.node()->lane(fx.track_b) = after_b;
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == after_b);
}

TEST(ClipboardCommandTest,
     PasteLastCandidateValidationFailureLeavesCommandRetryable) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2,
            build_voice({make_note(pitch(Letter::kD), quarter())}));
  const TrackLane        invalid_before = fx.lane_of(fx.track_a);
  const NotationFragment fragment       = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == invalid_before);

  VoiceContent repaired =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);
  ASSERT_TRUE(repaired.normalize(fx.node_end()).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2, std::move(repaired));
  ASSERT_TRUE(command.execute(fx.project).ok());
}

TEST(ClipboardCommandTest,
     CutLastCandidateValidationFailureLeavesFragmentEmptyAndRetries) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole())});
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2,
            build_voice({make_note(pitch(Letter::kD), quarter())}));
  const TrackLane invalid_before = fx.lane_of(fx.track_a);
  const Selection selection      = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});

  CutFragmentCommand command(selection);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(command.fragment().has_value());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == invalid_before);

  VoiceContent repaired =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);
  ASSERT_TRUE(repaired.normalize(fx.node_end()).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2, std::move(repaired));
  ASSERT_TRUE(command.execute(fx.project).ok());
  EXPECT_TRUE(command.fragment().has_value());
}

}  // namespace clipboard_test
