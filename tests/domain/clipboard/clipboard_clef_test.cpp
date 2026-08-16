// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <utility>
#include <vector>

namespace clipboard_test {

TEST(ClipboardCommandTest,
     PasteMeterMismatchRejectionLeavesClefLanesUntouched) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());
  const ClefLane  before_clef = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane before_lane = fx.lane_of(fx.track_a);

  const std::vector<FragmentMeasureContext> mismatched = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(3, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}}, {},
      {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}}, {}, mismatched);
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_clef);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);
}

TEST(ClipboardCommandTest,
     PasteLaneValidationFailureLeavesClefLanesUntouchedEvenWithClefChanges) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2,
            build_voice({make_note(pitch(Letter::kD), quarter())}));
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());
  const ClefLane  before_clef = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane before_lane = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_clef);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);
}

TEST(ClipboardCommandTest,
     PasteAppliesInteriorClefChangeOnMappedDestinationStaves) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}, FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))},
                     FragmentVoicePart{1, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {},
                    {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass},
                     FragmentClefChange{1, 0, rat(1, 4), Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* treble_lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(treble_lane, nullptr);
  EXPECT_EQ(treble_lane->clef_at(rat(1, 4)), Clef::kBass);

  // track_ordinal 1 walks forward to the next active track (track_b) at
  // that track's own stave_ordinal 0 (stave_b), not offset by the anchor.
  const ClefLane* track_b_lane = fx.timeline()->clef_lane(fx.stave_b);
  ASSERT_NE(track_b_lane, nullptr);
  EXPECT_EQ(track_b_lane->clef_at(rat(1, 4)), Clef::kAlto);
}

TEST(ClipboardCommandTest, PasteNeverAppliesFragmentClefAtOrigin) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {}, {FragmentStaveContext{0, 0, Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  EXPECT_TRUE(lane->changes().empty());
  EXPECT_EQ(lane->clef_at(Rational(0)), Clef::kTreble);
}

TEST(ClipboardCommandTest, PasteReplacesDestinationClefChangesInsideRange) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kTenor}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(lane->clef_at(rat(1, 8)), Clef::kTenor);

  std::size_t matches_at_position = 0;
  for (const ClefChange& change : lane->changes()) {
    if (change.position == rat(1, 8))
      ++matches_at_position;
  }
  EXPECT_EQ(matches_at_position, 1u);
}

TEST(ClipboardCommandTest, PasteContainmentPreservesPrevailingClefAfterRange) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(lane->clef_at(rat(1, 4)), Clef::kBass);
  // Contained: everything from range_end (1/2) onward, which this paste
  // never touches, still shows the pre-paste prevailing clef.
  EXPECT_EQ(lane->clef_at(rat(1, 2)), Clef::kTreble);
  EXPECT_EQ(lane->clef_at(Rational(3)), Clef::kTreble);

  bool has_reassertion = false;
  for (const ClefChange& change : lane->changes()) {
    if (change.position == rat(1, 2) && change.clef == Clef::kTreble)
      has_reassertion = true;
  }
  EXPECT_TRUE(has_reassertion);
}

TEST(ClipboardCommandTest, PasteSkipsClefReassertionWhenRangeEndEqualsNodeEnd) {
  Fixture        fx;
  const Rational node_end        = fx.node_end();
  const Rational span            = rat(1, 2);
  const Rational anchor_position = node_end - span;

  const NotationFragment fragment =
      make_fragment(span, {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(span)}}, {},
                    {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           anchor_position};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  ASSERT_EQ(lane->changes().size(), 1u);
  EXPECT_EQ(lane->changes().front().position, anchor_position + rat(1, 4));
}

TEST(ClipboardCommandTest,
     PasteSkipsReassertionWhenDestinationChangeAlreadySitsAtRangeEnd) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 2), Clef::kTenor)
                  .ok());
  const ClefLane before = *fx.timeline()->clef_lane(fx.stave_a_treble);

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);

  std::size_t matches_at_range_end = 0;
  for (const ClefChange& change : lane->changes()) {
    if (change.position == rat(1, 2)) {
      ++matches_at_range_end;
      EXPECT_EQ(change.clef, Clef::kTenor);
    }
  }
  EXPECT_EQ(matches_at_range_end, 1u);

  const Rational near_node_end = fx.node_end() - rat(1, 8);
  EXPECT_EQ(lane->clef_at(rat(1, 2)), before.clef_at(rat(1, 2)));
  EXPECT_EQ(lane->clef_at(near_node_end), before.clef_at(near_node_end));
}

TEST(ClipboardCommandTest, PasteClefApplicationDoesNotAlterStoredPitches) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 5), quarter()),
                       make_note(pitch(Letter::kG, 2), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC, 5));
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kG, 2));
}

TEST(ClipboardCommandTest, PasteCreatesClefLaneWhenAbsentAndUndoRemovesIt) {
  Project    project{ProjectId::generate(), "NoClefLane"};
  const auto tid = project.add_track(
      "A", StaffLayout::single_staff(Clef::kAlto), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  std::vector<Measure> measures;
  measures.push_back(
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  // No StaveDefinition passed to create(): the resulting timeline has no
  // clef lane at all for this stave, simulating a track added after the
  // node's timeline was created (Project::add_track only calls
  // Node::ensure_lane, never touches NodeTimeline's clef lanes).
  auto timeline = NodeTimeline::create(measures, {});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  node_p->lane(track_id)->ensure_stave(stave_id);
  ASSERT_FALSE(node_p->timeline()->has_clef_lane(stave_id));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kBass}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(project).ok());

  ASSERT_TRUE(node_p->timeline()->has_clef_lane(stave_id));
  const ClefLane* lane = node_p->timeline()->clef_lane(stave_id);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(lane->default_clef(), Clef::kAlto);
  EXPECT_EQ(lane->clef_at(rat(1, 8)), Clef::kBass);
  const ClefLane after_execute = *lane;

  ASSERT_TRUE(command.undo(project).ok());
  EXPECT_FALSE(node_p->timeline()->has_clef_lane(stave_id));

  ASSERT_TRUE(command.redo(project).ok());
  ASSERT_TRUE(node_p->timeline()->has_clef_lane(stave_id));
  EXPECT_TRUE(*node_p->timeline()->clef_lane(stave_id) == after_execute);
}

TEST(ClipboardCommandTest, PasteClefExecuteUndoRedoRestoresExactly) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());
  const ClefLane before = *fx.timeline()->clef_lane(fx.stave_a_treble);

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const ClefLane after_execute = *fx.timeline()->clef_lane(fx.stave_a_treble);
  EXPECT_FALSE(after_execute == before);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == after_execute);
}

TEST(ClipboardCommandTest, PasteUndoRejectsStaleClefLaneContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const ClefLane  after_execute = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane after_execute_lane = fx.lane_of(fx.track_a);

  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(3, 8), Clef::kTenor)
                  .ok());
  const ClefLane stale = *fx.timeline()->clef_lane(fx.stave_a_treble);

  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == stale);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_execute_lane);

  ASSERT_TRUE(
      fx.timeline()->restore_clef_lane(fx.stave_a_treble, after_execute).ok());
  ASSERT_TRUE(command.undo(fx.project).ok());
}

TEST(ClipboardCommandTest, PasteRedoRejectsStaleClefLaneContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.undo(fx.project).ok());
  const ClefLane  after_undo = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane after_undo_lane = fx.lane_of(fx.track_a);

  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(3, 8), Clef::kTenor)
                  .ok());
  const ClefLane stale = *fx.timeline()->clef_lane(fx.stave_a_treble);

  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == stale);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_undo_lane);

  ASSERT_TRUE(
      fx.timeline()->restore_clef_lane(fx.stave_a_treble, after_undo).ok());
  ASSERT_TRUE(command.redo(fx.project).ok());
}

TEST(ClipboardCommandTest,
     PasteWipesStaleDestinationClefChangeWithNoFragmentClefChanges) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 4), Clef::kBass)
                  .ok());

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  for (const ClefChange& change : lane->changes())
    EXPECT_NE(change.position, rat(1, 4));
  // Inside the wiped range, the removed change no longer governs, so the
  // stave's default clef prevails.
  EXPECT_EQ(lane->clef_at(rat(1, 4)), Clef::kTreble);
  // Containment: the pre-edit prevailing clef at range_end (Bass, from the
  // now-removed change at 1/4) is reasserted, so nothing at or after the
  // paste's own range changes from what it showed before the paste.
  EXPECT_EQ(lane->clef_at(rat(1, 2)), Clef::kBass);
  EXPECT_EQ(lane->clef_at(Rational(3)), Clef::kBass);
}

TEST(ClipboardCommandTest,
     PasteWipesStaleClefsConsistentlyAcrossNamedAndUnnamedStaves) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 4), Clef::kBass)
                  .ok());
  ASSERT_TRUE(
      fx.timeline()->add_clef_change(fx.stave_b, rat(1, 4), Clef::kAlto).ok());

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}, FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))},
                     FragmentVoicePart{1, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(3, 8), Clef::kTenor}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Named stave: fragment names a clef change at 3/8, so the destination's
  // stale change at 1/4 is replaced; containment reasserts that stave's own
  // pre-edit clef at range_end (Bass, from the removed 1/4 change).
  const ClefLane* treble_lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(treble_lane, nullptr);
  for (const ClefChange& change : treble_lane->changes())
    EXPECT_NE(change.position, rat(1, 4));
  EXPECT_EQ(treble_lane->clef_at(rat(3, 8)), Clef::kTenor);
  EXPECT_EQ(treble_lane->clef_at(rat(1, 2)), Clef::kBass);

  // Unnamed stave: the fragment names no clef change on track_ordinal 1 at
  // all, yet its notes are wholly replaced too, so its stale change at 1/4
  // is wiped exactly the same way, with containment reasserting its own
  // pre-edit clef at range_end (Alto, from the removed 1/4 change).
  const ClefLane* track_b_lane = fx.timeline()->clef_lane(fx.stave_b);
  ASSERT_NE(track_b_lane, nullptr);
  for (const ClefChange& change : track_b_lane->changes())
    EXPECT_NE(change.position, rat(1, 4));
  EXPECT_EQ(track_b_lane->clef_at(rat(1, 4)), Clef::kTreble);
  EXPECT_EQ(track_b_lane->clef_at(rat(1, 2)), Clef::kAlto);
}

TEST(ClipboardCommandTest,
     PasteAppliesClefChangeOnStaveUnreferencedByVoicePartOrPedalSpan) {
  Fixture         fx;
  const ClefLane  before_treble = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const ClefLane  before_bass   = *fx.timeline()->clef_lane(fx.stave_a_bass);
  const TrackLane before_lane   = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 1, rat(1, 8), Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Application: the clef change on bass (ordinal 1), named by no voice
  // part or pedal span, is applied.
  const ClefLane* bass_lane = fx.timeline()->clef_lane(fx.stave_a_bass);
  ASSERT_NE(bass_lane, nullptr);
  EXPECT_EQ(bass_lane->clef_at(rat(1, 8)), Clef::kAlto);
  // Containment: nothing at or after range_end differs from the pre-edit
  // prevailing clef, even on this unreferenced stave.
  EXPECT_EQ(bass_lane->clef_at(rat(1, 4)), Clef::kBass);
  EXPECT_EQ(bass_lane->clef_at(Rational(3)), Clef::kBass);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_treble);

  // Exact undo.
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_bass) == before_bass);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_treble);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_EQ(fx.timeline()->clef_lane(fx.stave_a_bass)->clef_at(rat(1, 8)),
            Clef::kAlto);
}

TEST(ClipboardCommandTest,
     PasteClefChangeOnLowerOrdinalCompactsVoicePartToHigherStave) {
  Fixture         fx;
  const TrackLane before_lane   = fx.lane_of(fx.track_a);
  const ClefLane  before_treble = *fx.timeline()->clef_lane(fx.stave_a_treble);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 1, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Application: the voice part (fragment ordinal 1) lands on bass
  // (base+1), not treble (base+0) -- treble is consumed by ordinal 0's
  // clef-only reference.
  const VoiceContent& bass_voice =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_bass)->voice(kVoice1);
  ASSERT_GE(bass_voice.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(bass_voice.events()[0]).pitch, pitch(Letter::kC));
  // Treble's own voice content is untouched -- ordinal 0 names no voice
  // part.
  const VoiceContent& treble_voice =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  for (const VoiceEvent& event : treble_voice.events())
    EXPECT_TRUE(std::holds_alternative<Rest>(event));

  const ClefLane* treble_lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(treble_lane, nullptr);
  EXPECT_EQ(treble_lane->clef_at(rat(1, 8)), Clef::kAlto);
  // Containment.
  EXPECT_EQ(treble_lane->clef_at(rat(1, 4)), Clef::kTreble);
  EXPECT_EQ(treble_lane->clef_at(Rational(3)), Clef::kTreble);

  // Exact undo.
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_treble);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_EQ(fx.timeline()->clef_lane(fx.stave_a_treble)->clef_at(rat(1, 8)),
            Clef::kAlto);
}

}  // namespace clipboard_test
