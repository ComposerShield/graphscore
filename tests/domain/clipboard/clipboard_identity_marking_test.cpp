// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <unordered_set>
#include <utility>
#include <vector>

namespace clipboard_test {

TEST(ClipboardCommandTest, PasteLeavesMusicOutsideRangeByteIdentical) {
  Fixture      fx;
  const Note   n1          = make_note(pitch(Letter::kC), quarter());
  const Note   n2          = make_note(pitch(Letter::kD), quarter());
  const Note   n3          = make_note(pitch(Letter::kE), quarter());
  const Note   n4          = make_note(pitch(Letter::kF), quarter());
  VoiceContent destination = build_voice({n1, n2, n3, n4});
  ASSERT_TRUE(
      destination.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(destination.add_slur(make_slur(n3.id, n4.id)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, destination);

  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_a)
                  ->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(Rational(0), rat(1, 4)))
                  .ok());
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_a)
                  ->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(3, 4), Rational(1)))
                  .ok());

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 8)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  // Paste at [1/8, 1/4): n1 is truncated at 1/8 (original id kept, tie
  // severed), B3 pasted, n2-n4 preserved.  node_end() (4) exceeds
  // content, so region3 pads with rests.
  ASSERT_GE(content.events().size(), 5u);
  // Truncated n1: same id, same pitch, shorter duration.
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  EXPECT_EQ(std::get<Note>(content.events()[0]).id, n1.id);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(content.events()[0]).duration, eighth());
  EXPECT_FALSE(std::get<Note>(content.events()[0]).tied_to_next);
  // Pasted B3 at [1/8, 1/4).
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kB, 3));
  // n2, n3, n4 preserved verbatim.
  EXPECT_TRUE(std::get<Note>(content.events()[2]) == n2);
  EXPECT_TRUE(std::get<Note>(content.events()[3]) == n3);
  EXPECT_TRUE(std::get<Note>(content.events()[4]) == n4);

  ASSERT_EQ(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event, n1.id);
  EXPECT_EQ(content.dynamics()[0].value, Dynamic::kFf);
  ASSERT_EQ(content.slurs().size(), 1u);
  EXPECT_EQ(content.slurs()[0].start_event, n3.id);
  EXPECT_EQ(content.slurs()[0].end_event, n4.id);

  // Pedal span at [0, 1/4) is clipped by the paste range [1/8, 1/4).
  // Only the portion before [0, 1/8) survives with a freshly-generated id.
  // Span at [3/4, 1) is outside the range, id preserved.
  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_a)->pedal_spans(fx.stave_a_treble);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  bool saw_before = false;
  bool saw_after  = false;
  for (const PedalSpan& span : *spans) {
    if (span.start == Rational(0) && span.end == rat(1, 8))
      saw_before = true;
    if (span.start == rat(3, 4) && span.end == Rational(1))
      saw_after = true;
  }
  EXPECT_TRUE(saw_before);
  EXPECT_TRUE(saw_after);
}

TEST(ClipboardCommandTest, PastingSameFragmentTwiceYieldsDisjointIds) {
  Fixture      fx;
  const Note   note_a       = make_note(pitch(Letter::kC), quarter());
  VoiceContent part_content = build_voice({note_a});
  ASSERT_TRUE(
      part_content.add_dynamic(make_dynamic_marking(note_a.id, Dynamic::kFf))
          .ok());
  const NotationFragment fragment =
      make_fragment(quarter().resolved(), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part_content}});

  std::vector<NotationEntityId> fragment_ids;
  collect_ids(fragment.parts()[0].content, fragment_ids);

  fx.assign_and_complete(fx.track_a, fx.stave_a_bass, kVoice1,
                         {make_note(pitch(Letter::kA, 3), whole())});
  const NotationEntityId preexisting_id =
      std::get<Note>(fx.node()
                         ->lane(fx.track_a)
                         ->stave(fx.stave_a_bass)
                         ->voice(kVoice1)
                         .events()[0])
          .id;

  const PasteAnchor anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  const PasteAnchor anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            rat(1, 2)};

  PasteFragmentCommand first(fragment, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());
  PasteFragmentCommand second(fragment, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  std::vector<NotationEntityId> all_ids;
  collect_ids(content, all_ids);

  const std::unordered_set<NotationEntityId> unique(all_ids.begin(),
                                                    all_ids.end());
  EXPECT_EQ(all_ids.size(), unique.size());
  for (const NotationEntityId id : fragment_ids)
    EXPECT_FALSE(unique.contains(id));
  EXPECT_FALSE(unique.contains(preexisting_id));
}

TEST(ClipboardCommandTest, PasteDropsMarkingsAnchoredToRemovedEventsAndSpans) {
  Fixture      fx;
  const Note   e1          = make_note(pitch(Letter::kC), quarter());
  const Note   e2          = make_note(pitch(Letter::kD), quarter());
  const Note   e3          = make_note(pitch(Letter::kE), quarter());
  const Note   e4          = make_note(pitch(Letter::kF), quarter());
  VoiceContent destination = build_voice({e1, e2, e3, e4});
  ASSERT_TRUE(
      destination.add_dynamic(make_dynamic_marking(e1.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(
      destination.add_dynamic(make_dynamic_marking(e3.id, Dynamic::kPp)).ok());
  ASSERT_TRUE(destination.add_slur(make_slur(e2.id, e3.id)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, destination);

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
  ASSERT_EQ(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event, e1.id);
  EXPECT_TRUE(content.slurs().empty());
}

TEST(ClipboardCommandTest,
     PasteMarkingsOnSurvivingLeftBoundaryEventArePreserved) {
  // C half note at [0, 1/2) with a dynamic marking (ff) on it.
  // Paste at [1/4, 1/2): left region preserves truncated C.  Its dynamic
  // must survive.
  Fixture      fx;
  const Note   c_half = make_note(pitch(Letter::kC, 4), half());
  VoiceContent dest   = build_voice({c_half});
  ASSERT_TRUE(
      dest.add_dynamic(make_dynamic_marking(c_half.id, Dynamic::kFf)).ok());
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
  ASSERT_EQ(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event,
            std::get<Note>(content.events()[0]).id);
  EXPECT_EQ(content.dynamics()[0].value, Dynamic::kFf);
}

TEST(ClipboardCommandTest, PasteWithAllMarkingsGeneratesDisjointIds) {
  // Build a fragment containing dynamics, hairpin, slur, beam override,
  // and grace group, then paste it twice.  Every id in each paste must be
  // fresh (not present in the fragment, not colliding across pastes).
  Fixture fx;
  // Use eighth notes so the beam override validates (beamable events only).
  const Note   n1   = make_note(pitch(Letter::kC, 4), eighth());
  const Note   n2   = make_note(pitch(Letter::kD, 4), eighth());
  VoiceContent part = build_voice({n1, n2});
  ASSERT_TRUE(part.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kMf)).ok());
  ASSERT_TRUE(
      part.add_hairpin(make_hairpin(n1.id, n2.id, HairpinDirection::kCrescendo))
          .ok());
  ASSERT_TRUE(part.add_slur(make_slur(n1.id, n2.id)).ok());
  ASSERT_TRUE(
      part.add_beam_override(
              make_beam_override(BeamOverride::Kind::kJoin, {n1.id, n2.id}))
          .ok());
  std::vector<graphscore::GraceNote> grace_notes;
  grace_notes.push_back(graphscore::GraceNote{
      graphscore::NotationEntityId{}, pitch(Letter::kF, 5), eighth(),
      graphscore::GraceNoteType::kAcciaccatura, true});
  ASSERT_TRUE(
      part.add_grace_group(make_grace_group(n2.id, std::move(grace_notes)))
          .ok());

  std::vector<NotationEntityId> fragment_ids;
  collect_ids(part, fragment_ids);

  const NotationFragment frag =
      make_fragment(rat(1, 4), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part}});

  // First paste.
  const PasteAnchor    anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  PasteFragmentCommand first(frag, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());

  const VoiceContent& content1 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  std::vector<NotationEntityId> first_ids;
  collect_ids(content1, first_ids);

  // Verify no fragment id appears in the destination.
  const std::unordered_set<NotationEntityId> first_set(first_ids.begin(),
                                                       first_ids.end());
  for (const NotationEntityId fid : fragment_ids)
    EXPECT_FALSE(first_set.contains(fid));

  // Verify all referencing markings are rewired correctly.
  ASSERT_GE(content1.dynamics().size(), 1u);
  EXPECT_EQ(content1.dynamics()[0].at_event, event_id(content1.events()[0]));
  ASSERT_GE(content1.hairpins().size(), 1u);
  EXPECT_EQ(content1.hairpins()[0].start_event, event_id(content1.events()[0]));
  EXPECT_EQ(content1.hairpins()[0].end_event, event_id(content1.events()[1]));
  ASSERT_GE(content1.slurs().size(), 1u);
  EXPECT_EQ(content1.slurs()[0].start_event, event_id(content1.events()[0]));
  EXPECT_EQ(content1.slurs()[0].end_event, event_id(content1.events()[1]));
  ASSERT_GE(content1.beam_overrides().size(), 1u);
  const auto& beam = content1.beam_overrides()[0];
  ASSERT_EQ(beam.events.size(), 2u);
  EXPECT_EQ(beam.events[0], event_id(content1.events()[0]));
  EXPECT_EQ(beam.events[1], event_id(content1.events()[1]));
  ASSERT_GE(content1.grace_groups().size(), 1u);
  EXPECT_EQ(content1.grace_groups()[0].principal_event,
            event_id(content1.events()[1]));

  // Second paste at a disjoint position.
  const PasteAnchor    anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(1)};
  PasteFragmentCommand second(frag, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content2 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  std::vector<NotationEntityId> second_ids;
  collect_ids(content2, second_ids);

  // No id appears twice anywhere in the joined content.
  const std::unordered_set<NotationEntityId> all_set(second_ids.begin(),
                                                     second_ids.end());
  EXPECT_EQ(second_ids.size(), all_set.size());
}

TEST(ClipboardCommandTest, PasteRemovesBeamOverrideCrossingBoundary) {
  // A beam override on events n1,n2,n3.  Paste replaces n2.  The beam
  // should be dropped (fewer than two survive or incomplete set).
  Fixture      fx;
  const Note   n1   = make_note(pitch(Letter::kC, 4), quarter());
  const Note   n2   = make_note(pitch(Letter::kD, 4), quarter());
  const Note   n3   = make_note(pitch(Letter::kE, 4), quarter());
  VoiceContent dest = build_voice({n1, n2, n3});
  ASSERT_TRUE(
      dest.add_beam_override(make_beam_override(BeamOverride::Kind::kJoin,
                                                {n1.id, n2.id, n3.id}))
          .ok());
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
  EXPECT_TRUE(content.beam_overrides().empty());
}

TEST(ClipboardCommandTest, PastedChordAllChordNoteIdsRegeneratedDisjoint) {
  // Paste a fragment containing a chord twice. Every ChordNote id must be
  // freshly regenerated each time, disjoint within and across pastes.
  Fixture                fx;
  std::vector<ChordNote> chord_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), false},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), false}};
  const Chord  chord        = make_chord(quarter(), std::move(chord_notes));
  VoiceContent part_content = build_voice({chord});

  const NotationFragment fragment =
      make_fragment(quarter().resolved(), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part_content}});

  const PasteAnchor anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  const PasteAnchor anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            rat(1, 4)};

  PasteFragmentCommand first(fragment, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());
  PasteFragmentCommand second(fragment, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[1]));

  const Chord& chord1 = std::get<Chord>(content.events()[0]);
  const Chord& chord2 = std::get<Chord>(content.events()[1]);

  // Each chord must have unique top-level id.
  EXPECT_NE(chord1.id, chord2.id);

  // Collect all ChordNote ids across both pasted chords.
  std::vector<NotationEntityId> all_notehead_ids;
  for (const ChordNote& cn : chord1.notes)
    all_notehead_ids.push_back(cn.id);
  for (const ChordNote& cn : chord2.notes)
    all_notehead_ids.push_back(cn.id);

  const std::unordered_set<NotationEntityId> unique(all_notehead_ids.begin(),
                                                    all_notehead_ids.end());
  EXPECT_EQ(all_notehead_ids.size(), unique.size());
}

TEST(ClipboardCommandTest, MarkingSurvivalAndRemovalAcrossPasteBoundary) {
  // Events: n1, n2, n3 (all eighth). Markings on n1 (dynamic), n2→n3
  // (slur), n1→n2 (hairpin), n1+n2+n3 (beam override), n2 with grace group.
  // Paste replaces [1/4, 1/2), beginning exactly at n3's attack. Verify:
  //   - Dynamic on n1 survives (n1 is in left region).
  //   - Slur n2→n3 and beam override drop because n3 is removed.
  //   - Hairpin n1→n2 survives because both events precede the range.
  //   - Grace group on n2 survives because n2 remains immediately before
  //     the replaced range.
  Fixture      fx;
  const Note   n1   = make_note(pitch(Letter::kC, 4), eighth());
  const Note   n2   = make_note(pitch(Letter::kD, 4), eighth());
  const Note   n3   = make_note(pitch(Letter::kE, 4), eighth());
  VoiceContent dest = build_voice({n1, n2, n3});
  ASSERT_TRUE(dest.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kMf)).ok());
  ASSERT_TRUE(dest.add_slur(make_slur(n2.id, n3.id)).ok());
  ASSERT_TRUE(
      dest.add_hairpin(make_hairpin(n1.id, n2.id, HairpinDirection::kCrescendo))
          .ok());
  ASSERT_TRUE(
      dest.add_beam_override(make_beam_override(BeamOverride::Kind::kJoin,
                                                {n1.id, n2.id, n3.id}))
          .ok());
  const graphscore::GraceNote grace_note{
      graphscore::NotationEntityId{}, pitch(Letter::kF, 5), eighth(),
      graphscore::GraceNoteType::kAcciaccatura, true};
  const auto grace_group = make_grace_group(n2.id, {grace_note});
  ASSERT_TRUE(dest.add_grace_group(grace_group).ok());
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

  // Dynamic on n1 survives; its at_event rewired to left n1.
  ASSERT_GE(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event, event_id(content.events()[0]));
  EXPECT_EQ(content.dynamics()[0].value, Dynamic::kMf);

  // Slur n2→n3 and beam override n1,n2,n3 are dropped (n3 removed).
  EXPECT_TRUE(content.slurs().empty());
  EXPECT_TRUE(content.beam_overrides().empty());

  // Hairpin n1→n2 and the GraceGroup survive because both principals/endpoints
  // are before the replaced range [1/4, 1/2).
  EXPECT_GE(content.hairpins().size(), 1u);
  ASSERT_EQ(content.grace_groups().size(), 1u);
  const auto& pasted_group = content.grace_groups()[0];
  EXPECT_EQ(pasted_group.id, grace_group.id);
  EXPECT_EQ(pasted_group.principal_event, n2.id);
  ASSERT_EQ(pasted_group.notes.size(), 1u);
  ASSERT_EQ(grace_group.notes.size(), 1u);
  EXPECT_EQ(pasted_group.notes[0].id, grace_group.notes[0].id);
  EXPECT_EQ(pasted_group.notes[0].pitch, grace_group.notes[0].pitch);
  EXPECT_EQ(pasted_group.notes[0].duration, grace_group.notes[0].duration);
  EXPECT_EQ(pasted_group.notes[0].type, grace_group.notes[0].type);
  EXPECT_EQ(pasted_group.notes[0].slashed, grace_group.notes[0].slashed);

  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, DestinationGraceGroupRemovedWhenPrincipalReplaced) {
  Fixture                     fx;
  const Note                  n1 = make_note(pitch(Letter::kC, 4), eighth());
  const Note                  n2 = make_note(pitch(Letter::kD, 4), eighth());
  const Note                  n3 = make_note(pitch(Letter::kE, 4), eighth());
  VoiceContent                destination = build_voice({n1, n2, n3});
  const graphscore::GraceNote grace_note{
      NotationEntityId{}, pitch(Letter::kF, 5), eighth(),
      graphscore::GraceNoteType::kAppoggiatura, false};
  ASSERT_TRUE(
      destination.add_grace_group(make_grace_group(n2.id, {grace_note})).ok());
  ASSERT_TRUE(validate_voice_references(destination).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(destination));

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 8)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  EXPECT_TRUE(content.grace_groups().empty());
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, FragmentMarkingReferenceRewireAndTwoPastesDisjoint) {
  // Fragment with dynamics, hairpin, slur, beam override, grace group.
  // Paste twice; verify each paste's marking references are correctly
  // rewired to the pasted events of THAT paste, and both paste sets are
  // ID-disjoint.
  Fixture      fx;
  const Note   n1   = make_note(pitch(Letter::kC, 4), eighth());
  const Note   n2   = make_note(pitch(Letter::kD, 4), eighth());
  VoiceContent part = build_voice({n1, n2});
  ASSERT_TRUE(part.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(
      part.add_hairpin(make_hairpin(n1.id, n2.id, HairpinDirection::kCrescendo))
          .ok());
  ASSERT_TRUE(part.add_slur(make_slur(n1.id, n2.id)).ok());
  ASSERT_TRUE(
      part.add_beam_override(
              make_beam_override(BeamOverride::Kind::kJoin, {n1.id, n2.id}))
          .ok());
  std::vector<graphscore::GraceNote> grace_notes;
  grace_notes.push_back(graphscore::GraceNote{
      graphscore::NotationEntityId{}, pitch(Letter::kG, 5), eighth(),
      graphscore::GraceNoteType::kAcciaccatura, true});
  ASSERT_TRUE(
      part.add_grace_group(make_grace_group(n2.id, std::move(grace_notes)))
          .ok());

  const NotationFragment frag =
      make_fragment(rat(1, 4), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part}});

  // First paste at [0, 1/4).
  const PasteAnchor    anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  PasteFragmentCommand first(frag, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());

  const VoiceContent& content1 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content1.events().size(), 2u);
  const NotationEntityId e1_0 = event_id(content1.events()[0]);
  const NotationEntityId e1_1 = event_id(content1.events()[1]);

  ASSERT_GE(content1.dynamics().size(), 1u);
  EXPECT_EQ(content1.dynamics()[0].at_event, e1_0);
  ASSERT_GE(content1.hairpins().size(), 1u);
  EXPECT_EQ(content1.hairpins()[0].start_event, e1_0);
  EXPECT_EQ(content1.hairpins()[0].end_event, e1_1);
  ASSERT_GE(content1.slurs().size(), 1u);
  EXPECT_EQ(content1.slurs()[0].start_event, e1_0);
  EXPECT_EQ(content1.slurs()[0].end_event, e1_1);
  ASSERT_GE(content1.beam_overrides().size(), 1u);
  EXPECT_EQ(content1.beam_overrides()[0].events[0], e1_0);
  EXPECT_EQ(content1.beam_overrides()[0].events[1], e1_1);
  ASSERT_GE(content1.grace_groups().size(), 1u);
  EXPECT_EQ(content1.grace_groups()[0].principal_event, e1_1);

  // Second paste at [1/2, 3/4).
  const PasteAnchor    anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            rat(1, 2)};
  PasteFragmentCommand second(frag, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content2 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  // After two pastes with a gap: events[0-1] first paste, events[2] gap
  // rest, events[3-4] second paste.
  ASSERT_GE(content2.events().size(), 5u);
  const NotationEntityId e2_0 = event_id(content2.events()[3]);
  const NotationEntityId e2_1 = event_id(content2.events()[4]);

  // Verify second paste's markings rewire correctly.
  std::size_t dynamics_count = 0;
  for (const auto& d : content2.dynamics()) {
    if (d.at_event == e2_0)
      dynamics_count++;
  }
  EXPECT_GE(dynamics_count, 1u);

  // Hairpin and slur from the second paste must reference e2_0, e2_1.
  bool saw_hairpin = false;
  for (const auto& hp : content2.hairpins()) {
    if (hp.start_event == e2_0 && hp.end_event == e2_1)
      saw_hairpin = true;
  }
  EXPECT_TRUE(saw_hairpin);
  bool saw_slur = false;
  for (const auto& sl : content2.slurs()) {
    if (sl.start_event == e2_0 && sl.end_event == e2_1)
      saw_slur = true;
  }
  EXPECT_TRUE(saw_slur);

  // Collect all ids and verify disjointness.
  std::vector<NotationEntityId> all_ids;
  collect_ids(content2, all_ids);
  const std::unordered_set<NotationEntityId> all_set(all_ids.begin(),
                                                     all_ids.end());
  EXPECT_EQ(all_ids.size(), all_set.size());
}

}  // namespace clipboard_test
