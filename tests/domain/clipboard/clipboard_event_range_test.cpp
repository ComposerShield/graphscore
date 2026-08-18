// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace clipboard_test {

TEST(ClipboardCommandTest, PasteStartBoundaryStraddlePreservesAttackSeversTie) {
  // Finding 2: left retained region preserves the attack of a note
  // straddling the paste boundary.  C half note at [0, 1/2), paste at
  // [1/4, 3/8): the left region [0, 1/4) keeps a truncated C quarter with
  // tied_to_next = false (tie severed); the pasted B occupies [1/4, 3/8);
  // the right region converts the C tail [3/8, 1/2) to rests (attack was
  // inside the replaced range).
  Fixture                fx;
  const Note             c_half = make_note(pitch(Letter::kC), half());
  const NotationEntityId c_id   = c_half.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({c_half}));

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 3u);
  // Left region: truncated C quarter, original id preserved, tie severed.
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  const Note& left_note = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_note.id, c_id);
  EXPECT_EQ(left_note.pitch, pitch(Letter::kC));
  EXPECT_EQ(left_note.duration, quarter());
  EXPECT_FALSE(left_note.tied_to_next);
  // Middle region: pasted B.
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kB));
  // Right region: C tail converted to rests.
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[2]));
  EXPECT_EQ(std::get<Rest>(content.events()[2]).duration, eighth());
}

TEST(ClipboardCommandTest, PasteEndBoundaryStraddleBecomesNormalizedRests) {
  Fixture fx;
  // C occupies [0, 1/4), exactly the removed range's start; D occupies
  // [1/4, 3/4), straddling the paste range's end at 3/8.
  const Duration dotted_quarter = *Duration::create(NoteValue::kQuarter, 1);
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), half())}));

  const NotationFragment fragment = make_fragment(
      rat(3, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), dotted_quarter)})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kB));
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[1]));
  EXPECT_EQ(std::get<Rest>(content.events()[1]).duration, dotted_quarter);
}

TEST(ClipboardCommandTest,
     PasteStartBoundaryTupletStraddleFailsModelUnchanged) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 24)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteEndBoundaryTupletStraddleFailsModelUnchanged) {
  Fixture fx;
  // Three tuplet eighths tile [0, 1/12), [1/12, 1/6), [1/6, 1/4). A paste
  // range of [0, 1/8) leaves the second tuplet note straddling the range's
  // end (1/8 falls strictly inside [1/12, 1/6)).
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest,
     PasteWhollyContainedTupletRemapsIdentityAndSupportsUndoRedo) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  fx.assign_and_complete(fx.track_b, fx.stave_b, kVoice1,
                         {make_note(pitch(Letter::kG), quarter()),
                          make_note(pitch(Letter::kA), quarter()),
                          make_note(pitch(Letter::kB), quarter()),
                          make_note(pitch(Letter::kC, 5), quarter())});

  const Selection source = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 4)}}});
  const FragmentExtraction extraction = extract_fragment(fx.project, source);
  ASSERT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  ASSERT_EQ(extraction.fragment->parts().size(), 1u);
  ASSERT_EQ(extraction.fragment->parts()[0].content.events().size(), 3u);
  const auto source_tuplet_group =
      std::get<Note>(extraction.fragment->parts()[0].content.events()[0])
          .tuplet_group;
  ASSERT_TRUE(source_tuplet_group.has_value());

  std::vector<NotationEntityId> fragment_ids;
  collect_ids(extraction.fragment->parts()[0].content, fragment_ids);
  const TrackLane   before = fx.lane_of(fx.track_b);
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, rat(1, 2)};

  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane     after        = fx.lane_of(fx.track_b);
  const VoiceContent& pasted       = after.stave(fx.stave_b)->voice(kVoice1);
  const auto          first_pasted = pasted.find_event_index_at(rat(1, 2));
  ASSERT_TRUE(first_pasted.has_value());
  ASSERT_GE(pasted.events().size(), *first_pasted + 3U);

  std::vector<NotationEntityId> pasted_ids;
  for (std::size_t i = 0; i < 3U; ++i) {
    const Note& note = std::get<Note>(pasted.events()[*first_pasted + i]);
    EXPECT_EQ(note.duration, tuplet_eighth());
    ASSERT_TRUE(note.tuplet_group.has_value());
    pasted_ids.push_back(note.id);
  }
  EXPECT_EQ(std::get<Note>(pasted.events()[*first_pasted]).tuplet_group,
            std::get<Note>(pasted.events()[*first_pasted + 1]).tuplet_group);
  EXPECT_EQ(std::get<Note>(pasted.events()[*first_pasted]).tuplet_group,
            std::get<Note>(pasted.events()[*first_pasted + 2]).tuplet_group);
  EXPECT_NE(std::get<Note>(pasted.events()[*first_pasted]).tuplet_group,
            source_tuplet_group);
  for (const NotationEntityId fragment_id : fragment_ids) {
    EXPECT_EQ(std::ranges::count(pasted_ids, fragment_id), 0);
  }

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_b) == after);
}

TEST(ClipboardCommandTest,
     PasteLeftBoundaryChordStraddlePreservesAttackSeversChordTies) {
  // A chord with two noteheads (C4 tied, E4 tied), spanning [0, 1/2).
  // Paste at [1/4, 1/2): left region preserves truncated chord at
  // [0, 1/4) with original id, both noteheads' ties severed.
  Fixture                fx;
  std::vector<ChordNote> chord_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), true},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), true}};
  const Chord            chord    = make_chord(half(), std::move(chord_notes));
  const NotationEntityId chord_id = chord.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({chord}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  // Left region: truncated chord with original id.
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[0]));
  const Chord& left_chord = std::get<Chord>(content.events()[0]);
  EXPECT_EQ(left_chord.id, chord_id);
  EXPECT_EQ(left_chord.duration, quarter());
  ASSERT_EQ(left_chord.notes.size(), 2u);
  EXPECT_FALSE(left_chord.notes[0].tied_to_next);
  EXPECT_FALSE(left_chord.notes[1].tied_to_next);
}

TEST(ClipboardCommandTest, PasteSeveresOutgoingTieWhenTargetIsRemoved) {
  // C4 quarter tied to D4 quarter.  Paste replaces D4.  C4's tie must
  // be severed (tied_to_next = false).
  Fixture                fx;
  const Note             c4 = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note             d4 = make_note(pitch(Letter::kD, 4), quarter(), false);
  const NotationEntityId c_id = c4.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({c4, d4}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_FALSE(left_c.tied_to_next);
}

TEST(ClipboardCommandTest, CutSeveresOutgoingTieWhenTailIsInCutRange) {
  // C4 quarter tied to D4 quarter.  Cut replaces D4 with rests.  C4's tie
  // is severed.
  Fixture                fx;
  const Note             c4 = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note             d4 = make_note(pitch(Letter::kD, 4), quarter(), false);
  const NotationEntityId c_id = c4.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({c4, d4}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_FALSE(left_c.tied_to_next);
}

TEST(ClipboardCommandTest,
     PasteRightBoundaryStraddleAttackRemovedBecomesRests) {
  // D half note at [0, 1/2).  Paste at [0, 1/4): the right region
  // [1/4, 1/2) of D should become rests (attack was in the replaced range).
  Fixture    fx;
  const Note d_half = make_note(pitch(Letter::kD, 4), half());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({d_half}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC, 4));
  // D's tail [1/4, 1/2) → rests.
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[1]));
  EXPECT_EQ(std::get<Rest>(content.events()[1]).duration, quarter());
}

TEST(ClipboardCommandTest,
     PasteBothBoundariesStraddledDifferentDestinationEvents) {
  // C half note at [0, 1/2), D half note at [1/2, 1).
  // Paste at [1/4, 3/4): C straddles left boundary (preserved attack),
  // D straddles right boundary (attack removed → rests).
  Fixture                fx;
  const Note             c_half = make_note(pitch(Letter::kC, 4), half());
  const Note             d_half = make_note(pitch(Letter::kD, 4), half());
  const NotationEntityId c_id   = c_half.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({c_half, d_half}));

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), half())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 3u);
  // Left: truncated C quarter, original id, tie severed.
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_EQ(left_c.duration, quarter());
  // Middle: pasted B half.
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kB, 3));
  // Right: D tail [3/4, 1) → rests.
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[2]));
  EXPECT_EQ(std::get<Rest>(content.events()[2]).duration, quarter());
}

TEST(ClipboardCommandTest,
     ValidSamePitchTieChainLeftAttackPreservedOutgoingSevered) {
  // C4 quarter tied to C4 quarter (valid same-pitch tie). Paste replaces
  // the second C4. Left C4 retained with original id, its outgoing tie
  // must be severed. validate_voice_references clean before and after.
  Fixture    fx;
  const Note c4_tied   = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note c4_untied = make_note(pitch(Letter::kC, 4), quarter(), false);
  const NotationEntityId c_id = c4_tied.id;
  VoiceContent           dest = build_voice({c4_tied, c4_untied});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kD, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_EQ(left_c.pitch, pitch(Letter::kC, 4));
  EXPECT_FALSE(left_c.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest,
     DifferentPitchPastedReplacementSeveresRetainedOutgoingTie) {
  // C4 tied to C4. Paste B3 into the position of the second C4. The
  // retained C4's outgoing tie severs deterministically — even though
  // the pasted note is a different pitch.
  Fixture      fx;
  const Note   c4_tied   = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note   c4_untied = make_note(pitch(Letter::kC, 4), quarter(), false);
  VoiceContent dest      = build_voice({c4_tied, c4_untied});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_FALSE(left_c.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, CutExactBoundarySeveresOutgoingDestinationTie) {
  // C4 tied to C4. Cut replaces the second C4 with rests. Left C4 retained
  // with tie severed.
  Fixture      fx;
  const Note   c4_tied   = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note   c4_untied = make_note(pitch(Letter::kC, 4), quarter(), false);
  VoiceContent dest      = build_voice({c4_tied, c4_untied});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_FALSE(left_c.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, PartialChordNoteTiesSomeRetainedSomeRemoved) {
  // Chord (C4 tied, E4 untied) followed by another chord as tie target.
  // Paste replaces the second chord. The retained truncated first chord
  // has both noteheads' ties severed since the next event is removed.
  Fixture                fx;
  std::vector<ChordNote> chord1_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), true},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), false}};
  std::vector<ChordNote> chord2_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), false},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), false}};
  const Chord  chord1 = make_chord(quarter(), std::move(chord1_notes));
  const Chord  chord2 = make_chord(quarter(), std::move(chord2_notes));
  VoiceContent dest   = build_voice({chord1, chord2});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Chord& left_chord = std::get<Chord>(content.events()[0]);
  ASSERT_EQ(left_chord.notes.size(), 2u);
  // Both severed: the next event is removed, so neither notehead keeps its
  // tie (even the E4 that was originally untied stays untied).
  EXPECT_FALSE(left_chord.notes[0].tied_to_next);
  EXPECT_FALSE(left_chord.notes[1].tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, MultiPieceLeftTruncationLastPieceOutgoingTieFalse) {
  // C whole note with tied_to_next=true, followed by another C note
  // (valid same-pitch tie chain). Paste at [1/4, 3/4). Left region
  // [0, 1/4) retains a truncated C quarter with tied_to_next forced false
  // because the tie target (second note) is in the replaced range.
  Fixture      fx;
  const Note   c_whole = make_note(pitch(Letter::kC, 4), whole(), true);
  const Note   c_tail  = make_note(pitch(Letter::kC, 4), whole(), false);
  VoiceContent dest    = build_voice({c_whole, c_tail});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kD, 4), half())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left.duration, quarter());
  EXPECT_FALSE(left.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest,
     CutFailureOnStraddlingTupletLeavesModelAndFragmentDisengaged) {
  // Simulate a reachable deterministic failure: cut a range that straddles
  // a tuplet. The model must be unchanged, fragment() must be disengaged,
  // and a fresh retry with a valid selection must succeed.
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const Selection bad_selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 24)}}});

  CutFragmentCommand bad_cut(bad_selection);
  EXPECT_EQ(bad_cut.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(bad_cut.fragment().has_value());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
  // State must still be fresh/retryable: undo and double-execute both reject.
  EXPECT_EQ(bad_cut.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(bad_cut.redo(fx.project).code(), ResultCode::kInvalidArgument);

  // A fresh command with a valid selection must succeed on the same model.
  const Selection good_selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 4)}}});
  CutFragmentCommand good_cut(good_selection);
  ASSERT_TRUE(good_cut.execute(fx.project).ok());
  ASSERT_TRUE(good_cut.fragment().has_value());
}

TEST(ClipboardCommandTest,
     PasteFailureOnStraddlingTupletLeavesModelUnchangedFreshRetry) {
  // Paste into a destination where a tuplet straddles the left boundary.
  // Model must be unchanged, command state kFresh, retry valid succeeds.
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor bad_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                               rat(1, 24)};

  PasteFragmentCommand bad_paste(fragment, bad_anchor);
  EXPECT_EQ(bad_paste.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  // Retry with valid anchor.
  const PasteAnchor    good_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                                Rational(1)};
  PasteFragmentCommand good_paste(fragment, good_anchor);
  ASSERT_TRUE(good_paste.execute(fx.project).ok());
}

}  // namespace clipboard_test
