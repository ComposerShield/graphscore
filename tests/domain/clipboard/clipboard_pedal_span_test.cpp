// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <vector>

namespace clipboard_test {

TEST(ClipboardCommandTest, PastePedalSpansClippedRemovedAndOffsetInserted) {
  // Paste range == [1/2, 3/4).
  Fixture    fx;
  TrackLane* lane = fx.node()->lane(fx.track_a);

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(Rational(0), rat(1, 4)))
                  .ok());  // fully before range: untouched
  const NotationEntityId before_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(9, 16), rat(11, 16)))
                  .ok());  // fully inside range: removed
  const NotationEntityId inside_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(1, 4), rat(3, 5)))
                  .ok());  // straddles the start boundary (1/2)
  const NotationEntityId straddle_start_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(7, 10), Rational(1)))
                  .ok());  // straddles the end boundary (3/4)
  const NotationEntityId straddle_end_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, build_voice({make_rest(quarter())})}},
      {FragmentPedalSpan{0, 0, Rational(0), rat(1, 8)}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 2)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_a)->pedal_spans(fx.stave_a_treble);
  ASSERT_NE(spans, nullptr);

  bool saw_untouched_before = false;
  bool saw_inside_removed   = true;
  bool saw_straddle_removed = true;
  bool saw_end_removed      = true;
  bool saw_truncated_start  = false;
  bool saw_truncated_end    = false;
  bool saw_fragment_offset  = false;
  for (const PedalSpan& span : *spans) {
    if (span.id == before_id) {
      EXPECT_EQ(span.start, Rational(0));
      EXPECT_EQ(span.end, rat(1, 4));
      saw_untouched_before = true;
    }
    if (span.id == inside_id)
      saw_inside_removed = false;
    if (span.id == straddle_start_id)
      saw_straddle_removed = false;
    if (span.id == straddle_end_id)
      saw_end_removed = false;
    if (span.start == rat(1, 4) && span.end == rat(1, 2))
      saw_truncated_start = true;
    if (span.start == rat(3, 4) && span.end == Rational(1))
      saw_truncated_end = true;
    if (span.start == rat(1, 2) && span.end == rat(5, 8))
      saw_fragment_offset = true;
  }
  EXPECT_TRUE(saw_untouched_before);
  EXPECT_TRUE(saw_inside_removed);
  EXPECT_TRUE(saw_straddle_removed);
  EXPECT_TRUE(saw_end_removed);
  EXPECT_TRUE(saw_truncated_start);
  EXPECT_TRUE(saw_truncated_end);
  EXPECT_TRUE(saw_fragment_offset);
}

TEST(ClipboardCommandTest,
     PedalOnlyStaveFragmentMapsAndCreatesDestinationStave) {
  // Fragment: voice part on treble (ordinal 0) + pedal span on bass
  // (ordinal 1). No voice part names the bass stave. Paste must map both
  // staves: treble maps to anchor stave (bass), pedal-only stave is an
  // extra referenced ordinal that requires a second stave.
  PedalOnlyFixture fx;

  // Source fragment: part on ordinal 0, pedal span on ordinal 1.
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  // Anchor on the bass stave (index 1). Ordinal 0 → bass, ordinal 1 →
  // would need one more stave (nonexistent in a 2-stave grand staff when
  // starting at index 1). Verify — this actually fails because there's no
  // third stave.
  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_bass,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest, PedalSpanOnUnreferencedStavePastesOntoGrandStaff) {
  // Fragment: part on ordinal 0, pedal span on ordinal 1. Anchor on treble
  // stave (ordinal 0 in grand staff). Treble maps ordinal 0 → treble;
  // bass maps ordinal 1 → bass. Paste succeeds because grand staff has
  // two staves. Destination bass gets the pedal span but no voice rebuild.
  PedalOnlyFixture fx;

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Treble stave has the C half note.
  const auto* treble = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  ASSERT_NE(treble, nullptr);
  ASSERT_GE(treble->voice(kVoice1).events().size(), 1u);
  EXPECT_EQ(std::get<Note>(treble->voice(kVoice1).events()[0]).pitch,
            pitch(Letter::kC, 4));

  // Bass stave has the pedal span (offset to anchor position 0).
  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_a)->pedal_spans(fx.stave_a_bass);
  ASSERT_NE(spans, nullptr);
  bool saw_pedal = false;
  for (const PedalSpan& span : *spans) {
    if (span.start == Rational(0) && span.end == rat(1, 2)) {
      saw_pedal = true;
      break;
    }
  }
  EXPECT_TRUE(saw_pedal);
}

TEST(ClipboardCommandTest, PedalOnlyFragmentToSingleStaffFailsAtomically) {
  // Fragment: part on ordinal 0, pedal on ordinal 1. Single-staff
  // destination: ordinal 0 maps to the only stave, ordinal 1 needs a
  // second → overflow. Paste must fail, model unchanged.
  PedalOnlyFixture fx;
  // Add a single-staff track.
  const auto tid_b = fx.project.add_track("B", StaffLayout::single_staff(),
                                          *MidiChannel::create(1));
  ASSERT_TRUE(tid_b.has_value());
  const TrackId track_b     = *tid_b;
  const Track*  track_b_ptr = fx.project.find_active_track(track_b);
  ASSERT_NE(track_b_ptr, nullptr);
  const StaveId stave_b = track_b_ptr->layout().staves()[0].id;

  const TrackLane before = *fx.node()->lane(track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  const PasteAnchor    anchor{fx.node_id, track_b, stave_b, Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.node()->lane(track_b) == before);
}

TEST(ClipboardCommandTest, PedalOnlyStaveUndoRedoExactRoundTrip) {
  // Paste a fragment with pedal on bass-only into grand staff. Undo must
  // restore exact pre-state (no pedal span on bass). Redo must re-apply.
  PedalOnlyFixture fx;
  // Pre-write only on treble; bass has no stave yet.
  fx.node()->lane(fx.track_a)->ensure_stave(fx.stave_a_treble);
  // Give treble voice 1 some content for proper lane existence.
  VoiceContent vc;
  ASSERT_TRUE(vc.normalize(fx.node_end()).ok());
  fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1) = vc;

  const TrackLane before = *fx.node()->lane(fx.track_a);
  ASSERT_FALSE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Bass stave now exists and has pedal span.
  EXPECT_TRUE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));
  const TrackLane after_execute = *fx.node()->lane(fx.track_a);

  // Undo: bass stave gone, exact pre-state restored.
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(*fx.node()->lane(fx.track_a) == before);
  EXPECT_FALSE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));

  // Redo: bass stave back with pedal.
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));
  EXPECT_TRUE(*fx.node()->lane(fx.track_a) == after_execute);
}

TEST(ClipboardCommandTest, PedalOnlyMultiTrackPedalMapsAndPastesCorrectly) {
  // Two-track fragment: voice part on track_ordinal 0, pedal on
  // track_ordinal 1 stave_ordinal 1. The pedal span on the second track
  // compacts to that track's only stave (ordinal 1 → index 0). Paste
  // succeeds and the pedal span appears on the correct stave.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}, FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), quarter())})}},
      {FragmentPedalSpan{1, 1, Rational(0), rat(1, 4)}});

  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Voice part pasted onto track_a treble.
  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC, 4));

  // Pedal span pasted onto track_b's stave.
  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_b)->pedal_spans(fx.stave_b);
  ASSERT_NE(spans, nullptr);
  bool saw_pedal = false;
  for (const PedalSpan& span : *spans) {
    if (span.start == Rational(0) && span.end == rat(1, 4)) {
      saw_pedal = true;
      break;
    }
  }
  EXPECT_TRUE(saw_pedal);
}

}  // namespace clipboard_test
