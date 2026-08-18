// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <utility>
#include <vector>

namespace clipboard_test {

TEST(ClipboardCommandTest, PasteAtDestinationShorterThanRangeRestFillsGap) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), eighth())}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_TRUE(content.check_complete(fx.node_end()).ok());
  ASSERT_GE(content.events().size(), 3u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC));
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[1]));
}

TEST(ClipboardCommandTest, PasteVoicePreservedExactlyByFragmentVoiceOrdinal) {
  Fixture     fx;
  const auto* stave_before =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  const VoiceContent before_voice1 = stave_before->voice(kVoice1);
  const VoiceContent before_voice2 = stave_before->voice(kVoice2);
  const VoiceContent before_voice4 = stave_before->voice(kVoice4);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice3,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  ASSERT_GE(stave->voice(kVoice3).events().size(), 1u);
  EXPECT_EQ(std::get<Note>(stave->voice(kVoice3).events()[0]).pitch,
            pitch(Letter::kC));
  // A fragment part in one voice only touches that exact destination
  // voice: every other voice is byte-identical to its pre-paste value.
  EXPECT_TRUE(stave->voice(kVoice1) == before_voice1);
  EXPECT_TRUE(stave->voice(kVoice2) == before_voice2);
  EXPECT_TRUE(stave->voice(kVoice4) == before_voice4);
}

TEST(ClipboardCommandTest, PasteMultiStaveFragmentOntoGrandStaff) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kE), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* lane = fx.node()->lane(fx.track_a);
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_treble)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kC));
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_bass)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kE));
}

TEST(ClipboardCommandTest,
     PastePlacementMatchesCommandIntervalAndAffectedStavesWithoutMutation) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kG), quarter()),
                          make_note(pitch(Letter::kA), quarter()),
                          make_note(pitch(Letter::kB), quarter())});
  fx.assign_and_complete(fx.track_a, fx.stave_a_bass, kVoice2,
                         {make_note(pitch(Letter::kG, 3), quarter()),
                          make_note(pitch(Letter::kA, 3), quarter()),
                          make_note(pitch(Letter::kB, 3), quarter())});
  const TrackLane        before   = fx.lane_of(fx.track_a);
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1,
                         build_voice({make_note(pitch(Letter::kC), half())})},
       FragmentVoicePart{0, 1, kVoice2,
                         build_voice({make_note(pitch(Letter::kE), half())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  const auto placement = describe_paste_placement(fx.project, fragment, anchor);
  ASSERT_TRUE(placement.has_value());
  EXPECT_EQ(placement->node, fx.node_id);
  EXPECT_EQ(placement->span, (MusicalSpan{rat(1, 4), rat(3, 4)}));
  EXPECT_EQ(placement->scopes,
            (std::vector<PasteScope>{{fx.track_a, fx.stave_a_treble},
                                     {fx.track_a, fx.stave_a_bass}}));
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane after = fx.lane_of(fx.track_a);
  EXPECT_FALSE(after == before);
  EXPECT_EQ(
      std::get<Note>(after.stave(fx.stave_a_treble)->voice(kVoice1).events()[1])
          .pitch,
      pitch(Letter::kC));
  EXPECT_EQ(
      std::get<Note>(after.stave(fx.stave_a_bass)->voice(kVoice2).events()[1])
          .pitch,
      pitch(Letter::kE));
  for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
    EXPECT_TRUE(after.stave(fx.stave_a_treble)->voice(voice) ==
                before.stave(fx.stave_a_treble)->voice(voice));
  }
  for (const Voice voice : {kVoice1, kVoice3, kVoice4}) {
    EXPECT_TRUE(after.stave(fx.stave_a_bass)->voice(voice) ==
                before.stave(fx.stave_a_bass)->voice(voice));
  }

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after);
}

TEST(ClipboardCommandTest,
     PastePlacementUsesExplicitScopesAndRejectsExactlyWhenCommandRejects) {
  Fixture                fx;
  const TrackLane        before_a = fx.lane_of(fx.track_a);
  const TrackLane        before_b = fx.lane_of(fx.track_b);
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kE), quarter())})}});
  const PasteAnchor explicit_anchor{
      fx.node_id,
      fx.track_a,
      fx.stave_a_treble,
      Rational(0),
      {{fx.track_a, fx.stave_a_bass}, {fx.track_b, fx.stave_b}}};

  const auto placement =
      describe_paste_placement(fx.project, fragment, explicit_anchor);
  ASSERT_TRUE(placement.has_value());
  EXPECT_EQ(placement->scopes,
            (std::vector<PasteScope>{{fx.track_a, fx.stave_a_bass},
                                     {fx.track_b, fx.stave_b}}));
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);

  const PasteAnchor out_of_range{fx.node_id, fx.track_a, fx.stave_a_treble,
                                 fx.node_end()};
  EXPECT_FALSE(
      describe_paste_placement(fx.project, fragment, out_of_range).has_value());
  PasteFragmentCommand rejected(fragment, out_of_range);
  EXPECT_EQ(rejected.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
}

TEST(ClipboardCommandTest, PasteMultiTrackFragmentOntoConsecutiveActiveTracks) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}, FragmentTrackShape{1}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           1, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kG), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  EXPECT_EQ(std::get<Note>(fx.node()
                               ->lane(fx.track_a)
                               ->stave(fx.stave_a_treble)
                               ->voice(kVoice1)
                               .events()[0])
                .pitch,
            pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(fx.node()
                               ->lane(fx.track_b)
                               ->stave(fx.stave_b)
                               ->voice(kVoice1)
                               .events()[0])
                .pitch,
            pitch(Letter::kG));
}

TEST(ClipboardCommandTest, PasteRunningOutOfStavesFailsModelUnchanged) {
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kE), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before);
}

TEST(ClipboardCommandTest, PasteRunningOutOfTracksFailsModelUnchanged) {
  Fixture         fx;
  const TrackLane before_a = fx.lane_of(fx.track_a);
  const TrackLane before_b = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4),
      {FragmentTrackShape{1}, FragmentTrackShape{1}, FragmentTrackShape{1}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           1, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kE), quarter())})},
       FragmentVoicePart{
           2, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kG), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
}

TEST(ClipboardCommandTest, TrebleToBassPastePreservesSpelledPitchExactly) {
  // Build a source project with known pitches in a treble staff, extract
  // a fragment, then paste into a bass-only destination and verify every
  // SpelledPitch (letter, octave, accidental) is byte-identical to the
  // source — paste must never adjust for clef difference.
  Fixture    src_fx;
  const Note c4 = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  const Note d4 = make_note(*SpelledPitch::create(Letter::kD, 4), quarter());
  const Note e4 = make_note(*SpelledPitch::create(Letter::kE, 4), quarter());
  src_fx.assign(src_fx.track_a, src_fx.stave_a_treble, kVoice1,
                build_voice({c4, d4, e4}));
  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{src_fx.node_id, src_fx.track_a, src_fx.stave_a_treble,
                          kVoice1, MusicalSpan{Rational(0), rat(3, 4)}}});
  const FragmentExtraction extraction =
      extract_fragment(src_fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  // Destination is a separate project with a bass-only staff (just track_b,
  // single staff, treble clef context but pitches remain unchanged).
  Fixture           dst_fx;
  const PasteAnchor anchor{dst_fx.node_id, dst_fx.track_b, dst_fx.stave_b,
                           Rational(0)};

  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(dst_fx.project).ok());

  const VoiceContent& content = dst_fx.node()
                                    ->lane(dst_fx.track_b)
                                    ->stave(dst_fx.stave_b)
                                    ->voice(kVoice1);
  ASSERT_GE(content.events().size(), 3u);
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[1]));
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[2]));

  const Note& pasted_c4 = std::get<Note>(content.events()[0]);
  const Note& pasted_d4 = std::get<Note>(content.events()[1]);
  const Note& pasted_e4 = std::get<Note>(content.events()[2]);

  EXPECT_TRUE(pasted_c4.pitch == c4.pitch);
  EXPECT_TRUE(pasted_d4.pitch == d4.pitch);
  EXPECT_TRUE(pasted_e4.pitch == e4.pitch);

  // Verify each SpelledPitch field individually (accessors, not members).
  for (const auto& [src_note, dst_note] :
       {std::make_pair(&c4, &pasted_c4), std::make_pair(&d4, &pasted_d4),
        std::make_pair(&e4, &pasted_e4)}) {
    EXPECT_EQ(dst_note->pitch.letter(), src_note->pitch.letter());
    EXPECT_EQ(dst_note->pitch.octave(), src_note->pitch.octave());
    EXPECT_EQ(dst_note->pitch.accidental(), src_note->pitch.accidental());
  }

  // Destination range tile check: fragment length + rest fill = node_end.
  EXPECT_TRUE(content.check_complete(dst_fx.node_end()).ok());
}

TEST(ClipboardCommandTest,
     GrandStaffFragmentOnlyBassRequiresOneDestinationStave) {
  // A fragment whose shape declares grand staff (stave_count = 2) but
  // whose parts reference only stave_ordinal 1 (the bass stave) must
  // require only that one mapped stave — not both.  Pasting into a
  // single-staff track must succeed.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 1, kVoice1,
          build_voice({make_note(pitch(Letter::kE, 3), quarter())})}});

  // Anchor into the single-staff track_b.
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_b)->stave(fx.stave_b)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kE, 3));
}

TEST(ClipboardCommandTest,
     GrandStaffFragmentBothStavesRequiresTwoDestinationStaves) {
  // A fragment declaring grand staff and actually referencing both staves
  // must still fail when pasted into a single-staff track.
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kE, 3), quarter())})}});

  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before);
}

TEST(ClipboardCommandTest,
     PasteUndoRestoresExactRawLaneIncludingUntouchedStaves) {
  // Fixture pre-fills all voices rest-filled.  We touch only track_a's
  // treble stave voice 1.  After undo, the entire lane (including untouched
  // bass stave and voices 2-4) must be byte-equal to the pre-paste lane.
  Fixture                fx;
  const TrackLane        before   = fx.lane_of(fx.track_a);
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const TrackLane after_execute = fx.lane_of(fx.track_a);
  EXPECT_FALSE(after_execute == before);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_execute);
}

TEST(ClipboardCommandTest,
     PasteWithoutPreEnsuringStaveSucceedsWhenMappingPermits) {
  // The paste mapping resolves staves through the destination track's
  // layout, not through pre-existing ensure_stave calls.  If a stave does
  // not yet exist in the lane, ensure_stave is called during paste and the
  // paste succeeds.
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
  ASSERT_NE(stave, nullptr);
  EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
}

TEST(ClipboardCommandTest,
     TrebleToBassPastePreservesAccidentalAndChordNoteOctave) {
  // Source: treble staff with F#5 (accidental) and Eb4.
  // Destination: any staff.  Pitches must be verbatim regardless of clef.
  Fixture    src_fx;
  const Note f_sharp = make_note(
      *SpelledPitch::create(Letter::kF, 5, Accidental::kSharp), quarter());
  const Note e_flat = make_note(
      *SpelledPitch::create(Letter::kE, 4, Accidental::kFlat), quarter());
  src_fx.assign(src_fx.track_a, src_fx.stave_a_treble, kVoice1,
                build_voice({f_sharp, e_flat}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{src_fx.node_id, src_fx.track_a, src_fx.stave_a_treble,
                          kVoice1, MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction =
      extract_fragment(src_fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  Fixture           dst_fx;
  const PasteAnchor anchor{dst_fx.node_id, dst_fx.track_b, dst_fx.stave_b,
                           Rational(0)};

  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(dst_fx.project).ok());

  const VoiceContent& content = dst_fx.node()
                                    ->lane(dst_fx.track_b)
                                    ->stave(dst_fx.stave_b)
                                    ->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);

  // F#5: must be exact.
  const Note& pasted_fs = std::get<Note>(content.events()[0]);
  EXPECT_EQ(pasted_fs.pitch.letter(), Letter::kF);
  EXPECT_EQ(pasted_fs.pitch.octave(), 5);
  EXPECT_EQ(pasted_fs.pitch.accidental(), Accidental::kSharp);

  // Eb4: must be exact.
  const Note& pasted_eb = std::get<Note>(content.events()[1]);
  EXPECT_EQ(pasted_eb.pitch.letter(), Letter::kE);
  EXPECT_EQ(pasted_eb.pitch.octave(), 4);
  EXPECT_EQ(pasted_eb.pitch.accidental(), Accidental::kFlat);
}

TEST(ClipboardCommandTest, SparseOrdinalsZeroAndTwoOnSingleStaffFails) {
  // On a single-staff track, ordinal 2 (compacted to index 1) needs 2
  // staves → fails.
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{3}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kG, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before);
}

TEST(ClipboardCommandTest, RepeatedVoicesOnSameOrdinalShareStave) {
  // Two fragment parts with the same (track_ordinal, stave_ordinal) but
  // different voices → they share one destination stave.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 0, kVoice2,
           build_voice({make_note(pitch(Letter::kD, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  EXPECT_EQ(std::get<Note>(stave->voice(kVoice1).events()[0]).pitch,
            pitch(Letter::kC, 4));
  EXPECT_EQ(std::get<Note>(stave->voice(kVoice2).events()[0]).pitch,
            pitch(Letter::kD, 4));

  // Bass stave must be untouched.
  const auto* bass = fx.node()->lane(fx.track_a)->stave(fx.stave_a_bass);
  ASSERT_NE(bass, nullptr);
}

TEST(ClipboardCommandTest, NonFirstAnchorStavePasteSucceeds) {
  // Anchor on the bass stave (stave_ordinal 1 in grand staff).  Fragment
  // stave_ordinal 0 maps to the bass stave itself.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kE, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_bass,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_bass)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kE, 3));
}

TEST(ClipboardCommandTest,
     TrebleToBassChordAndAccidentalNoteSpelledPitchInvariant) {
  // Source: treble staff with F#5 (accidental) and a C-major-root + Eb4
  // Chord. Destination: bass-only stave (Clef::kBass). Every SpelledPitch
  // (letter, octave, accidental) must match verbatim; clef_at_origin is
  // informational and must not be applied.
  Fixture    src_fx;
  const Note f_sharp = make_note(
      *SpelledPitch::create(Letter::kF, 5, Accidental::kSharp), quarter());

  std::vector<ChordNote> chord_notes = {
      ChordNote{NotationEntityId{},
                *SpelledPitch::create(Letter::kC, 4, Accidental::kNatural),
                false},
      ChordNote{NotationEntityId{},
                *SpelledPitch::create(Letter::kE, 4, Accidental::kFlat),
                false}};
  const Chord chord = make_chord(quarter(), std::move(chord_notes));

  src_fx.assign(src_fx.track_a, src_fx.stave_a_treble, kVoice1,
                build_voice({f_sharp, chord}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{src_fx.node_id, src_fx.track_a, src_fx.stave_a_treble,
                          kVoice1, MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction =
      extract_fragment(src_fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  // Verify the fragment carries clef_at_origin = kTreble.
  bool saw_treble_clef = false;
  for (const auto& ctx : extraction.fragment->stave_contexts()) {
    if (ctx.stave_ordinal == 0 && ctx.track_ordinal == 0) {
      EXPECT_EQ(ctx.clef_at_origin, Clef::kTreble);
      saw_treble_clef = true;
    }
  }
  EXPECT_TRUE(saw_treble_clef);

  // Destination: anchor on the bass stave (stave_a_bass) which has
  // default_clef = Clef::kBass.
  Fixture      dst_fx;
  const Track* dst_track = dst_fx.project.find_active_track(dst_fx.track_a);
  ASSERT_NE(dst_track, nullptr);
  ASSERT_GE(dst_track->layout().staves().size(), 2u);
  EXPECT_EQ(dst_track->layout().staves()[1].default_clef, Clef::kBass);

  const PasteAnchor anchor{dst_fx.node_id, dst_fx.track_a, dst_fx.stave_a_bass,
                           Rational(0)};

  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(dst_fx.project).ok());

  const VoiceContent& content = dst_fx.node()
                                    ->lane(dst_fx.track_a)
                                    ->stave(dst_fx.stave_a_bass)
                                    ->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);

  // F#5: verbatim.
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  const Note& pasted_fs = std::get<Note>(content.events()[0]);
  EXPECT_EQ(pasted_fs.pitch.letter(), Letter::kF);
  EXPECT_EQ(pasted_fs.pitch.octave(), 5);
  EXPECT_EQ(pasted_fs.pitch.accidental(), Accidental::kSharp);

  // Chord: two noteheads verbatim.
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[1]));
  const Chord& pasted_chord = std::get<Chord>(content.events()[1]);
  ASSERT_EQ(pasted_chord.notes.size(), 2u);
  EXPECT_EQ(pasted_chord.notes[0].pitch.letter(), Letter::kC);
  EXPECT_EQ(pasted_chord.notes[0].pitch.octave(), 4);
  EXPECT_EQ(pasted_chord.notes[0].pitch.accidental(), Accidental::kNatural);
  EXPECT_EQ(pasted_chord.notes[1].pitch.letter(), Letter::kE);
  EXPECT_EQ(pasted_chord.notes[1].pitch.octave(), 4);
  EXPECT_EQ(pasted_chord.notes[1].pitch.accidental(), Accidental::kFlat);

  // References must be clean post-paste.
  const std::vector<NotationDiagnostic> ref_diags =
      validate_voice_references(content);
  EXPECT_TRUE(ref_diags.empty());
}

TEST(ClipboardCommandTest, PasteCreatesStaveWhenDestinationLaneHasNoStaves) {
  // Construct through normal Project add_track / add_node / set_timeline
  // WITHOUT calling ensure_stave on any lane. Assert has_stave false,
  // paste creates the stave, undo restores exact no-stave lane, redo
  // succeeds, and an untouched lane remains unchanged.
  Project    proj{ProjectId::generate(), "NoStave"};
  const auto tid =
      proj.add_track("A", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = proj.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = proj.add_node("N");
  Node*        node_p  = proj.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  std::vector<Measure> measures;
  measures.push_back(
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  auto timeline = NodeTimeline::create(
      measures, {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));

  // No ensure_stave call — lane is truly empty.
  ASSERT_FALSE(node_p->lane(track_id)->has_stave(stave_id));

  // Capture untouched lane for later comparison.
  const auto tid2 =
      proj.add_track("B", StaffLayout::single_staff(), *MidiChannel::create(1));
  ASSERT_TRUE(tid2.has_value());
  const TrackId untouched_track = *tid2;
  // B's lane is also untouched — capture it.
  TrackLane untouched_before = *node_p->lane(untouched_track);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), quarter())})}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(proj).ok());

  // Stave created by paste's ensure_stave.
  EXPECT_TRUE(node_p->lane(track_id)->has_stave(stave_id));
  const auto* stave = node_p->lane(track_id)->stave(stave_id);
  ASSERT_NE(stave, nullptr);
  EXPECT_TRUE(stave->voice(kVoice1)
                  .check_complete(node_p->timeline()->node_end())
                  .ok());

  // Untouched lane unchanged.
  EXPECT_TRUE(*node_p->lane(untouched_track) == untouched_before);

  // Undo restores exact no-stave lane.
  const TrackLane post_execute = *node_p->lane(track_id);
  ASSERT_TRUE(command.undo(proj).ok());
  ASSERT_FALSE(node_p->lane(track_id)->has_stave(stave_id));
  EXPECT_TRUE(*node_p->lane(untouched_track) == untouched_before);

  // Redo succeeds and creates stave again.
  ASSERT_TRUE(command.redo(proj).ok());
  EXPECT_TRUE(node_p->lane(track_id)->has_stave(stave_id));
  EXPECT_TRUE(*node_p->lane(untouched_track) == untouched_before);
}

TEST(ClipboardCommandTest,
     SparseOrdinalsZeroAndTwoOnGrandStaffCompactCorrectly) {
  // Fragment track_shape declares 3 staves but only {0, 2} are used.
  // Distinct referenced ordinals compact onto the two destination staves.
  Fixture fx;

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{3}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kG, 4), quarter())})}});

  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const TrackLane* lane = fx.node()->lane(fx.track_a);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_treble)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kC, 4));
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_bass)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kG, 4));
}

TEST(ClipboardCommandTest,
     SparseOrdinalsAcrossMultipleTracksCompactIndependently) {
  Project    project{ProjectId::generate(), "Sparse multi-track"};
  const auto track_a = project.add_track("A", StaffLayout::grand_staff(),
                                         *MidiChannel::create(0));
  const auto track_b = project.add_track("B", StaffLayout::grand_staff(),
                                         *MidiChannel::create(1));
  ASSERT_TRUE(track_a.has_value());
  ASSERT_TRUE(track_b.has_value());

  const Track* first_track  = project.find_active_track(*track_a);
  const Track* second_track = project.find_active_track(*track_b);
  ASSERT_NE(first_track, nullptr);
  ASSERT_NE(second_track, nullptr);
  const StaveId first_treble  = first_track->layout().staves()[0].id;
  const StaveId first_bass    = first_track->layout().staves()[1].id;
  const StaveId second_treble = second_track->layout().staves()[0].id;
  const StaveId second_bass   = second_track->layout().staves()[1].id;

  const NodeId node_id = project.add_node("Node");
  Node*        node    = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{first_treble, Clef::kTreble},
       StaveDefinition{first_bass, Clef::kBass},
       StaveDefinition{second_treble, Clef::kTreble},
       StaveDefinition{second_bass, Clef::kBass}});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{3}, FragmentTrackShape{3}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kD, 4), quarter())})},
       FragmentVoicePart{
           1, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kE, 4), quarter())})},
       FragmentVoicePart{
           1, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kF, 4), quarter())})}});
  const PasteAnchor anchor{node_id, *track_a, first_treble, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(project).ok());

  const auto pasted_pitch = [node](TrackId track, StaveId stave) {
    return std::get<Note>(
               node->lane(track)->stave(stave)->voice(kVoice1).events()[0])
        .pitch;
  };
  EXPECT_EQ(pasted_pitch(*track_a, first_treble), pitch(Letter::kC, 4));
  EXPECT_EQ(pasted_pitch(*track_a, first_bass), pitch(Letter::kD, 4));
  EXPECT_EQ(pasted_pitch(*track_b, second_treble), pitch(Letter::kE, 4));
  EXPECT_EQ(pasted_pitch(*track_b, second_bass), pitch(Letter::kF, 4));
}

TEST(ClipboardCommandTest, SparseOrdinalsOnMultiTrackOverflowNonFirstAnchor) {
  // Two-track fragment with sparse staves, anchor on second track.
  // Verify multi-track overflow correctly counts per track.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}, FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           1, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kG, 4), quarter())})}});

  // Anchor on track_b (single staff). track_ordinal 0 → track_b itself,
  // track_ordinal 1 → would need another active track.
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest,
     MultiTrackMultiStavePasteLifecycleAndAtomicStaleRejection) {
  Fixture                fx;
  const TrackLane        before_a = fx.lane_of(fx.track_a);
  const TrackLane        before_b = fx.lane_of(fx.track_b);
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}, FragmentTrackShape{1}},
      {FragmentVoicePart{0, 0, kVoice1,
                         build_voice({make_note(pitch(Letter::kC), half())})},
       FragmentVoicePart{
           0, 1, kVoice2,
           build_voice({make_note(pitch(Letter::kE, 3), half())})},
       FragmentVoicePart{1, 0, kVoice3,
                         build_voice({make_note(pitch(Letter::kG), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)},
       FragmentPedalSpan{1, 0, rat(1, 4), rat(1, 2)}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane after_a = fx.lane_of(fx.track_a);
  const TrackLane after_b = fx.lane_of(fx.track_b);
  EXPECT_FALSE(after_a == before_a);
  EXPECT_FALSE(after_b == before_b);
  ASSERT_NE(after_a.pedal_spans(fx.stave_a_bass), nullptr);
  ASSERT_NE(after_b.pedal_spans(fx.stave_b), nullptr);

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

  *fx.node()->lane(fx.track_a) = TrackLane{};
  const TrackLane redo_stale_a = fx.lane_of(fx.track_a);
  const TrackLane redo_stale_b = fx.lane_of(fx.track_b);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == redo_stale_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == redo_stale_b);

  *fx.node()->lane(fx.track_a) = before_a;
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == after_b);
}

}  // namespace clipboard_test
