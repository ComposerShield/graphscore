// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <unordered_set>
#include <utility>
#include <vector>

namespace clipboard_test {

TEST(ClipboardCommandTest, CutFullMeasureSelectionClearsAllFourVoices) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole())});
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice2,
                         {make_note(pitch(Letter::kD), whole())});

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Measure 0's length (1) is shorter than node_end() (4), so region3
  // pads with additional rests after the cut range's own rest.
  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  for (const Voice voice : {kVoice1, kVoice2, kVoice3, kVoice4}) {
    ASSERT_GE(stave->voice(voice).events().size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Rest>(stave->voice(voice).events()[0]));
    EXPECT_EQ(std::get<Rest>(stave->voice(voice).events()[0]).duration,
              whole());
  }
}

TEST(ClipboardCommandTest, CompleteMeasureExtractPasteAllVoicesExactLifecycle) {
  Fixture                   fx;
  const std::vector<Voice>  voices = {kVoice1, kVoice2, kVoice3, kVoice4};
  const std::vector<Letter> source_pitches = {Letter::kC, Letter::kD,
                                              Letter::kE, Letter::kF};

  for (std::size_t i = 0; i < voices.size(); ++i) {
    fx.assign_and_complete(fx.track_a, fx.stave_a_treble, voices[i],
                           {make_note(pitch(source_pitches[i]), whole())});
    fx.assign(fx.track_b, fx.stave_b, voices[i],
              build_voice({make_note(pitch(Letter::kG), whole()),
                           make_note(pitch(Letter::kA), whole()),
                           make_note(pitch(Letter::kB), whole()),
                           make_note(pitch(Letter::kC, 5), whole())}));
  }

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});
  const FragmentExtraction extraction = extract_fragment(fx.project, selection);
  ASSERT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  EXPECT_EQ(extraction.fragment->span_length(), Rational(1));
  ASSERT_EQ(extraction.fragment->parts().size(), 4u);

  const TrackLane               source_before      = fx.lane_of(fx.track_a);
  const TrackLane               destination_before = fx.lane_of(fx.track_b);
  std::vector<NotationEntityId> fragment_ids;
  for (const FragmentVoicePart& part : extraction.fragment->parts())
    collect_ids(part.content, fragment_ids);

  const PasteAnchor    anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(2)};
  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane destination_after = fx.lane_of(fx.track_b);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == source_before);

  for (std::size_t i = 0; i < voices.size(); ++i) {
    const VoiceContent& before =
        destination_before.stave(fx.stave_b)->voice(voices[i]);
    const VoiceContent& after =
        destination_after.stave(fx.stave_b)->voice(voices[i]);
    ASSERT_EQ(before.events().size(), 4u);
    ASSERT_EQ(after.events().size(), 4u);
    EXPECT_TRUE(after.events()[0] == before.events()[0]);
    EXPECT_TRUE(after.events()[1] == before.events()[1]);
    EXPECT_TRUE(after.events()[3] == before.events()[3]);
    EXPECT_EQ(std::get<Note>(after.events()[2]).pitch,
              pitch(source_pitches[i]));
    EXPECT_NE(event_id(after.events()[2]),
              event_id(extraction.fragment->parts()[i].content.events()[0]));
  }
  std::vector<NotationEntityId> pasted_ids;
  for (const Voice voice : voices) {
    collect_ids(destination_after.stave(fx.stave_b)->voice(voice), pasted_ids);
  }
  const std::unordered_set<NotationEntityId> pasted_set(pasted_ids.begin(),
                                                        pasted_ids.end());
  for (const NotationEntityId id : fragment_ids)
    EXPECT_FALSE(pasted_set.contains(id));

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == source_before);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == destination_before);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == source_before);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == destination_after);
}

TEST(ClipboardCommandTest,
     ContiguousMeasuresPasteToExactExplicitScopesAndUndoRedo) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), whole()),
                         make_note(pitch(Letter::kD), whole()),
                         make_note(pitch(Letter::kE), whole()),
                         make_note(pitch(Letter::kF), whole())}));
  fx.assign(fx.track_b, fx.stave_b, kVoice1,
            build_voice({make_note(pitch(Letter::kG), whole()),
                         make_note(pitch(Letter::kA), whole()),
                         make_note(pitch(Letter::kB), whole()),
                         make_note(pitch(Letter::kC, 5), whole())}));
  fx.assign(fx.track_a, fx.stave_a_bass, kVoice1,
            build_voice({make_note(pitch(Letter::kF, 3), whole()),
                         make_note(pitch(Letter::kF, 3), whole()),
                         make_note(pitch(Letter::kF, 3), whole()),
                         make_note(pitch(Letter::kF, 3), whole())}));
  for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
    fx.assign(fx.track_a, fx.stave_a_treble, voice, rest_filled(fx.node_end()));
    fx.assign(fx.track_a, fx.stave_a_bass, voice, rest_filled(fx.node_end()));
    fx.assign(fx.track_b, fx.stave_b, voice, rest_filled(fx.node_end()));
  }

  const Selection source = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0, 2},
       FullMeasureItem{fx.node_id, fx.track_b, fx.stave_b, 0, 2}});
  const FragmentExtraction extraction = extract_fragment(fx.project, source);
  ASSERT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  const TrackLane before_a = fx.lane_of(fx.track_a);
  const TrackLane before_b = fx.lane_of(fx.track_b);

  const PasteAnchor    destination{fx.node_id,
                                fx.track_b,
                                fx.stave_b,
                                Rational(2),
                                   {PasteScope{fx.track_b, fx.stave_b},
                                    PasteScope{fx.track_a, fx.stave_a_bass}}};
  PasteFragmentCommand command(*extraction.fragment, destination);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane after_a  = fx.lane_of(fx.track_a);
  const TrackLane after_b  = fx.lane_of(fx.track_b);
  const auto&     b_events = after_b.stave(fx.stave_b)->voice(kVoice1).events();
  const auto&     bass_events =
      after_a.stave(fx.stave_a_bass)->voice(kVoice1).events();
  EXPECT_EQ(std::get<Note>(b_events[2]).pitch, pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(b_events[3]).pitch, pitch(Letter::kD));
  EXPECT_EQ(std::get<Note>(bass_events[2]).pitch, pitch(Letter::kG));
  EXPECT_EQ(std::get<Note>(bass_events[3]).pitch, pitch(Letter::kA));

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == after_b);
}

TEST(ClipboardCommandTest,
     ExplicitScopeMismatchStaleScopeAndOverflowRejectAtomically) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole()),
                          make_note(pitch(Letter::kD), whole())});
  const Selection source = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0, 2}});
  const FragmentExtraction extraction = extract_fragment(fx.project, source);
  ASSERT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  const TrackLane before_a = fx.lane_of(fx.track_a);
  const TrackLane before_b = fx.lane_of(fx.track_b);

  const std::vector<PasteAnchor> rejected = {
      PasteAnchor{fx.node_id,
                  fx.track_b,
                  fx.stave_b,
                  Rational(0),
                  {PasteScope{fx.track_b, fx.stave_b},
                   PasteScope{fx.track_a, fx.stave_a_bass}}},
      PasteAnchor{fx.node_id,
                  fx.track_b,
                  fx.stave_b,
                  Rational(0),
                  {PasteScope{fx.track_b, StaveId::generate()}}},
      PasteAnchor{fx.node_id,
                  fx.track_b,
                  fx.stave_b,
                  Rational(3),
                  {PasteScope{fx.track_b, fx.stave_b}}}};
  for (const PasteAnchor& anchor : rejected) {
    PasteFragmentCommand command(*extraction.fragment, anchor);
    EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
    EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
    EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
  }
}

TEST(ClipboardCommandTest, MultiMeasureFullSelectionCutIsRejectedAtomically) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole()),
                          make_note(pitch(Letter::kD), whole())});
  const TrackLane before    = fx.lane_of(fx.track_a);
  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0, 2}});
  CutFragmentCommand command(selection);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(command.fragment().has_value());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, ContiguousMeasurePasteSupportsMatchingMeterChanges) {
  Project    project{ProjectId::generate(), "Changing meter"};
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  const StaveId stave_id =
      project.find_active_track(*track_id)->layout().staves()[0].id;
  const NodeId node_id = project.add_node("Node");
  Node*        node    = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  const TimeSignature        three_four = *TimeSignature::create(3, 4);
  const TimeSignature        four_four  = *TimeSignature::create(4, 4);
  const std::vector<Measure> measures   = {
      Measure{three_four, KeySignature{}}, Measure{four_four, KeySignature{}},
      Measure{three_four, KeySignature{}}, Measure{four_four, KeySignature{}}};
  auto timeline = NodeTimeline::create(
      measures, {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));
  node->lane(*track_id)->ensure_stave(stave_id);
  VoiceContent& voice = node->lane(*track_id)->stave(stave_id)->voice(kVoice1);
  for (const Letter letter : {Letter::kC, Letter::kD, Letter::kE})
    ASSERT_TRUE(voice.append(make_note(pitch(letter), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kF), whole())).ok());
  for (const Letter letter : {Letter::kG, Letter::kA, Letter::kB})
    ASSERT_TRUE(voice.append(make_note(pitch(letter), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC, 5), whole())).ok());
  for (const Voice other : {kVoice2, kVoice3, kVoice4}) {
    node->lane(*track_id)->stave(stave_id)->voice(other) =
        rest_filled(node->timeline()->node_end());
  }

  const Selection source = *FullMeasureSet::create(
      {FullMeasureItem{node_id, *track_id, stave_id, 0, 2}});
  const FragmentExtraction extraction = extract_fragment(project, source);
  ASSERT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  EXPECT_EQ(extraction.fragment->span_length(), rat(7, 4));
  const PasteAnchor    destination{node_id,
                                *track_id,
                                stave_id,
                                rat(7, 4),
                                   {PasteScope{*track_id, stave_id}}};
  PasteFragmentCommand command(*extraction.fragment, destination);
  EXPECT_TRUE(command.execute(project).ok());
}

TEST(ClipboardCommandTest, PasteMeterGateAcceptsMatchingTimeSignature) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_TRUE(command.execute(fx.project).ok());
}

TEST(ClipboardCommandTest,
     PasteMeterGateRejectsMismatchedTimeSignatureLeavesProjectUnchanged) {
  Project    project{ProjectId::generate(), "Meter"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  node_p->lane(track_id)->ensure_stave(stave_id);

  const std::vector<FragmentMeasureContext> mismatched = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {}, {}, mismatched);
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  const TrackLane before = *node_p->lane(track_id);
  EXPECT_FALSE(describe_paste_placement(project, fragment, anchor).has_value());
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*node_p->lane(track_id) == before);
}

TEST(ClipboardCommandTest, PasteMeterGateRejectsMultiMeterFragment) {
  Fixture                                   fx;
  const std::vector<FragmentMeasureContext> multi_meter = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(0)},
      FragmentMeasureContext{rat(1, 2), *TimeSignature::create(3, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment = make_fragment(
      Rational(1), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}}, {}, {}, {},
      multi_meter);
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  const TrackLane      before = fx.lane_of(fx.track_a);
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteMeterGateRejectsMultiMeterDestinationRange) {
  Project    project{ProjectId::generate(), "Meter"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  // measure 0: 4/4 (length 1); measure 1: 3/4 (length 3/4). node_end == 7/4.
  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)},
       Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  node_p->lane(track_id)->ensure_stave(stave_id);

  // Range [0, 9/8) overlaps all of measure 0 and the start of measure 1.
  const NotationFragment fragment =
      make_fragment(rat(9, 8), {FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(9, 8))}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  const TrackLane      before = *node_p->lane(track_id);
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*node_p->lane(track_id) == before);
}

TEST(ClipboardCommandTest, PasteMeterGatePickdownRangeUsesLastMainRegionMeter) {
  Project    project{ProjectId::generate(), "Pickdown"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  ASSERT_TRUE(node_p->timeline()->set_pickdown(rat(1, 8)).ok());
  node_p->lane(track_id)->ensure_stave(stave_id);

  // Range [1, 9/8) lies entirely in the pickdown; the governing meter is
  // the (only, hence last) main-region measure's 4/4.
  const NotationFragment fragment =
      make_fragment(rat(1, 8), {FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 8))}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_TRUE(command.execute(project).ok());
}

TEST(ClipboardCommandTest, PasteMeterGatePickdownRangeRejectsMismatchedMeter) {
  Project    project{ProjectId::generate(), "Pickdown"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  ASSERT_TRUE(node_p->timeline()->set_pickdown(rat(1, 8)).ok());
  node_p->lane(track_id)->ensure_stave(stave_id);

  const std::vector<FragmentMeasureContext> mismatched = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(3, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment =
      make_fragment(rat(1, 8), {FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 8))}},
                    {}, {}, {}, mismatched);
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(1)};

  const TrackLane      before = *node_p->lane(track_id);
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*node_p->lane(track_id) == before);
}

TEST(ClipboardCommandTest, PasteMeterGateKeySignatureDifferenceDoesNotReject) {
  Fixture                                   fx;
  const std::vector<FragmentMeasureContext> different_key = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(5)}};
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {}, {}, different_key);
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_TRUE(command.execute(fx.project).ok());
}

TEST(ClipboardCommandTest,
     PasteRejectsFragmentWithEmptyMeasureContextsLeavesLaneUnchanged) {
  Fixture                         fx;
  std::optional<NotationFragment> fragment = NotationFragment::create(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 4))}}, {}, {}, {},
      {});
  ASSERT_TRUE(fragment.has_value());
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  const TrackLane   before = fx.lane_of(fx.track_a);

  PasteFragmentCommand command(*fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

}  // namespace clipboard_test
