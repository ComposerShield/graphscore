// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::event_id;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Hairpin;
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
using graphscore::NotationEntityId;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::RefOpKind;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaveId;
using graphscore::TrackLane;
using graphscore::validate_voice_references;
using graphscore::VoiceContent;
using graphscore::VoiceDelta;
using graphscore::VoiceEvent;
using graphscore::VoiceRevision;
using graphscore::VoiceValidationState;

namespace {

SpelledPitch pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

Duration duration(NoteValue base, std::uint8_t dots = 0) {
  return *Duration::create(base, dots);
}

}  // namespace

// -- VoiceRevision lineage, copy/move semantics, stale fallback --

TEST(VoiceRevisionLineageTest, CopyAssignmentInvalidatesPriorTokens) {
  VoiceContent a;
  ASSERT_TRUE(
      a.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const auto   token_before = a.capture_revision();
  VoiceContent b;
  b = a;
  EXPECT_FALSE(b.delta_since(token_before).has_value());
}

TEST(VoiceRevisionLineageTest, MoveAssignmentInvalidatesPriorTokens) {
  VoiceContent a;
  ASSERT_TRUE(
      a.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const auto   token_before = a.capture_revision();
  VoiceContent b;
  b = std::move(a);
  EXPECT_FALSE(b.delta_since(token_before).has_value());
}

TEST(VoiceRevisionLineageTest, ClearAdvancesRevision) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const auto rev_before = voice.capture_revision();
  voice.clear();
  const auto rev_after = voice.capture_revision();
  EXPECT_NE(rev_before, rev_after);
  // delta_since(rev_before) should show the full_reset.
  const auto delta = voice.delta_since(rev_before);
  ASSERT_TRUE(delta.has_value());
  EXPECT_TRUE(delta->full_reset);
}

TEST(VoiceRevisionLineageTest, FailedMutationDoesNotAdvanceRevision) {
  VoiceContent voice;
  const auto   rev0      = voice.capture_revision();
  const Chord  bad_chord = make_chord(duration(NoteValue::kQuarter),
                                      {ChordNote{.pitch = pitch(Letter::kC)}});
  EXPECT_FALSE(voice.append(VoiceEvent(bad_chord)).ok());
  EXPECT_EQ(voice.capture_revision(), rev0);
}

TEST(VoiceRevisionLineageTest, SemanticEqualityExcludesBookkeeping) {
  VoiceContent a;
  ASSERT_TRUE(
      a.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  VoiceContent b = a;  // copy: content identical, different revision lineage
  EXPECT_TRUE(a == b);
  // Mutate a only via a valid dynamic marking.
  ASSERT_TRUE(
      a.add_dynamic(make_dynamic_marking(event_id(a.events()[0]), Dynamic::kMf))
          .ok());
  EXPECT_FALSE(a == b);
}

TEST(VoiceRevisionLineageTest, StaleRingFallbackReturnsNullopt) {
  VoiceContent voice;
  const auto   rev_start = voice.capture_revision();
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(
        voice
            .append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
            .ok());
  }
  EXPECT_FALSE(voice.delta_since(rev_start).has_value());
}

TEST(VoiceRevisionLineageTest, SourceTokenAfterMoveIsStaleOnDest) {
  VoiceContent a;
  ASSERT_TRUE(
      a.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const auto   src_token = a.capture_revision();
  VoiceContent b;
  b = std::move(a);
  EXPECT_FALSE(b.delta_since(src_token).has_value());
}

TEST(VoiceRevisionLineageTest, MoveConstructionInvalidatesSourceTokens) {
  VoiceContent source;
  ASSERT_TRUE(
      source.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const auto source_token = source.capture_revision();

  VoiceContent destination(std::move(source));

  EXPECT_EQ(destination.events().size(), 1U);
  EXPECT_FALSE(destination.delta_since(source_token).has_value());
  // Accepted move-from source category: exercise the documented valid
  // moved-from token invalidation and reuse contract.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
  EXPECT_FALSE(source.delta_since(source_token).has_value());
  const auto moved_from_token = source.capture_revision();
  ASSERT_TRUE(
      source.append(make_note(pitch(Letter::kD), duration(NoteValue::kQuarter)))
          .ok());
  EXPECT_TRUE(source.delta_since(moved_from_token).has_value());
}

TEST(VoiceRevisionLineageTest, MoveAssignmentInvalidatesSourceTokens) {
  VoiceContent source;
  ASSERT_TRUE(
      source.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const auto   source_token = source.capture_revision();
  VoiceContent destination;
  const auto   destination_token = destination.capture_revision();

  destination = std::move(source);

  EXPECT_EQ(destination.events().size(), 1U);
  EXPECT_FALSE(destination.delta_since(source_token).has_value());
  EXPECT_FALSE(destination.delta_since(destination_token).has_value());
  // Accepted move-from source category: exercise the documented valid
  // moved-from token invalidation and reuse contract.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
  EXPECT_FALSE(source.delta_since(source_token).has_value());
  const auto moved_from_token = source.capture_revision();
  ASSERT_TRUE(
      source.append(make_note(pitch(Letter::kD), duration(NoteValue::kQuarter)))
          .ok());
  EXPECT_TRUE(source.delta_since(moved_from_token).has_value());
}

TEST(TrackLaneRevisionLineageTest, MoveConstructionInvalidatesSourceTokens) {
  TrackLane     source;
  const StaveId original_stave = StaveId::generate();
  source.ensure_stave(original_stave);
  ASSERT_TRUE(source
                  .add_pedal_span(original_stave,
                                  make_pedal_span(Rational(0), Rational(1)))
                  .ok());
  const auto source_token = source.capture_revision();

  TrackLane destination(std::move(source));

  ASSERT_NE(destination.pedal_spans(original_stave), nullptr);
  EXPECT_FALSE(destination.pedal_delta_since(source_token).has_value());
  // Accepted move-from source category: exercise the documented valid
  // moved-from token invalidation and reuse contract.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
  EXPECT_FALSE(source.pedal_delta_since(source_token).has_value());
  const auto    moved_from_token = source.capture_revision();
  const StaveId new_stave        = StaveId::generate();
  source.ensure_stave(new_stave);
  ASSERT_TRUE(
      source
          .add_pedal_span(new_stave, make_pedal_span(Rational(0), Rational(1)))
          .ok());
  EXPECT_TRUE(source.pedal_delta_since(moved_from_token).has_value());
}

TEST(TrackLaneRevisionLineageTest, MoveAssignmentInvalidatesSourceTokens) {
  TrackLane     source;
  const StaveId original_stave = StaveId::generate();
  source.ensure_stave(original_stave);
  ASSERT_TRUE(source
                  .add_pedal_span(original_stave,
                                  make_pedal_span(Rational(0), Rational(1)))
                  .ok());
  const auto source_token = source.capture_revision();
  TrackLane  destination;
  const auto destination_token = destination.capture_revision();

  destination = std::move(source);

  ASSERT_NE(destination.pedal_spans(original_stave), nullptr);
  EXPECT_FALSE(destination.pedal_delta_since(source_token).has_value());
  EXPECT_FALSE(destination.pedal_delta_since(destination_token).has_value());
  // Accepted move-from source category: exercise the documented valid
  // moved-from token invalidation and reuse contract.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
  EXPECT_FALSE(source.pedal_delta_since(source_token).has_value());
  const auto    moved_from_token = source.capture_revision();
  const StaveId new_stave        = StaveId::generate();
  source.ensure_stave(new_stave);
  ASSERT_TRUE(
      source
          .add_pedal_span(new_stave, make_pedal_span(Rational(0), Rational(1)))
          .ok());
  EXPECT_TRUE(source.pedal_delta_since(moved_from_token).has_value());
}

TEST(VoiceRevisionLineageTest, CopyConstructHasFreshLineage) {
  VoiceContent a;
  ASSERT_TRUE(
      a.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const auto   src_token = a.capture_revision();
  VoiceContent b(a);
  // b is a copy — tokens from a must not work on b.
  EXPECT_FALSE(b.delta_since(src_token).has_value());
}

// -- Operation-complete deltas and multi-mutation aggregation --

TEST(VoiceDeltaTest, AddRemoveDynamicCorrectOps) {
  VoiceContent voice;
  const auto evt = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(evt).ok());
  const auto d1 = make_dynamic_marking(event_id(evt), Dynamic::kMf);
  const auto d2 =
      make_dynamic_marking(NotationEntityId::generate(), Dynamic::kF);
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.add_dynamic(d1).ok());
  ASSERT_TRUE(voice.add_dynamic(d2).ok());
  ASSERT_TRUE(voice.remove_dynamic(d1.id).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  ASSERT_EQ(delta.dynamic_ops.size(), 3u);
  EXPECT_EQ(delta.dynamic_ops[0].kind, RefOpKind::kAdd);
  EXPECT_EQ(delta.dynamic_ops[0].id, d1.id);
  EXPECT_EQ(delta.dynamic_ops[1].kind, RefOpKind::kAdd);
  EXPECT_EQ(delta.dynamic_ops[1].id, d2.id);
  EXPECT_EQ(delta.dynamic_ops[2].kind, RefOpKind::kRemove);
  EXPECT_EQ(delta.dynamic_ops[2].id, d1.id);
}

TEST(VoiceDeltaTest, RemoveAndReAddSameId) {
  VoiceContent voice;
  const auto evt = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(evt).ok());
  const auto dyn  = make_dynamic_marking(event_id(evt), Dynamic::kMf);
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.add_dynamic(dyn).ok());
  ASSERT_TRUE(voice.remove_dynamic(dyn.id).ok());
  ASSERT_TRUE(voice.add_dynamic(dyn).ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  EXPECT_EQ(d_opt->dynamic_ops.size(), 3u);
}

TEST(VoiceDeltaTest, MultipleFamiliesAggregated) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  const auto   e2 = make_note(pitch(Letter::kD), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(e1).ok());
  ASSERT_TRUE(voice.append(e2).ok());
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(e1), Dynamic::kMf)).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(e1), event_id(e2),
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(voice.add_slur(make_slur(event_id(e1), event_id(e2))).ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  EXPECT_EQ(delta.dynamic_ops.size(), 1u);
  EXPECT_EQ(delta.hairpin_ops.size(), 1u);
  EXPECT_EQ(delta.slur_ops.size(), 1u);
  EXPECT_TRUE(delta.beam_override_ops.empty());
  EXPECT_TRUE(delta.grace_group_ops.empty());
}

// -- Incremental-consumer diagnostic equivalence --

TEST(VoiceValidationStateTest, IncrementalEqualsFreshForLocalEdit) {
  VoiceContent voice;
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(
        voice
            .append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
            .ok());
  }
  VoiceValidationState state;
  const auto           init_diags = state.rebuild(voice);
  (void)init_diags;
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice
                  .add_slur(make_slur(event_id(voice.events()[0]),
                                      event_id(voice.events()[1])))
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result = state.apply(voice, *d_opt);
  const auto fresh  = validate_voice_references(voice);
  EXPECT_EQ(result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, VisitedIdsReportsSemanticRecordsRevalidated) {
  VoiceContent voice;
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(
        voice
            .append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
            .ok());
  }
  VoiceValidationState val_state;
  const auto           diags = val_state.rebuild(voice);
  (void)diags;
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice
                  .add_dynamic(make_dynamic_marking(event_id(voice.events()[0]),
                                                    Dynamic::kF))
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto result     = val_state.apply(voice, *d_opt);
  const auto event19_id = event_id(voice.events()[19]);
  bool       found      = std::ranges::find(result.visited_ids, event19_id) !=
               result.visited_ids.end();
  EXPECT_TRUE(found);
}

// -- Per-family remove-one-add-two tests --

TEST(VoiceDeltaFamilyTest, RemoveOneAddTwoDynamics) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(e1).ok());
  const auto dyn1 = make_dynamic_marking(event_id(e1), Dynamic::kP);
  ASSERT_TRUE(voice.add_dynamic(dyn1).ok());
  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.remove_dynamic(dyn1.id).ok());
  const auto dyn2 = make_dynamic_marking(event_id(e1), Dynamic::kMf);
  const auto dyn3 = make_dynamic_marking(event_id(e1), Dynamic::kF);
  ASSERT_TRUE(voice.add_dynamic(dyn2).ok());
  ASSERT_TRUE(voice.add_dynamic(dyn3).ok());
  const auto& dyns = voice.dynamics();
  EXPECT_FALSE(std::ranges::any_of(
      dyns, [&](const DynamicMarking& d) { return d.id == dyn1.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      dyns, [&](const DynamicMarking& d) { return d.id == dyn2.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      dyns, [&](const DynamicMarking& d) { return d.id == dyn3.id; }));
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  EXPECT_EQ(d_opt->dynamic_ops.size(), 3u);
  EXPECT_EQ(state.apply(voice, *d_opt).diagnostics,
            validate_voice_references(voice));
}

TEST(VoiceDeltaFamilyTest, RemoveOneAddTwoHairpins) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  const auto   e2 = make_note(pitch(Letter::kD), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(e1).ok());
  ASSERT_TRUE(voice.append(e2).ok());
  const auto h1 =
      make_hairpin(event_id(e1), event_id(e2), HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice.add_hairpin(h1).ok());
  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.remove_hairpin(h1.id).ok());
  const auto h2 =
      make_hairpin(event_id(e1), event_id(e2), HairpinDirection::kDiminuendo);
  const auto h3 =
      make_hairpin(event_id(e1), event_id(e2), HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice.add_hairpin(h2).ok());
  ASSERT_TRUE(voice.add_hairpin(h3).ok());
  EXPECT_FALSE(std::ranges::any_of(
      voice.hairpins(), [&](const Hairpin& h) { return h.id == h1.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      voice.hairpins(), [&](const Hairpin& h) { return h.id == h2.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      voice.hairpins(), [&](const Hairpin& h) { return h.id == h3.id; }));
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  EXPECT_EQ(d_opt->hairpin_ops.size(), 3u);
  EXPECT_EQ(state.apply(voice, *d_opt).diagnostics,
            validate_voice_references(voice));
}

TEST(VoiceDeltaFamilyTest, RemoveOneAddTwoSlurs) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  const auto   e2 = make_note(pitch(Letter::kD), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(e1).ok());
  ASSERT_TRUE(voice.append(e2).ok());
  const auto s1 = make_slur(event_id(e1), event_id(e2));
  ASSERT_TRUE(voice.add_slur(s1).ok());
  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.remove_slur(s1.id).ok());
  const auto s2 = make_slur(event_id(e1), event_id(e2));
  const auto s3 = make_slur(event_id(e1), event_id(e2));
  ASSERT_TRUE(voice.add_slur(s2).ok());
  ASSERT_TRUE(voice.add_slur(s3).ok());
  EXPECT_FALSE(std::ranges::any_of(
      voice.slurs(), [&](const Slur& s) { return s.id == s1.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      voice.slurs(), [&](const Slur& s) { return s.id == s2.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      voice.slurs(), [&](const Slur& s) { return s.id == s3.id; }));
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  EXPECT_EQ(d_opt->slur_ops.size(), 3u);
  EXPECT_EQ(state.apply(voice, *d_opt).diagnostics,
            validate_voice_references(voice));
}

TEST(VoiceDeltaFamilyTest, RemoveOneAddTwoBeamOverrides) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  const auto   e2 = make_note(pitch(Letter::kD), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(e1).ok());
  ASSERT_TRUE(voice.append(e2).ok());
  const auto b1 = make_beam_override(BeamOverride::Kind::kJoin,
                                     {event_id(e1), event_id(e2)});
  ASSERT_TRUE(voice.add_beam_override(b1).ok());
  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.remove_beam_override(b1.id).ok());
  const auto b2 = make_beam_override(BeamOverride::Kind::kBreak,
                                     {event_id(e1), event_id(e2)});
  const auto b3 = make_beam_override(BeamOverride::Kind::kJoin,
                                     {event_id(e1), event_id(e2)});
  ASSERT_TRUE(voice.add_beam_override(b2).ok());
  ASSERT_TRUE(voice.add_beam_override(b3).ok());
  EXPECT_FALSE(std::ranges::any_of(
      voice.beam_overrides(),
      [&](const BeamOverride& b) { return b.id == b1.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      voice.beam_overrides(),
      [&](const BeamOverride& b) { return b.id == b2.id; }));
  EXPECT_TRUE(std::ranges::any_of(
      voice.beam_overrides(),
      [&](const BeamOverride& b) { return b.id == b3.id; }));
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  EXPECT_EQ(d_opt->beam_override_ops.size(), 3u);
  EXPECT_EQ(state.apply(voice, *d_opt).diagnostics,
            validate_voice_references(voice));
}

TEST(VoiceDeltaFamilyTest,
     ReplaceBeamOverrideEmitsSingleUpdatePreservingPosition) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  const auto   e2 = make_note(pitch(Letter::kD), duration(NoteValue::kEighth));
  const auto   e3 = make_note(pitch(Letter::kE), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(e1).ok());
  ASSERT_TRUE(voice.append(e2).ok());
  ASSERT_TRUE(voice.append(e3).ok());
  const auto first  = make_beam_override(BeamOverride::Kind::kJoin,
                                         {event_id(e1), event_id(e2)});
  const auto second = make_beam_override(BeamOverride::Kind::kBreak,
                                         {event_id(e2), event_id(e3)});
  ASSERT_TRUE(voice.add_beam_override(first).ok());
  ASSERT_TRUE(voice.add_beam_override(second).ok());
  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();

  const BeamOverride replacement{
      first.id, BeamOverride::Kind::kBreak, {event_id(e1), event_id(e2)}};
  ASSERT_TRUE(voice.replace_beam_override(first.id, replacement).ok());

  // Position and identity are preserved: replacement stays at index 0, the
  // peer at index 1.
  ASSERT_EQ(voice.beam_overrides().size(), 2u);
  EXPECT_EQ(voice.beam_overrides()[0], replacement);
  EXPECT_EQ(voice.beam_overrides()[1], second);

  // Exactly one kUpdate op carrying the full replacement record.
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  ASSERT_EQ(d_opt->beam_override_ops.size(), 1u);
  EXPECT_EQ(d_opt->beam_override_ops[0].kind, RefOpKind::kUpdate);
  EXPECT_EQ(d_opt->beam_override_ops[0].id, first.id);
  EXPECT_EQ(d_opt->beam_override_ops[0].record, replacement);
  EXPECT_EQ(state.apply(voice, *d_opt).diagnostics,
            validate_voice_references(voice));
}

TEST(VoiceDeltaFamilyTest, RemoveOneAddTwoGraceGroups) {
  VoiceContent voice;
  const auto   principal =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(principal).ok());
  const auto g1 = make_grace_group(
      event_id(principal), {GraceNote{.pitch    = pitch(Letter::kD),
                                      .duration = duration(NoteValue::kEighth),
                                      .type = GraceNoteType::kAppoggiatura}});
  ASSERT_TRUE(voice.add_grace_group(g1).ok());
  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.remove_grace_group(g1.id).ok());
  const auto g2 = make_grace_group(
      event_id(principal), {GraceNote{.pitch    = pitch(Letter::kE),
                                      .duration = duration(NoteValue::kEighth),
                                      .type = GraceNoteType::kAppoggiatura}});
  const auto g3 = make_grace_group(
      event_id(principal), {GraceNote{.pitch    = pitch(Letter::kF),
                                      .duration = duration(NoteValue::kEighth),
                                      .type = GraceNoteType::kAcciaccatura}});
  ASSERT_TRUE(voice.add_grace_group(g2).ok());
  ASSERT_TRUE(voice.add_grace_group(g3).ok());
  EXPECT_FALSE(
      std::ranges::any_of(voice.grace_groups(),
                          [&](const GraceGroup& g) { return g.id == g1.id; }));
  EXPECT_TRUE(
      std::ranges::any_of(voice.grace_groups(),
                          [&](const GraceGroup& g) { return g.id == g2.id; }));
  EXPECT_TRUE(
      std::ranges::any_of(voice.grace_groups(),
                          [&](const GraceGroup& g) { return g.id == g3.id; }));
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  EXPECT_EQ(d_opt->grace_group_ops.size(), 3u);
  EXPECT_EQ(state.apply(voice, *d_opt).diagnostics,
            validate_voice_references(voice));
}

TEST(VoiceValidationStateTest, RemoveAndReAddAllReferenceFamiliesEqualsFresh) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  const auto   e2 = make_note(pitch(Letter::kD), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(e1).ok());
  ASSERT_TRUE(voice.append(e2).ok());
  const auto dynamic = make_dynamic_marking(event_id(e1), Dynamic::kMf);
  const auto hairpin =
      make_hairpin(event_id(e1), event_id(e2), HairpinDirection::kCrescendo);
  const auto slur  = make_slur(event_id(e1), event_id(e2));
  const auto beam  = make_beam_override(BeamOverride::Kind::kJoin,
                                        {event_id(e1), event_id(e2)});
  const auto grace = make_grace_group(
      event_id(e1), {GraceNote{.pitch    = pitch(Letter::kE),
                               .duration = duration(NoteValue::kEighth),
                               .type     = GraceNoteType::kAppoggiatura}});
  ASSERT_TRUE(voice.add_dynamic(dynamic).ok());
  ASSERT_TRUE(voice.add_hairpin(hairpin).ok());
  ASSERT_TRUE(voice.add_slur(slur).ok());
  ASSERT_TRUE(voice.add_beam_override(beam).ok());
  ASSERT_TRUE(voice.add_grace_group(grace).ok());
  VoiceValidationState state;
  (void)state.rebuild(voice);
  const auto revision = voice.capture_revision();

  ASSERT_TRUE(voice.remove_dynamic(dynamic.id).ok());
  ASSERT_TRUE(voice.remove_hairpin(hairpin.id).ok());
  ASSERT_TRUE(voice.remove_slur(slur.id).ok());
  ASSERT_TRUE(voice.remove_beam_override(beam.id).ok());
  ASSERT_TRUE(voice.remove_grace_group(grace.id).ok());
  ASSERT_TRUE(voice.add_dynamic(dynamic).ok());
  ASSERT_TRUE(voice.add_hairpin(hairpin).ok());
  ASSERT_TRUE(voice.add_slur(slur).ok());
  ASSERT_TRUE(voice.add_beam_override(beam).ok());
  ASSERT_TRUE(voice.add_grace_group(grace).ok());

  const auto delta = voice.delta_since(revision);
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(state.apply(voice, *delta).diagnostics,
            validate_voice_references(voice));
}

// -- Full vs incremental-consumer diagnostics equality --

TEST(VoiceValidationStateTest, FullVsIncrementalDiagnosticsEqual) {
  VoiceContent voice;
  const auto   e1 = make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  const auto   e2 = make_note(pitch(Letter::kD), duration(NoteValue::kEighth));
  const auto   e3 = make_note(pitch(Letter::kE), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(e1).ok());
  ASSERT_TRUE(voice.append(e2).ok());
  ASSERT_TRUE(voice.append(e3).ok());
  VoiceValidationState val_state;
  const auto           init = val_state.rebuild(voice);
  (void)init;
  const auto rev0 = voice.capture_revision();
  ASSERT_TRUE(voice.add_slur(make_slur(event_id(e1), event_id(e3))).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(e1), event_id(e2),
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(e1), Dynamic::kMf)).ok());
  ASSERT_TRUE(voice
                  .add_beam_override(make_beam_override(
                      BeamOverride::Kind::kJoin, {event_id(e1), event_id(e2)}))
                  .ok());
  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto inc_result = val_state.apply(voice, *d_opt);
  const auto fresh      = validate_voice_references(voice);
  EXPECT_EQ(inc_result.diagnostics, fresh);
}

TEST(VoiceValidationStateTest, StaleCursorTriggersFullRebuild) {
  VoiceContent voice;
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(
        voice
            .append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
            .ok());
  }
  VoiceValidationState val_state;
  const auto           init = val_state.rebuild(voice);
  (void)init;
  const std::size_t event_count = voice.events().size();
  VoiceDelta        full_reset;
  full_reset.full_reset = true;
  const auto result     = val_state.apply(voice, full_reset);
  EXPECT_GE(result.visited_ids.size(), event_count);
}
