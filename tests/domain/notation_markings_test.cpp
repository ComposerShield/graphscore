// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::BeamOverride;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::event_id;
using graphscore::event_is_beamable;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Hairpin;
using graphscore::HairpinDirection;
using graphscore::Letter;
using graphscore::make_beam_override;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::NotationEntityId;
using graphscore::NoteValue;
using graphscore::PedalSpan;
using graphscore::Rational;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaveId;
using graphscore::TrackLane;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

namespace {

SpelledPitch pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

}  // namespace

TEST(DynamicMarkingTest, AttachesToAnEvent) {
  const VoiceEvent     note = make_note(pitch(Letter::kC), quarter());
  const DynamicMarking marking =
      make_dynamic_marking(event_id(note), Dynamic::kFf);
  EXPECT_EQ(marking.at_event, event_id(note));
  EXPECT_EQ(marking.value, Dynamic::kFf);
}

TEST(DynamicMarkingTest, VoiceContentAccumulatesDynamics) {
  VoiceContent     voice;
  const VoiceEvent note = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(note).ok());
  voice.add_dynamic(make_dynamic_marking(event_id(note), Dynamic::kP));
  ASSERT_EQ(voice.dynamics().size(), 1u);
  EXPECT_EQ(voice.dynamics()[0].value, Dynamic::kP);
}

TEST(HairpinTest, SpanReferencesBothEndpoints) {
  const VoiceEvent start   = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent end     = make_note(pitch(Letter::kD), quarter());
  const Hairpin    hairpin = make_hairpin(event_id(start), event_id(end),
                                          HairpinDirection::kCrescendo);
  EXPECT_EQ(hairpin.start_event, event_id(start));
  EXPECT_EQ(hairpin.end_event, event_id(end));
  EXPECT_EQ(hairpin.direction, HairpinDirection::kCrescendo);
}

TEST(HairpinTest, DiminuendoDirectionRoundTrips) {
  const Hairpin hairpin =
      make_hairpin(NotationEntityId::generate(), NotationEntityId::generate(),
                   HairpinDirection::kDiminuendo);
  EXPECT_EQ(hairpin.direction, HairpinDirection::kDiminuendo);
}

TEST(SlurTest, SpanReferencesBothEndpoints) {
  const VoiceEvent start = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent end   = make_note(pitch(Letter::kD), quarter());
  const Slur       slur  = make_slur(event_id(start), event_id(end));
  EXPECT_EQ(slur.start_event, event_id(start));
  EXPECT_EQ(slur.end_event, event_id(end));
}

TEST(PedalSpanTest, HoldsExactStartAndEnd) {
  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  EXPECT_EQ(span.start, Rational(0));
  EXPECT_EQ(span.end, *Rational::create(1, 2));
}

TEST(PedalSpanTest, TrackLaneScopesSpansPerStave) {
  TrackLane     lane;
  const StaveId stave_id = StaveId::generate();
  EXPECT_EQ(lane.pedal_spans(stave_id), nullptr);

  lane.add_pedal_span(stave_id,
                      make_pedal_span(Rational(0), *Rational::create(1, 2)));
  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].end, *Rational::create(1, 2));
}

TEST(BeamOverrideTest, EventIsBeamableForEighthAndShorterOnly) {
  const VoiceEvent quarter_event = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent eighth_event  = make_note(pitch(Letter::kC), eighth());
  const VoiceEvent rest_event    = make_rest(eighth());
  EXPECT_FALSE(event_is_beamable(quarter_event));
  EXPECT_TRUE(event_is_beamable(eighth_event));
  EXPECT_FALSE(event_is_beamable(rest_event));
}

TEST(BeamOverrideTest, ManualBreakReferencesAdjacentBeamableEvents) {
  const VoiceEvent   first    = make_note(pitch(Letter::kC), eighth());
  const VoiceEvent   second   = make_note(pitch(Letter::kD), eighth());
  const BeamOverride override = make_beam_override(
      BeamOverride::Kind::kBreak, {event_id(first), event_id(second)});
  EXPECT_EQ(override.kind, BeamOverride::Kind::kBreak);
  ASSERT_EQ(override.events.size(), 2u);
  EXPECT_EQ(override.events[0], event_id(first));
  EXPECT_EQ(override.events[1], event_id(second));
}

TEST(BeamOverrideTest, ManualJoinRoundTrips) {
  const VoiceEvent   first    = make_note(pitch(Letter::kC), eighth());
  const VoiceEvent   second   = make_note(pitch(Letter::kD), eighth());
  const BeamOverride override = make_beam_override(
      BeamOverride::Kind::kJoin, {event_id(first), event_id(second)});
  EXPECT_EQ(override.kind, BeamOverride::Kind::kJoin);
}

TEST(GraceGroupTest, AttachesOrderedGraceNotesToAPrincipalEvent) {
  const VoiceEvent principal = make_note(pitch(Letter::kC), quarter());

  const GraceNote first{pitch(Letter::kB), eighth(),
                        GraceNoteType::kAcciaccatura, /*slashed=*/true};
  const GraceNote second{pitch(Letter::kA), eighth(),
                         GraceNoteType::kAppoggiatura,
                         /*slashed=*/false};

  const GraceGroup group =
      make_grace_group(event_id(principal), {first, second});

  EXPECT_EQ(group.principal_event, event_id(principal));
  ASSERT_EQ(group.notes.size(), 2u);
  EXPECT_EQ(group.notes[0].pitch, pitch(Letter::kB));
  EXPECT_EQ(group.notes[0].type, GraceNoteType::kAcciaccatura);
  EXPECT_TRUE(group.notes[0].slashed);
  EXPECT_EQ(group.notes[1].pitch, pitch(Letter::kA));
  EXPECT_EQ(group.notes[1].type, GraceNoteType::kAppoggiatura);
  EXPECT_FALSE(group.notes[1].slashed);
}

TEST(GraceGroupTest, VoiceContentAccumulatesGraceGroups) {
  VoiceContent     voice;
  const VoiceEvent principal = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(principal).ok());

  voice.add_grace_group(make_grace_group(
      event_id(principal), {GraceNote{pitch(Letter::kB), eighth()}}));

  ASSERT_EQ(voice.grace_groups().size(), 1u);
  EXPECT_EQ(voice.grace_groups()[0].principal_event, event_id(principal));
  EXPECT_EQ(voice.grace_groups()[0].notes.size(), 1u);
}
