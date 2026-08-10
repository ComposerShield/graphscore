// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <ranges>
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
using graphscore::VoiceValidationState;

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

TEST(NotationValidationTest, DuplicateArticulationOnOneEventIsFlagged) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice
          .append(make_note(pitch(Letter::kC), quarter(), false,
                            {Articulation::kAccent, Articulation::kAccent}))
          .ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kDuplicateArticulation);
  EXPECT_EQ(diagnostics[0].entity_id, event_id(voice.events()[0]));
}

TEST(NotationValidationTest, DuplicateDurationArticulationIsFlaggedTwice) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice
          .append(make_note(pitch(Letter::kC), quarter(), false,
                            {Articulation::kStaccato, Articulation::kStaccato}))
          .ok());

  const auto diagnostics = validate_voice_references(voice);
  ASSERT_EQ(diagnostics.size(), 2u);
  EXPECT_EQ(diagnostics[0].code,
            NotationDiagnosticCode::kDuplicateArticulation);
  EXPECT_EQ(diagnostics[1].code,
            NotationDiagnosticCode::kConflictingDurationArticulation);
}

TEST(NotationValidationTest, DistinctArticulationsOnOneChordAreClean) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice
          .append(make_chord(
              quarter(),
              {ChordNote{NotationEntityId::generate(), pitch(Letter::kC)},
               ChordNote{NotationEntityId::generate(), pitch(Letter::kE)}},
              {Articulation::kAccent, Articulation::kMarcato,
               Articulation::kTenuto}))
          .ok());

  EXPECT_TRUE(validate_voice_references(voice).empty());
}

// ---- Focused per-family incremental-consumer vs full equality ----

TEST(VoiceValidationStateTest, IncrementalTieDiagnosticsEqualFull) {
  VoiceContent voice;
  for (int i = 0; i < 10; ++i)
    ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), eighth())).ok());

  // Create a tie mismatch: note 4 tied to next with wrong pitch at position 5.
  auto events   = voice.events();  // copy needed before mutation
  auto tie_note = make_note(pitch(Letter::kD), eighth(), /*tied_to_next=*/true);
  ASSERT_TRUE(voice
                  .replace_event(*Rational::create(4, 8), tie_note,
                                 voice.total_length())
                  .ok());

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Change note at position 5 to G (not D) — should create pitch mismatch.
  auto mismatch = make_note(pitch(Letter::kG), eighth());
  ASSERT_TRUE(voice
                  .replace_event(*Rational::create(5, 8), mismatch,
                                 voice.total_length())
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, IncrementalTupletDiagnosticsEqualFull) {
  VoiceContent voice;
  // Two tuplet eighths — incomplete group (needs 3 for 3:2).
  auto tuplet_dur =
      *Duration::create(NoteValue::kEighth, 0, TupletRatio::create(3, 2));
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), tuplet_dur)).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kD), tuplet_dur)).ok());
  // Add trailing events so we have context.
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kE), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kF), eighth())).ok());

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Add a dynamic near the tuplet group and verify complete ordered output.
  ASSERT_TRUE(voice
                  .add_dynamic(make_dynamic_marking(event_id(voice.events()[1]),
                                                    Dynamic::kMf))
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, IncrementalDynamicDiagnosticsEqualFull) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kD), quarter())).ok());

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Add a dynamic with dangling reference.
  ASSERT_TRUE(voice
                  .add_dynamic(make_dynamic_marking(
                      NotationEntityId::generate(), Dynamic::kFf))
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, IncrementalArticulationDiagnosticsEqualFull) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Replace with conflicting articulations.
  auto conflict = make_note(pitch(Letter::kE), quarter(), false,
                            {Articulation::kStaccato, Articulation::kTenuto});
  ASSERT_TRUE(
      voice.replace_event(Rational(0), conflict, voice.total_length()).ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, IncrementalHairpinDiagnosticsEqualFull) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kD), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kE), quarter())).ok());
  const auto e1 = event_id(voice.events()[0]);
  const auto e3 = event_id(voice.events()[2]);

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Add hairpin with reversed endpoints (end before start).
  ASSERT_TRUE(
      voice.add_hairpin(make_hairpin(e3, e1, HairpinDirection::kCrescendo))
          .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, IncrementalSlurDiagnosticsEqualFull) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kD), quarter())).ok());
  const auto e1 = event_id(voice.events()[0]);

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Add slur with dangling endpoint.
  ASSERT_TRUE(voice.add_slur(make_slur(e1, NotationEntityId::generate())).ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, IncrementalGraceDiagnosticsEqualFull) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Add grace group with dangling principal.
  ASSERT_TRUE(voice
                  .add_grace_group(make_grace_group(
                      NotationEntityId::generate(),
                      {GraceNote{.pitch    = pitch(Letter::kD),
                                 .duration = eighth(),
                                 .type     = GraceNoteType::kAppoggiatura,
                                 .slashed  = false}}))
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, IncrementalBeamDiagnosticsEqualFull) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kD), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kE), eighth())).ok());
  const auto e1 = event_id(voice.events()[0]);
  const auto e2 = event_id(voice.events()[1]);

  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  // Add beam override referencing non-beamable event (quarter note).
  ASSERT_TRUE(voice
                  .add_beam_override(
                      make_beam_override(BeamOverride::Kind::kJoin, {e1, e2}))
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

// ---- Transactional journal: failed mutation leaves no journal entry ----

TEST(VoiceRevisionLineageTest, FailedAddDynamicLeavesJournalUnchanged) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch(Letter::kC), quarter())).ok());
  const auto rev0 = voice.capture_revision();

  // Attempt to add a dynamic with a duplicate ID — should fail.
  const auto dyn =
      make_dynamic_marking(event_id(voice.events()[0]), Dynamic::kMf);
  ASSERT_TRUE(voice.add_dynamic(dyn).ok());
  const auto rev1 = voice.capture_revision();
  EXPECT_NE(rev0, rev1);

  // Adding the same marking again must fail — no journal change.
  EXPECT_FALSE(voice.add_dynamic(dyn).ok());
  EXPECT_EQ(voice.capture_revision(), rev1);
}

TEST(VoiceRevisionLineageTest, FailedAddPedalLeavesJournalUnchanged) {
  TrackLane     lane;
  const StaveId stave_id = StaveId::generate();
  lane.ensure_stave(stave_id);
  const auto rev0 = lane.capture_revision();

  // Add a valid pedal span.
  ASSERT_TRUE(
      lane.add_pedal_span(stave_id, make_pedal_span(Rational(0), Rational(1)))
          .ok());

  // Adding the same span again must fail (duplicate ID).
  const auto* spans = lane.pedal_spans(stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_FALSE(lane.add_pedal_span(stave_id, (*spans)[0]).ok());

  // Journal should still only have one entry.
  const auto delta = lane.pedal_delta_since(rev0);
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(delta->size(), 1u);
}

// ---- Pedal span add/remove with unrelated staves ----

TEST(TrackLanePedalSpanTest, AddPedalRejectsDuplicateAcrossUnrelatedStaves) {
  TrackLane lane;
  // Create many unrelated staves with pedal spans.
  for (int i = 0; i < 10; ++i) {
    const StaveId sid = StaveId::generate();
    lane.ensure_stave(sid);
    ASSERT_TRUE(
        lane.add_pedal_span(sid, make_pedal_span(Rational(0), Rational(i + 1)))
            .ok());
  }
  // Add another pedal and verify global identity semantics. The implementation
  // may scan retained pedal collections; that bookkeeping is outside the
  // engraving-fragment rebuild metric.
  const StaveId target = StaveId::generate();
  lane.ensure_stave(target);
  ASSERT_TRUE(
      lane.add_pedal_span(target, make_pedal_span(Rational(0), Rational(1)))
          .ok());
  // Duplicate check must reject re-adding the same span.
  const auto* spans = lane.pedal_spans(target);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_FALSE(lane.add_pedal_span(target, (*spans)[0]).ok());
}

TEST(TrackLanePedalSpanTest, RemovePedalThenReAddPreservesOrder) {
  TrackLane     lane;
  const StaveId sid = StaveId::generate();
  lane.ensure_stave(sid);

  auto s1 = make_pedal_span(Rational(0), Rational(1));
  auto s2 = make_pedal_span(Rational(1), Rational(2));
  ASSERT_TRUE(lane.add_pedal_span(sid, s1).ok());
  ASSERT_TRUE(lane.add_pedal_span(sid, s2).ok());
  ASSERT_TRUE(lane.remove_pedal_span(sid, s1.id).ok());
  ASSERT_TRUE(lane.add_pedal_span(sid, s1).ok());

  const auto* spans = lane.pedal_spans(sid);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  // s2 was inserted after s1 originally; after remove+re-add, s2 is first, s1
  // second.
  EXPECT_EQ((*spans)[0].id, s2.id);
  EXPECT_EQ((*spans)[1].id, s1.id);
}

// ---- Complete incremental-consumer validation with trailing references ----

TEST(VoiceValidationStateTest,
     LocalEditExactlyMatchesFreshWithTrailingReferenceCollections) {
  VoiceContent voice;
  // Create many events (30+) so trailing material is clearly separated.
  for (int i = 0; i < 30; ++i) {
    ASSERT_TRUE(
        voice
            .append(make_note(pitch(Letter::kC), quarter(),
                              /*tied_to_next=*/(i == 0 || i == 2 || i == 28)))
            .ok());
  }

  // Add large trailing reference collections.
  const auto& events = voice.events();
  // Dynamics on trailing events only (indices 10–29).
  for (int i = 10; i < 30; ++i) {
    ASSERT_TRUE(
        voice
            .add_dynamic(make_dynamic_marking(
                event_id(events[static_cast<std::size_t>(i)]),
                static_cast<Dynamic>(static_cast<int>(Dynamic::kP) + (i % 8))))
            .ok());
  }
  // Hairpins across trailing events.
  for (int i = 10; i < 28; ++i) {
    ASSERT_TRUE(voice
                    .add_hairpin(make_hairpin(
                        event_id(events[static_cast<std::size_t>(i)]),
                        event_id(events[static_cast<std::size_t>(i + 2)]),
                        HairpinDirection::kCrescendo))
                    .ok());
  }
  // Slurs on trailing events.
  for (int i = 12; i < 28; i += 2) {
    ASSERT_TRUE(voice
                    .add_slur(make_slur(
                        event_id(events[static_cast<std::size_t>(i)]),
                        event_id(events[static_cast<std::size_t>(i + 1)])))
                    .ok());
  }
  // Beam overrides on trailing events (every adjacent pair from 15+).
  for (int i = 15; i < 28; i += 2) {
    ASSERT_TRUE(voice
                    .add_beam_override(make_beam_override(
                        BeamOverride::Kind::kJoin,
                        {event_id(events[static_cast<std::size_t>(i)]),
                         event_id(events[static_cast<std::size_t>(i + 1)])}))
                    .ok());
  }
  // Grace groups on trailing events.
  for (int i = 15; i < 28; ++i) {
    ASSERT_TRUE(voice
                    .add_grace_group(make_grace_group(
                        event_id(events[static_cast<std::size_t>(i)]),
                        {GraceNote{.pitch    = pitch(Letter::kD),
                                   .duration = eighth(),
                                   .type     = GraceNoteType::kAppoggiatura}}))
                    .ok());
  }

  VoiceValidationState        val_state;
  [[maybe_unused]] const auto init = val_state.rebuild(voice);
  const auto                  rev0 = voice.capture_revision();

  // Perform a local edit on an early event: add a dynamic to event 0
  // and change event 2's articulation (remove one, add another).
  ASSERT_TRUE(
      voice
          .add_dynamic(make_dynamic_marking(event_id(events[0]), Dynamic::kFff))
          .ok());
  // Add a hairpin from event 0 to event 1.
  ASSERT_TRUE(
      voice
          .add_hairpin(make_hairpin(event_id(events[0]), event_id(events[1]),
                                    HairpinDirection::kDiminuendo))
          .ok());
  // Add a slur from event 1 to event 2.
  ASSERT_TRUE(
      voice.add_slur(make_slur(event_id(events[1]), event_id(events[2]))).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = val_state.apply(voice, *d_opt);

  // visited_ids reports semantic records directly revalidated by this complete
  // pass. It is deliberately not evidence of all computational work.
  const auto trailing              = event_id(events[29]);
  const auto trailing_sentinel_e28 = event_id(events[28]);
  const auto trailing_sentinel_e27 = event_id(events[27]);
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing),
            result.visited_ids.end());
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing_sentinel_e28),
            result.visited_ids.end());
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing_sentinel_e27),
            result.visited_ids.end());

  // Trailing reference IDs from all five families are semantic records in the
  // complete revalidation.
  // Dynamics: trailing ones are at indices 10–19 (20 was just added for event
  // 0).
  const auto trailing_dyn_sentinel = voice.dynamics()[10].id;
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing_dyn_sentinel),
            result.visited_ids.end());
  // Hairpins: trailing ones start at index 0 (all were created before edit).
  // The newly added hairpin is at the back; the last pre-edit hairpin is at
  // index 17 (events 10–27 → 18 hairpins).
  const auto trailing_hp_sentinel = voice.hairpins()[0].id;
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing_hp_sentinel),
            result.visited_ids.end());
  // Slurs: trailing ones start from event 12. The newly added slur (event 1→2)
  // is at the back; the first trailing slur is at index 0.
  const auto trailing_slur_sentinel = voice.slurs()[0].id;
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing_slur_sentinel),
            result.visited_ids.end());
  // Beam overrides: trailing ones start from event 15. The last one is at
  // index 5 (6 beam overrides for events 15–27).
  const auto trailing_beam_sentinel = voice.beam_overrides()[0].id;
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing_beam_sentinel),
            result.visited_ids.end());
  // Grace groups: trailing ones start from event 15. The last one is at
  // index 12 (13 grace groups for events 15–27).
  const auto trailing_grace_sentinel = voice.grace_groups()[0].id;
  EXPECT_NE(std::ranges::find(result.visited_ids, trailing_grace_sentinel),
            result.visited_ids.end());

  // Incremental diagnostics must equal full validation.
  const auto fresh_diags = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh_diags);
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
// any Phase 8 command-layer span repair). VoiceContent::clear() now properly
// removes all events and references; a separate test exercises the scenario
// where only events are removed (via remove_event) leaving references
// dangling.
TEST(NotationValidationTest, ClearRemovesAllContentAndReferences) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent second = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());

  const auto slur = make_slur(event_id(first), event_id(second));
  ASSERT_TRUE(voice.add_slur(slur).ok());
  ASSERT_TRUE(validate_voice_references(voice).empty());

  voice.clear();

  // After clear(), there are no events and no references, so no diagnostics.
  EXPECT_TRUE(validate_voice_references(voice).empty());
  EXPECT_EQ(voice.events().size(), 0u);
  EXPECT_EQ(voice.slurs().size(), 0u);
}

TEST(NotationValidationTest, RemoveEventLeavesExistingReferencesFlagged) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent second = make_note(pitch(Letter::kD), quarter());
  const VoiceEvent third  = make_note(pitch(Letter::kE), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());
  ASSERT_TRUE(voice.append(third).ok());

  const auto slur = make_slur(event_id(first), event_id(second));
  ASSERT_TRUE(voice.add_slur(slur).ok());
  ASSERT_TRUE(validate_voice_references(voice).empty());

  // Remove the second event but leave the slur referencing it.
  ASSERT_TRUE(voice.remove_event(*Rational::create(1, 4), Rational(1)).ok());

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
