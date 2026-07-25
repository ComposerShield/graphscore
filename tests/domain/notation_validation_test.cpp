// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Articulation;
using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_id;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::HairpinDirection;
using graphscore::Letter;
using graphscore::make_beam_override;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_slur;
using graphscore::NotationDiagnostic;
using graphscore::NotationDiagnosticCode;
using graphscore::NotationEntityId;
using graphscore::NoteValue;
using graphscore::PedalSpan;
using graphscore::Rational;
using graphscore::SpelledPitch;
using graphscore::StaveId;
using graphscore::StaveVoices;
using graphscore::TrackLane;
using graphscore::TupletRatio;
using graphscore::validate_lane_references;
using graphscore::validate_pedal_spans;
using graphscore::validate_voice_references;
using graphscore::Voice;
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

Duration tuplet_eighth() {
  return *Duration::create(NoteValue::kEighth, 0, TupletRatio::create(3, 2));
}

}  // namespace

TEST(NotationValidationTest, CleanVoiceYieldsNoDiagnostics) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent second = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());

  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(first), event_id(second),
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(
      voice.add_slur(make_slur(event_id(first), event_id(second))).ok());

  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(NotationValidationTest, ConflictingDurationArticulationsAreFlagged) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice
          .append(make_note(pitch(Letter::kC), quarter(), false,
                            {Articulation::kStaccato, Articulation::kTenuto}))
          .ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kConflictingDurationArticulation);
  EXPECT_EQ(diagnostics[0].entity_id, event_id(voice.events()[0]));
}

TEST(NotationValidationTest, AccentAndStaccatoTogetherAreNotFlagged) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice
          .append(make_note(pitch(Letter::kC), quarter(), false,
                            {Articulation::kAccent, Articulation::kStaccato}))
          .ok());
  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(NotationValidationTest, HairpinWithDanglingEndpointIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent first = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(first).ok());

  const auto hairpin =
      make_hairpin(event_id(first), NotationEntityId::generate(),
                   HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice.add_hairpin(hairpin).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kHairpinDanglingEndpoint);
  EXPECT_EQ(diagnostics[0].entity_id, hairpin.id);
}

TEST(NotationValidationTest, HairpinEndBeforeStartIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent second = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());

  const auto hairpin = make_hairpin(event_id(second), event_id(first),
                                    HairpinDirection::kDiminuendo);
  ASSERT_TRUE(voice.add_hairpin(hairpin).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kHairpinNotOrdered);
  EXPECT_EQ(diagnostics[0].entity_id, hairpin.id);
}

TEST(NotationValidationTest, SlurWithNonExistentEndpointIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent first = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(first).ok());

  const auto slur = make_slur(event_id(first), NotationEntityId::generate());
  ASSERT_TRUE(voice.add_slur(slur).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kSlurDanglingEndpoint);
  EXPECT_EQ(diagnostics[0].entity_id, slur.id);
}

TEST(NotationValidationTest, SlurWithOtherVoiceEndpointIsFlagged) {
  StaveVoices   staves;
  VoiceContent& voice_one = staves.voice(*Voice::create(1));
  VoiceContent& voice_two = staves.voice(*Voice::create(2));

  const VoiceEvent in_voice_one = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent in_voice_two = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice_one.append(in_voice_one).ok());
  ASSERT_TRUE(voice_two.append(in_voice_two).ok());

  const auto slur = make_slur(event_id(in_voice_one), event_id(in_voice_two));
  ASSERT_TRUE(voice_one.add_slur(slur).ok());

  const auto diagnostics = validate_voice_references(voice_one);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kSlurDanglingEndpoint);
  EXPECT_EQ(diagnostics[0].entity_id, slur.id);
}

TEST(NotationValidationTest, CompleteTupletGroupPasses) {
  VoiceContent voice;
  for (int i = 0; i < 3; ++i)
    ASSERT_TRUE(
        voice.append(make_note(pitch(Letter::kC), tuplet_eighth())).ok());

  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(NotationValidationTest, TruncatedTupletGroupIsFlagged) {
  VoiceContent voice;
  for (int i = 0; i < 2; ++i)
    ASSERT_TRUE(
        voice.append(make_note(pitch(Letter::kC), tuplet_eighth())).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kIncompleteTupletGroup);
  EXPECT_EQ(diagnostics[0].entity_id, event_id(voice.events()[0]));
}

TEST(NotationValidationTest, ValidBeamOverridePasses) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), eighth());
  const VoiceEvent second = make_note(pitch(Letter::kD), eighth());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());

  ASSERT_TRUE(
      voice
          .add_beam_override(make_beam_override(
              BeamOverride::Kind::kBreak, {event_id(first), event_id(second)}))
          .ok());

  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(NotationValidationTest, BeamOverrideOnNonBeamableEventIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent quarter_note = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(quarter_note).ok());

  const auto beam_override =
      make_beam_override(BeamOverride::Kind::kBreak, {event_id(quarter_note)});
  ASSERT_TRUE(voice.add_beam_override(beam_override).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kInvalidBeamOverride);
  EXPECT_EQ(diagnostics[0].entity_id, beam_override.id);
}

TEST(NotationValidationTest, BeamOverrideOnNonAdjacentEventsIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), eighth());
  const VoiceEvent middle = make_note(pitch(Letter::kD), eighth());
  const VoiceEvent last   = make_note(pitch(Letter::kE), eighth());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(middle).ok());
  ASSERT_TRUE(voice.append(last).ok());

  const auto beam_override = make_beam_override(
      BeamOverride::Kind::kJoin, {event_id(first), event_id(last)});
  ASSERT_TRUE(voice.add_beam_override(beam_override).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kInvalidBeamOverride);
  EXPECT_EQ(diagnostics[0].entity_id, beam_override.id);
}

TEST(PedalSpanValidationTest, ValidSpanPasses) {
  const PedalSpan span = make_pedal_span(Rational(0), Rational(1));
  EXPECT_TRUE(validate_pedal_spans({span}, Rational(2)).empty());
}

TEST(PedalSpanValidationTest, StartAfterEndIsFlagged) {
  const PedalSpan span        = make_pedal_span(Rational(1), Rational(0));
  const auto      diagnostics = validate_pedal_spans({span}, Rational(2));
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kPedalSpanNotOrdered);
  EXPECT_EQ(diagnostics[0].entity_id, span.id);
}

TEST(PedalSpanValidationTest, OutOfRangeIsFlagged) {
  const PedalSpan span        = make_pedal_span(Rational(1), Rational(3));
  const auto      diagnostics = validate_pedal_spans({span}, Rational(2));
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kPedalSpanOutOfRange);
  EXPECT_EQ(diagnostics[0].entity_id, span.id);
}

TEST(NotationValidationTest,
     SingleVoiceWithEveryReferentialErrorYieldsExpectedDiagnosticsInOrder) {
  VoiceContent voice;

  const VoiceEvent tied_mismatch =
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true);
  const VoiceEvent conflicting =
      make_note(pitch(Letter::kD), quarter(), false,
                {Articulation::kStaccato, Articulation::kTenuto});
  const VoiceEvent plain_one  = make_note(pitch(Letter::kE), quarter());
  const VoiceEvent plain_two  = make_note(pitch(Letter::kF), quarter());
  const VoiceEvent tuplet_one = make_note(pitch(Letter::kG), tuplet_eighth());
  const VoiceEvent tuplet_two = make_note(pitch(Letter::kG), tuplet_eighth());

  ASSERT_TRUE(voice.append(tied_mismatch).ok());
  ASSERT_TRUE(voice.append(conflicting).ok());
  ASSERT_TRUE(voice.append(plain_one).ok());
  ASSERT_TRUE(voice.append(plain_two).ok());
  ASSERT_TRUE(voice.append(tuplet_one).ok());
  ASSERT_TRUE(voice.append(tuplet_two).ok());

  const auto hairpin =
      make_hairpin(event_id(plain_one), NotationEntityId::generate(),
                   HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice.add_hairpin(hairpin).ok());

  const auto slur = make_slur(event_id(plain_one), event_id(conflicting));
  ASSERT_TRUE(voice.add_slur(slur).ok());

  const auto beam_override =
      make_beam_override(BeamOverride::Kind::kBreak, {event_id(plain_one)});
  ASSERT_TRUE(voice.add_beam_override(beam_override).ok());

  const std::vector<NotationDiagnostic> diagnostics =
      validate_voice_references(voice);

  struct ExpectedDiagnostic {
    NotationEntityId       entity_id;
    NotationDiagnosticCode code;
  };

  const std::vector<ExpectedDiagnostic> expected = {
      {event_id(tied_mismatch), NotationDiagnosticCode::kTiePitchMismatch},
      {event_id(conflicting),
       NotationDiagnosticCode::kConflictingDurationArticulation},
      {hairpin.id, NotationDiagnosticCode::kHairpinDanglingEndpoint},
      {slur.id, NotationDiagnosticCode::kSlurNotOrdered},
      {beam_override.id, NotationDiagnosticCode::kInvalidBeamOverride},
      {event_id(tuplet_one), NotationDiagnosticCode::kIncompleteTupletGroup},
  };

  ASSERT_EQ(diagnostics.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(diagnostics[i].entity_id, expected[i].entity_id) << "index " << i;
    EXPECT_EQ(diagnostics[i].code, expected[i].code) << "index " << i;
    EXPECT_FALSE(diagnostics[i].message.empty()) << "index " << i;
  }
}

TEST(NotationValidationTest,
     CleanLaneAcrossStavesAndPedalSpansYieldsNoDiagnostics) {
  TrackLane     lane;
  const StaveId stave_id = StaveId::generate();
  lane.ensure_stave(stave_id);

  StaveVoices* voices = lane.stave(stave_id);
  ASSERT_NE(voices, nullptr);
  VoiceContent&    voice  = voices->voice(*Voice::create(1));
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent second = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());
  ASSERT_TRUE(
      voice.add_slur(make_slur(event_id(first), event_id(second))).ok());

  ASSERT_TRUE(
      lane.add_pedal_span(stave_id, make_pedal_span(Rational(0), Rational(1)))
          .ok());

  EXPECT_TRUE(validate_lane_references(lane, Rational(2)).empty());
}

TEST(NotationValidationTest, LaneWithDanglingPedalSpanIsFlagged) {
  TrackLane     lane;
  const StaveId stave_id = StaveId::generate();
  lane.ensure_stave(stave_id);

  const PedalSpan span = make_pedal_span(Rational(0), Rational(3));
  ASSERT_TRUE(lane.add_pedal_span(stave_id, span).ok());

  const auto diagnostics = validate_lane_references(lane, Rational(2));
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kPedalSpanOutOfRange);
  EXPECT_EQ(diagnostics[0].entity_id, span.id);
}

// "Span repair" / cross-measure mutation: a destructive edit that removes a
// voice's underlying events (as a cross-measure move/cut might, ahead of
// any Phase 8 command-layer span repair) leaves a previously valid slur
// pointing at entity ids that no longer resolve. The referential validator
// is exactly the mechanism such a repair step would query.
TEST(NotationValidationTest, MutationThatRemovesEventsLeavesStaleSpanFlagged) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent second = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());

  const auto slur = make_slur(event_id(first), event_id(second));
  ASSERT_TRUE(voice.add_slur(slur).ok());
  ASSERT_TRUE(validate_voice_references(voice).empty());

  // Simulate the cross-measure mutation: the events are gone (e.g. cut to
  // another destination and remapped to fresh identities per the
  // clipboard's "never retains source entity UUIDs" rule), but the slur
  // referencing the old ids was not itself repaired/removed.
  voice.clear();

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code, NotationDiagnosticCode::kSlurDanglingEndpoint);
  EXPECT_EQ(diagnostics[0].entity_id, slur.id);
}

// Phase 8e-i — dynamics and grace group referential validation

TEST(NotationValidationTest, DynamicMarkingWithValidEventPasses) {
  VoiceContent     voice;
  const VoiceEvent note = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(note).ok());

  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(note), Dynamic::kMf))
          .ok());

  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(NotationValidationTest, DynamicMarkingWithDanglingEventIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent note = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(note).ok());

  const auto marking =
      make_dynamic_marking(NotationEntityId::generate(), Dynamic::kMf);
  ASSERT_TRUE(voice.add_dynamic(marking).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kDynamicDanglingReference);
  EXPECT_EQ(diagnostics[0].entity_id, marking.id);
}

TEST(NotationValidationTest, GraceGroupWithNotePrincipalPasses) {
  VoiceContent     voice;
  const VoiceEvent principal = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(principal).ok());

  ASSERT_TRUE(voice
                  .add_grace_group(make_grace_group(
                      event_id(principal),
                      {GraceNote{.pitch    = pitch(Letter::kD),
                                 .duration = eighth(),
                                 .type     = GraceNoteType::kAppoggiatura,
                                 .slashed  = false}}))
                  .ok());

  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(NotationValidationTest, GraceGroupWithChordPrincipalPasses) {
  VoiceContent voice;
  const Chord  chord =
      make_chord(quarter(), {ChordNote{.pitch = pitch(Letter::kC)},
                             ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(chord)).ok());

  ASSERT_TRUE(voice
                  .add_grace_group(make_grace_group(
                      event_id(voice.events()[0]),
                      {GraceNote{.pitch    = pitch(Letter::kD),
                                 .duration = eighth(),
                                 .type     = GraceNoteType::kAppoggiatura,
                                 .slashed  = false}}))
                  .ok());

  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(NotationValidationTest, GraceGroupWithRestPrincipalIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent rest_event = make_rest(quarter());
  ASSERT_TRUE(voice.append(rest_event).ok());

  const auto group = make_grace_group(
      event_id(rest_event), {GraceNote{.pitch    = pitch(Letter::kD),
                                       .duration = eighth(),
                                       .type     = GraceNoteType::kAppoggiatura,
                                       .slashed  = false}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kGraceGroupPrincipalNotSounding);
  EXPECT_EQ(diagnostics[0].entity_id, group.id);
}

TEST(NotationValidationTest, GraceGroupWithDanglingPrincipalIsFlagged) {
  VoiceContent     voice;
  const VoiceEvent note = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(note).ok());

  const auto group =
      make_grace_group(NotationEntityId::generate(),
                       {GraceNote{.pitch    = pitch(Letter::kD),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kGraceGroupPrincipalNotSounding);
  EXPECT_EQ(diagnostics[0].entity_id, group.id);
}
