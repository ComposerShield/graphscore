// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Articulation;
using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::decompose_measure_aligned_rests;
using graphscore::decompose_rest;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::event_id;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Hairpin;
using graphscore::HairpinDirection;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_beam_override;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MeasureMap;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::RefOpKind;
using graphscore::Rest;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StemDirection;
using graphscore::TimeSignature;
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

Measure measure(std::uint8_t numerator, std::uint16_t denominator = 4) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 KeySignature{}};
}

NodeTimeline make_timeline(std::vector<Measure> measures) {
  auto timeline = NodeTimeline::create(std::move(measures), {});
  assert(timeline.has_value());
  return std::move(*timeline);
}

// Rest::operator== compares NotationEntityId, and every Rest
// decompose_rest/decompose_measure_aligned_rests produces carries a fresh
// generated id -- two independently produced fills of the same span are
// never Rest-equal even when they are the same musical shape. Comparisons
// below compare each Rest's Duration only.
std::vector<Duration> rest_durations(const std::vector<Rest>& rests) {
  std::vector<Duration> durations;
  durations.reserve(rests.size());
  for (const Rest& rest : rests)
    durations.push_back(rest.duration);
  return durations;
}

}  // namespace

TEST(VoiceContentTest, EmptyVoiceHasZeroLength) {
  const VoiceContent voice;
  EXPECT_EQ(voice.total_length(), Rational(0));
}

TEST(VoiceContentTest, AppendAccumulatesResolvedLength) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kQuarter))).ok());
  EXPECT_EQ(voice.total_length(), *Rational::create(1, 2));
}

TEST(VoiceContentTest, AppendRejectsSingleNoteChord) {
  VoiceContent voice;
  const Chord  chord = make_chord(duration(NoteValue::kQuarter),
                                  {ChordNote{.pitch = pitch(Letter::kC)}});
  EXPECT_FALSE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.total_length(), Rational(0));
}

TEST(VoiceContentTest, AppendAcceptsTwoNoteChord) {
  VoiceContent voice;
  const Chord  chord = make_chord(duration(NoteValue::kQuarter),
                                  {ChordNote{.pitch = pitch(Letter::kC)},
                                   ChordNote{.pitch = pitch(Letter::kE)}});
  EXPECT_TRUE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.total_length(), *Rational::create(1, 4));
}

TEST(VoiceContentTest, CheckCompleteAcceptsExactFill) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kWhole)))
          .ok());
  EXPECT_TRUE(voice.check_complete(Rational(1)).ok());
}

TEST(VoiceContentTest, CheckCompleteFlagsUnderfill) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kHalf)))
          .ok());
  EXPECT_FALSE(voice.check_complete(Rational(1)).ok());
}

TEST(VoiceContentTest, CheckCompleteFlagsOverfill) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kWhole)))
          .ok());
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  EXPECT_FALSE(voice.check_complete(Rational(1)).ok());
}

TEST(VoiceContentTest, NormalizeIsNoOpWhenAlreadyComplete) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kWhole)))
          .ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  EXPECT_EQ(voice.events().size(), 1u);
  EXPECT_TRUE(voice.check_complete(Rational(1)).ok());
}

TEST(VoiceContentTest, NormalizeFillsGapWithAutomaticRests) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  EXPECT_TRUE(voice.check_complete(Rational(1)).ok());

  // Every automatically appended tail event must be a Rest.
  for (std::size_t i = 1; i < voice.events().size(); ++i) {
    EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[i]));
  }
}

TEST(VoiceContentTest, NormalizeFlagsOverfillWithoutModifyingVoice) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kWhole)))
          .ok());
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  EXPECT_FALSE(voice.normalize(Rational(1)).ok());
  EXPECT_EQ(voice.events().size(), 2u);
}

TEST(VoiceContentTest, ValidateSurfacesTieDiagnostic) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice
          .append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter),
                            /*tied_to_next=*/true))
          .ok());
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kD), duration(NoteValue::kQuarter)))
          .ok());
  EXPECT_FALSE(voice.validate().ok());
}

TEST(DecomposeRestTest, RejectsZeroAndNegativeLength) {
  EXPECT_FALSE(decompose_rest(Rational(0)).has_value());
  EXPECT_FALSE(decompose_rest(Rational(-1)).has_value());
}

TEST(DecomposeRestTest, SingleWholeNoteGapIsOneRest) {
  const auto rests = decompose_rest(Rational(1));
  ASSERT_TRUE(rests.has_value());
  ASSERT_EQ(rests->size(), 1u);
  EXPECT_EQ((*rests)[0].duration.resolved(), Rational(1));
}

TEST(DecomposeRestTest, FiveEighthsDecomposesToHalfPlusEighth) {
  const auto rests = decompose_rest(*Rational::create(5, 8));
  ASSERT_TRUE(rests.has_value());
  Rational total(0);
  for (const Rest& rest : *rests)
    total = total + rest.duration.resolved();
  EXPECT_EQ(total, *Rational::create(5, 8));
}

TEST(DecomposeRestTest, SevenEighthsIsExactlyOneDoubleDottedHalf) {
  const auto rests = decompose_rest(*Rational::create(7, 8));
  ASSERT_TRUE(rests.has_value());
  ASSERT_EQ(rests->size(), 1u);
  EXPECT_EQ((*rests)[0].duration.base(), NoteValue::kHalf);
  EXPECT_EQ((*rests)[0].duration.dots(), 2);
}

TEST(DecomposeRestTest, SmallestUnitIsAnUndottedSixtyFourth) {
  const auto rests = decompose_rest(*Rational::create(1, 64));
  ASSERT_TRUE(rests.has_value());
  ASSERT_EQ(rests->size(), 1u);
  EXPECT_EQ((*rests)[0].duration.base(), NoteValue::kSixtyFourth);
  EXPECT_EQ((*rests)[0].duration.dots(), 0);
}

TEST(DecomposeRestTest, FinerThanSixtyFourthIsUnrepresentable) {
  EXPECT_FALSE(decompose_rest(*Rational::create(1, 128)).has_value());
}

TEST(DecomposeRestTest, NonDyadicDenominatorRejected) {
  // 1/3: denominator 3 does not divide kScale (256), so the early
  // denominator-gate in decompose_rest returns nullopt without reaching
  // the DP.
  const auto r = Rational::create(1, 3);
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(decompose_rest(*r).has_value());
}

TEST(DecomposeRestTest, NegativeNegativeNormalizedToNonDyadicRejected) {
  // -1/-3 canonicalizes to 1/3.  This proves Rational denominator
  // canonicalization does not defeat the overflow-safe scaling guard.
  const auto r = Rational::create(-1, -3);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->numerator(), 1);
  EXPECT_EQ(r->denominator(), 3);
  EXPECT_FALSE(decompose_rest(*r).has_value());
}

TEST(DecomposeRestTest,
     FiveHundredTwentyEighthsIsDottedSixtyFourthPlusSixtyFourth) {
  // 5/128 = 3/128 + 2/128 = dotted sixty-fourth + sixty-fourth.
  // The greedy algorithm would pick 1/32 (4/128) leaving 1/128 (dead end).
  // Bounded exact DP must find the optimal 2-rest decomposition.
  const auto rests = decompose_rest(*Rational::create(5, 128));
  ASSERT_TRUE(rests.has_value());
  ASSERT_EQ(rests->size(), 2u);
  EXPECT_EQ((*rests)[0].duration.base(), NoteValue::kSixtyFourth);
  EXPECT_EQ((*rests)[0].duration.dots(), 1);  // dotted 64th = 3/128
  EXPECT_EQ((*rests)[0].duration.resolved(), *Rational::create(3, 128));
  EXPECT_EQ((*rests)[1].duration.base(), NoteValue::kSixtyFourth);
  EXPECT_EQ((*rests)[1].duration.dots(), 0);  // plain 64th = 2/128
  EXPECT_EQ((*rests)[1].duration.resolved(), *Rational::create(1, 64));

  Rational total(0);
  for (const Rest& rest : *rests)
    total = total + rest.duration.resolved();
  EXPECT_EQ(total, *Rational::create(5, 128));
}

TEST(DecomposeRestTest, SeventeenOneHundredTwentyEighthsIsTwoRestsNonGreedy) {
  // 17/128 = 34/256.  Greedy picks 1/8 (32/256), leaves 2/256 — dead.
  // Exact DP must find 7/64 (28/256) + 3/128 (6/256) = 2 rests.
  const auto rests = decompose_rest(*Rational::create(17, 128));
  ASSERT_TRUE(rests.has_value());
  ASSERT_EQ(rests->size(), 2u);
  EXPECT_EQ((*rests)[0].duration.base(), NoteValue::kSixteenth);
  EXPECT_EQ((*rests)[0].duration.dots(), 2);  // double-dotted 16th = 7/64
  EXPECT_EQ((*rests)[0].duration.resolved(), *Rational::create(7, 64));
  EXPECT_EQ((*rests)[1].duration.base(), NoteValue::kSixtyFourth);
  EXPECT_EQ((*rests)[1].duration.dots(), 1);  // dotted 64th = 3/128
  EXPECT_EQ((*rests)[1].duration.resolved(), *Rational::create(3, 128));

  Rational total(0);
  for (const Rest& rest : *rests)
    total = total + rest.duration.resolved();
  EXPECT_EQ(total, *Rational::create(17, 128));
}

// Overflow-safety: a large valid Rational whose scaled value (×256) would
// overflow a signed 64-bit integer must be rejected without UB — the
// cap check must happen before multiplication.
TEST(DecomposeRestTest, OverflowScaleLargeRationalRejected) {
  // numerator = 2^56 + 1  →  num × 256 ≈ 1.84 × 10^19  →  overflows int64_t
  constexpr std::int64_t kLargeNum = (1LL << 56) + 1;
  const auto             r         = Rational::create(kLargeNum, 1);
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(decompose_rest(*r).has_value());
}

// Cap boundary: the largest value that can theoretically decompose
// within 64 terms (kMaxTerms × largest candidate = 64 × 448/256 = 112).
TEST(DecomposeRestTest, LargestAcceptedDpCapBoundary) {
  // 112 = 28672/256 so target = kDpCap exactly.
  const auto rests = decompose_rest(Rational(112));
  ASSERT_TRUE(rests.has_value());
  // 64 double-dotted whole notes = 64 × 448/256 = 112 exactly.
  ASSERT_EQ(rests->size(), 64u);
  for (const Rest& r : *rests) {
    EXPECT_EQ(r.duration.base(), NoteValue::kWhole);
    EXPECT_EQ(r.duration.dots(), 2);
  }
  // Verify total.
  Rational total(0);
  for (const Rest& r : *rests)
    total = total + r.duration.resolved();
  EXPECT_EQ(total, Rational(112));
}

// Cap boundary: one 256th beyond the cap must be rejected.
TEST(DecomposeRestTest, ImmediatelyOutsideDpCapRejected) {
  const auto r = Rational::create(28673, 256);
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(decompose_rest(*r).has_value());
}

// ---- decompose_measure_aligned_rests: no rest ever crosses a barline ----

// Three 3/4 measures give node_end() == 9/4. A single decompose_rest(9/4)
// call's first rest alone already resolves to more than one measure's
// worth of time (3/4) -- it necessarily spans past the first barline. The
// measure-aligned fill must instead decompose each measure independently,
// producing three separate dotted-half rests whose cumulative sums land
// exactly on every barline.
TEST(DecomposeMeasureAlignedRestsTest,
     ThreeThreeQuarterMeasuresRespectEveryBarline) {
  const NodeTimeline timeline =
      make_timeline({measure(3, 4), measure(3, 4), measure(3, 4)});
  const Rational per_measure = *Rational::create(3, 4);

  // The naive whole-span decomposition does cross a barline, confirming
  // this is a genuine worked example and not a vacuous test.
  const auto naive = decompose_rest(timeline.node_end());
  ASSERT_TRUE(naive.has_value());
  ASSERT_FALSE(naive->empty());
  EXPECT_GT((*naive)[0].duration.resolved(), per_measure);

  const auto result = decompose_measure_aligned_rests(timeline);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);

  Rational cumulative(0);
  for (const Rest& rest : *result) {
    EXPECT_EQ(rest.duration.resolved(), per_measure);
    cumulative = cumulative + rest.duration.resolved();
  }
  EXPECT_EQ(cumulative, timeline.node_end());
  EXPECT_EQ(cumulative, timeline.measures().total_length());
}

// Several meters, including a mixed set, decomposed independently: the
// observable property this test proves is that the measure-aligned result
// tiles every measure boundary exactly (each measure starts on a rest
// boundary and no single rest overruns its own measure's length) --
// verified without re-deriving decompose_rest's own per-measure output,
// which would only be a change detector against voice_content.cpp's own
// implementation rather than independent evidence.
TEST(DecomposeMeasureAlignedRestsTest,
     MixedMetersEachTileExactlyWithNoRestExceedingItsMeasure) {
  const NodeTimeline timeline =
      make_timeline({measure(4, 4), measure(3, 8), measure(5, 4)});

  const auto result = decompose_measure_aligned_rests(timeline);
  ASSERT_TRUE(result.has_value());

  const MeasureMap& measures = timeline.measures();
  Rational          cumulative(0);
  std::size_t       rest_index = 0;
  for (std::size_t m = 0; m < measures.measure_count(); ++m) {
    EXPECT_EQ(cumulative, measures.measure_start(m))
        << "measure " << m << " does not start on a rest boundary";
    const Rational target = cumulative + measures.measure_length(m);
    while (cumulative < target) {
      ASSERT_LT(rest_index, result->size())
          << "measure " << m << " ran out of rests before reaching its end";
      const Rational rest_dur = (*result)[rest_index].duration.resolved();
      ASSERT_LE(cumulative + rest_dur, target)
          << "a rest in measure " << m << " overran the barline";
      cumulative = cumulative + rest_dur;
      ++rest_index;
    }
  }
  EXPECT_EQ(cumulative, timeline.node_end());
  EXPECT_EQ(rest_index, result->size())
      << "extra rests beyond the last measure";
}

// A trailing pickdown region is decomposed as its own final group, after
// every main-region measure's rests -- never merged with the boundary
// measure's own decomposition.
TEST(DecomposeMeasureAlignedRestsTest, PickdownIsDecomposedAsItsOwnFinalGroup) {
  NodeTimeline timeline = make_timeline({measure(4, 4)});
  ASSERT_TRUE(timeline.set_pickdown(*Rational::create(1, 4)).ok());

  const auto result = decompose_measure_aligned_rests(timeline);
  ASSERT_TRUE(result.has_value());

  const auto main_piece     = decompose_rest(*Rational::create(4, 4));
  const auto pickdown_piece = decompose_rest(*Rational::create(1, 4));
  ASSERT_TRUE(main_piece.has_value());
  ASSERT_TRUE(pickdown_piece.has_value());

  std::vector<Rest> expected = *main_piece;
  expected.insert(expected.end(), pickdown_piece->begin(),
                  pickdown_piece->end());
  EXPECT_EQ(rest_durations(*result), rest_durations(expected));

  // Exact tiling: the boundary between main region and pickdown falls
  // exactly on a rest boundary, and the total exactly matches node_end().
  Rational cumulative(0);
  bool     saw_boundary = false;
  for (const Rest& rest : *result) {
    cumulative = cumulative + rest.duration.resolved();
    if (cumulative == timeline.boundary_position())
      saw_boundary = true;
  }
  EXPECT_TRUE(saw_boundary);
  EXPECT_EQ(cumulative, timeline.node_end());
}

// A pickdown whose duration is not an exact sum of base-and-dot Duration
// values (a non-dyadic-denominator Rational) cannot be decomposed by
// decompose_rest, so the whole fill fails -- never a partial fill covering
// only the main region.
TEST(DecomposeMeasureAlignedRestsTest,
     UnrepresentablePickdownFailsTheWholeFillNotJustAPiece) {
  NodeTimeline   timeline        = make_timeline({measure(4, 4)});
  const Rational unrepresentable = *Rational::create(1, 3);
  ASSERT_TRUE(timeline.set_pickdown(unrepresentable).ok());

  EXPECT_FALSE(decompose_measure_aligned_rests(timeline).has_value());
}

// -- Phase 8f-i: ChordNote/GraceNote id uniqueness in VoiceContent --

TEST(NoteheadIdUniquenessTest, AppendRejectsChordNoteIdCollisionWithEvent) {
  VoiceContent voice;
  const auto   note = make_note(pitch(Letter::kC), duration(NoteValue::kHalf));
  ASSERT_TRUE(voice.append(note).ok());
  // Create a chord whose first notehead id equals the existing note's id.
  const Chord chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{event_id(note), pitch(Letter::kE), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kG), false}},
            {},
            {}};
  EXPECT_FALSE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.events().size(), 1u);
}

TEST(NoteheadIdUniquenessTest,
     AppendRejectsDuplicateChordNoteIdWithinSameChord) {
  VoiceContent           voice;
  const NotationEntityId dup_id = NotationEntityId::generate();
  const Chord            chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{NotationEntityId::generate(), pitch(Letter::kC), false},
             ChordNote{dup_id, pitch(Letter::kE), false},
             ChordNote{dup_id, pitch(Letter::kG), false}},
            {},
            {}};
  EXPECT_FALSE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.events().size(), 0u);
}

TEST(NoteheadIdUniquenessTest, InsertEventRejectsChordNoteIdCollision) {
  VoiceContent voice;
  // Fill with rests so we have a rest boundary to insert at.
  ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kWhole))).ok());
  ASSERT_TRUE(voice.check_complete(Rational(1)).ok());
  // Insert a note at position 0 consuming quarter-note rest coverage.
  const auto note = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.insert_event(Rational(0), note, Rational(1)).ok());
  const std::size_t event_count = voice.events().size();
  // Now try to insert at the next boundary (position = 1/4) a chord
  // whose first notehead id equals the existing note's event id.
  const NotationEntityId colliding_id = event_id(voice.events()[0]);
  const Rational         insert_pos   = *Rational::create(1, 4);
  const Chord            chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kEighth),
            {ChordNote{colliding_id, pitch(Letter::kE), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kG), false}},
            {},
            {}};
  EXPECT_FALSE(
      voice.insert_event(insert_pos, VoiceEvent(chord), Rational(1)).ok());
  // Model unchanged.
  EXPECT_EQ(voice.events().size(), event_count);
}

TEST(NoteheadIdUniquenessTest, AddGraceGroupRejectsGraceNoteIdCollision) {
  VoiceContent voice;
  const auto   principal =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(principal).ok());
  // First grace group succeeds.
  ASSERT_TRUE(voice
                  .add_grace_group(make_grace_group(
                      event_id(principal),
                      {GraceNote{.pitch    = pitch(Letter::kB),
                                 .duration = duration(NoteValue::kEighth),
                                 .type     = GraceNoteType::kAppoggiatura}}))
                  .ok());
  // Second grace group with a GraceNote id that collides with the first
  // grace group's own id (not the note id).
  const NotationEntityId colliding_id = voice.grace_groups()[0].id;
  const GraceNote        bad_gn{colliding_id, pitch(Letter::kA),
                         duration(NoteValue::kEighth),
                         GraceNoteType::kAppoggiatura, false};
  const GraceGroup       bad_group =
      GraceGroup{NotationEntityId::generate(), event_id(principal), {bad_gn}};
  EXPECT_FALSE(voice.add_grace_group(bad_group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 1u);
}

TEST(NoteheadIdUniquenessTest,
     AddGraceGroupRejectsGraceNoteIdCollisionWithEvent) {
  VoiceContent voice;
  const auto   principal =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(principal).ok());
  // Construct a grace group whose first GraceNote's id equals the
  // principal event's id.
  const GraceGroup bad_group =
      GraceGroup{NotationEntityId::generate(),
                 event_id(principal),
                 {GraceNote{event_id(principal), pitch(Letter::kD),
                            duration(NoteValue::kEighth),
                            GraceNoteType::kAppoggiatura, false}}};
  EXPECT_FALSE(voice.add_grace_group(bad_group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

TEST(NoteheadIdUniquenessTest,
     ReplaceEventAllowsTargetEmbeddedIdAsReplacementTopLevelId) {
  VoiceContent voice;
  const Chord  chord = make_chord(duration(NoteValue::kQuarter),
                                  {ChordNote{.pitch = pitch(Letter::kC)},
                                   ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  const Note replacement{chord.notes[0].id,
                         pitch(Letter::kG),
                         duration(NoteValue::kQuarter),
                         false,
                         {},
                         StemDirection::kAuto};

  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());
  const auto* result = std::get_if<Note>(&voice.events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->id, chord.notes[0].id);
}

TEST(NoteheadIdUniquenessTest,
     ReplaceEventAllowsTargetTopLevelIdAsReplacementEmbeddedId) {
  VoiceContent voice;
  const Chord  original = make_chord(duration(NoteValue::kQuarter),
                                     {ChordNote{.pitch = pitch(Letter::kC)},
                                      ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  const Chord replacement =
      Chord{original.notes[0].id,
            duration(NoteValue::kQuarter),
            {ChordNote{original.id, pitch(Letter::kF), false},
             ChordNote{original.notes[1].id, pitch(Letter::kA), false}},
            {},
            {}};

  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());
  const auto* result = std::get_if<Chord>(&voice.events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->notes[0].id, original.id);
}

TEST(NoteheadIdUniquenessTest, ReplaceEventAllowsEmbeddedIdReuseFromTarget) {
  VoiceContent voice;
  const Chord  original = make_chord(duration(NoteValue::kQuarter),
                                     {ChordNote{.pitch = pitch(Letter::kC)},
                                      ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  // Build a replacement chord with the same top-level and ChordNote ids.
  Chord replacement =
      Chord{original.id,
            duration(NoteValue::kHalf),
            {ChordNote{original.notes[0].id, pitch(Letter::kC), true},
             ChordNote{original.notes[1].id, pitch(Letter::kE), false}},
            {},
            {}};
  EXPECT_TRUE(
      voice.replace_event(Rational(0), VoiceEvent(replacement), Rational(1))
          .ok());
  const auto* result = std::get_if<Chord>(&voice.events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->id, original.id);
  EXPECT_EQ(result->notes[0].id, original.notes[0].id);
  EXPECT_EQ(result->notes[1].id, original.notes[1].id);
}

TEST(NoteheadIdUniquenessTest,
     ReplaceEventRejectsEmbeddedIdCollisionOtherEvent) {
  VoiceContent voice;
  // chord1 at pos 0, chord2 at pos 1/4.
  const Chord chord1 = make_chord(duration(NoteValue::kQuarter),
                                  {ChordNote{.pitch = pitch(Letter::kC)},
                                   ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(chord1)).ok());
  const Chord chord2 = make_chord(duration(NoteValue::kQuarter),
                                  {ChordNote{.pitch = pitch(Letter::kG)},
                                   ChordNote{.pitch = pitch(Letter::kB)}});
  ASSERT_TRUE(voice.append(VoiceEvent(chord2)).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  // Replace chord1 with a chord whose first notehead id equals
  // chord2's first notehead id.  This must be rejected.
  const NotationEntityId other_id = chord2.notes[0].id;
  const Note             bad_parent{
      other_id, pitch(Letter::kF),   duration(NoteValue::kQuarter), false,
                  {},       StemDirection::kAuto};
  EXPECT_FALSE(
      voice.replace_event(Rational(0), VoiceEvent(bad_parent), Rational(1))
          .ok());
  const Chord bad_chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{other_id, pitch(Letter::kF), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kA), false}},
            {},
            {}};
  EXPECT_FALSE(
      voice.replace_event(Rational(0), VoiceEvent(bad_chord), Rational(1))
          .ok());
  EXPECT_EQ(std::get<Chord>(voice.events()[0]).notes[0].id,
            chord1.notes[0].id);  // unchanged
}

TEST(NoteheadIdUniquenessTest,
     ReplaceEventRejectsReferenceAndGraceIdentityCollisions) {
  VoiceContent voice;
  const Chord  original = make_chord(duration(NoteValue::kQuarter),
                                     {ChordNote{.pitch = pitch(Letter::kC)},
                                      ChordNote{.pitch = pitch(Letter::kE)}});
  const auto   successor =
      make_note(pitch(Letter::kG), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice.append(successor).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(original.id, Dynamic::kF)).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(original.id, event_id(successor),
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(voice.add_slur(make_slur(original.id, event_id(successor))).ok());
  ASSERT_TRUE(
      voice
          .add_beam_override(make_beam_override(
              BeamOverride::Kind::kBreak, {original.id, event_id(successor)}))
          .ok());
  ASSERT_TRUE(
      voice
          .add_grace_group(make_grace_group(
              original.id, {GraceNote{.pitch    = pitch(Letter::kB),
                                      .duration = duration(NoteValue::kEighth),
                                      .type = GraceNoteType::kAppoggiatura}}))
          .ok());

  const std::vector<NotationEntityId> colliding_ids = {
      voice.dynamics()[0].id,     voice.hairpins()[0].id,
      voice.slurs()[0].id,        voice.beam_overrides()[0].id,
      voice.grace_groups()[0].id, voice.grace_groups()[0].notes[0].id,
  };
  for (const NotationEntityId id : colliding_ids) {
    const Note bad_parent{
        id, pitch(Letter::kF),   duration(NoteValue::kQuarter), false,
        {}, StemDirection::kAuto};
    EXPECT_FALSE(
        voice.replace_event(Rational(0), bad_parent, Rational(1)).ok());

    const Chord bad_embedded =
        Chord{original.id,
              duration(NoteValue::kQuarter),
              {ChordNote{id, pitch(Letter::kC), false},
               ChordNote{original.notes[1].id, pitch(Letter::kE), false}},
              {},
              {}};
    EXPECT_FALSE(
        voice.replace_event(Rational(0), bad_embedded, Rational(1)).ok());
  }
  EXPECT_EQ(voice.events()[0], VoiceEvent(original));
}

TEST(NoteheadIdUniquenessTest,
     ReplaceEventRejectsDuplicateIdsWithinReplacementChord) {
  VoiceContent voice;
  const Chord  original = make_chord(duration(NoteValue::kQuarter),
                                     {ChordNote{.pitch = pitch(Letter::kC)},
                                      ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId duplicate_id = original.notes[0].id;

  Chord bad = make_chord(duration(NoteValue::kQuarter),
                         {ChordNote{duplicate_id, pitch(Letter::kF), false},
                          ChordNote{duplicate_id, pitch(Letter::kA), false}});
  bad.id    = original.id;
  EXPECT_FALSE(voice.replace_event(Rational(0), bad, Rational(1)).ok());
  EXPECT_EQ(voice.events()[0], VoiceEvent(original));
}

// -- Phase 8f-i review follow-up: malformed-input rejection --

TEST(NoteheadIdUniquenessTest, AppendRejectsNilChordNoteId) {
  VoiceContent voice;
  const Chord  chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{NotationEntityId::generate(), pitch(Letter::kC), false},
             ChordNote{{}, pitch(Letter::kE), false}},
            {},
            {}};
  EXPECT_FALSE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.events().size(), 0u);
}

TEST(NoteheadIdUniquenessTest, AppendRejectsChordNoteIdEqualToChordId) {
  VoiceContent voice;
  const auto   chord_id = NotationEntityId::generate();
  const Chord  chord =
      Chord{chord_id,
            duration(NoteValue::kQuarter),
            {ChordNote{NotationEntityId::generate(), pitch(Letter::kC), false},
             ChordNote{chord_id, pitch(Letter::kE), false}},
            {},
            {}};
  EXPECT_FALSE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.events().size(), 0u);
}

TEST(NoteheadIdUniquenessTest, AppendRejectsNilParentId) {
  VoiceContent voice;
  const Chord  chord =
      Chord{NotationEntityId{},
            duration(NoteValue::kQuarter),
            {ChordNote{NotationEntityId::generate(), pitch(Letter::kC), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kE), false}},
            {},
            {}};
  EXPECT_FALSE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.events().size(), 0u);
}

TEST(NoteheadIdUniquenessTest, InsertEventRejectsNilChordNoteId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kWhole))).ok());
  ASSERT_TRUE(voice.check_complete(Rational(1)).ok());
  const Chord chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{{}, pitch(Letter::kC), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kE), false}},
            {},
            {}};
  EXPECT_FALSE(
      voice.insert_event(Rational(0), VoiceEvent(chord), Rational(1)).ok());
  EXPECT_EQ(voice.events().size(), 1u);  // unchanged
}

TEST(NoteheadIdUniquenessTest, ReplaceEventRejectsNilChordNoteId) {
  VoiceContent voice;
  const Chord  original = make_chord(duration(NoteValue::kQuarter),
                                     {ChordNote{.pitch = pitch(Letter::kC)},
                                      ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  const Chord bad =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{{}, pitch(Letter::kF), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kG), false}},
            {},
            {}};
  EXPECT_FALSE(
      voice.replace_event(Rational(0), VoiceEvent(bad), Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice.events()[0]));
}

TEST(NoteheadIdUniquenessTest, ReplaceEventRejectsParentIdEqualToEmbeddedId) {
  VoiceContent voice;
  const Chord  original = make_chord(duration(NoteValue::kQuarter),
                                     {ChordNote{.pitch = pitch(Letter::kC)},
                                      ChordNote{.pitch = pitch(Letter::kE)}});
  ASSERT_TRUE(voice.append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());
  const auto  parent_id = NotationEntityId::generate();
  const Chord bad =
      Chord{parent_id,
            duration(NoteValue::kQuarter),
            {ChordNote{NotationEntityId::generate(), pitch(Letter::kF), false},
             ChordNote{parent_id, pitch(Letter::kG), false}},
            {},
            {}};
  EXPECT_FALSE(
      voice.replace_event(Rational(0), VoiceEvent(bad), Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice.events()[0]));
}

TEST(GraceNoteIdUniquenessTest, AddGraceGroupRejectsNilGraceNoteId) {
  VoiceContent voice;
  const auto   principal =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(principal).ok());
  const GraceGroup group = GraceGroup{NotationEntityId::generate(),
                                      event_id(principal),
                                      {GraceNote{{},
                                                 pitch(Letter::kD),
                                                 duration(NoteValue::kEighth),
                                                 GraceNoteType::kAppoggiatura,
                                                 false}}};
  EXPECT_FALSE(voice.add_grace_group(group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

TEST(GraceNoteIdUniquenessTest, AddGraceGroupRejectsGraceNoteIdEqualToGroupId) {
  VoiceContent voice;
  const auto   principal =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(principal).ok());
  const auto       group_id = NotationEntityId::generate();
  const GraceGroup group    = GraceGroup{
      group_id,
      event_id(principal),
         {GraceNote{group_id, pitch(Letter::kD), duration(NoteValue::kEighth),
                 GraceNoteType::kAppoggiatura, false}}};
  EXPECT_FALSE(voice.add_grace_group(group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

TEST(GraceNoteIdUniquenessTest, AddGraceGroupRejectsNilGroupId) {
  VoiceContent voice;
  const auto   principal =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(principal).ok());
  const GraceGroup group =
      GraceGroup{NotationEntityId{},
                 event_id(principal),
                 {GraceNote{NotationEntityId::generate(), pitch(Letter::kD),
                            duration(NoteValue::kEighth),
                            GraceNoteType::kAppoggiatura, false}}};
  EXPECT_FALSE(voice.add_grace_group(group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

// -- replace_event ID remapping across event-reference families --

TEST(ReplaceEventRemapTest, RemapsAllFiveEventReferenceFamilies) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  const Note successor =
      make_note(pitch(Letter::kG), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.append(successor).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_top_id = event_id(original);
  const NotationEntityId succ_id    = event_id(successor);

  // Attach one of each reference family to the original note.
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(old_top_id, Dynamic::kF)).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(old_top_id, succ_id,
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(voice.add_slur(make_slur(old_top_id, succ_id)).ok());
  ASSERT_TRUE(voice
                  .add_beam_override(make_beam_override(
                      BeamOverride::Kind::kJoin, {old_top_id, succ_id}))
                  .ok());
  ASSERT_TRUE(
      voice
          .add_grace_group(make_grace_group(
              old_top_id, {GraceNote{.pitch    = pitch(Letter::kB),
                                     .duration = duration(NoteValue::kEighth),
                                     .type = GraceNoteType::kAppoggiatura}}))
          .ok());

  // Replace the original Note with a Chord whose top-level ID differs.
  const Chord chord =
      make_chord(duration(NoteValue::kEighth),
                 {{old_top_id, pitch(Letter::kC), false},
                  {NotationEntityId::generate(), pitch(Letter::kE), false}});
  const auto new_top_id = event_id(chord);
  EXPECT_NE(new_top_id, old_top_id);
  ASSERT_TRUE(voice.replace_event(Rational(0), chord, Rational(1)).ok());

  // All five families remapped old_top_id -> new_top_id.
  ASSERT_EQ(voice.dynamics().size(), 1u);
  EXPECT_EQ(voice.dynamics()[0].at_event, new_top_id);
  ASSERT_EQ(voice.hairpins().size(), 1u);
  EXPECT_EQ(voice.hairpins()[0].start_event, new_top_id);
  EXPECT_EQ(voice.hairpins()[0].end_event, succ_id);
  ASSERT_EQ(voice.slurs().size(), 1u);
  EXPECT_EQ(voice.slurs()[0].start_event, new_top_id);
  EXPECT_EQ(voice.slurs()[0].end_event, succ_id);
  ASSERT_EQ(voice.beam_overrides().size(), 1u);
  ASSERT_EQ(voice.beam_overrides()[0].events.size(), 2u);
  EXPECT_EQ(voice.beam_overrides()[0].events[0], new_top_id);
  EXPECT_EQ(voice.beam_overrides()[0].events[1], succ_id);
  ASSERT_EQ(voice.grace_groups().size(), 1u);
  EXPECT_EQ(voice.grace_groups()[0].principal_event, new_top_id);

  // Referential validation passes (references point to the top-level event).
  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(ReplaceEventRemapTest, NoRemapWhenTopLevelIdUnchanged) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId top_id = event_id(original);
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(top_id, Dynamic::kP)).ok());

  // Replace with a Note carrying the same top-level id (same pitch, different
  // duration).
  Note replacement     = original;
  replacement.duration = duration(NoteValue::kHalf);
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // Dynamic still points to the same id.
  ASSERT_EQ(voice.dynamics().size(), 1u);
  EXPECT_EQ(voice.dynamics()[0].at_event, top_id);
}

TEST(ReplaceEventRemapTest, RemapsBeamOverrideAllEvents) {
  VoiceContent voice;
  const Note   n1 = make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  const Note   n2 = make_note(pitch(Letter::kD), duration(NoteValue::kEighth));
  const Note   n3 = make_note(pitch(Letter::kE), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(n1).ok());
  ASSERT_TRUE(voice.append(n2).ok());
  ASSERT_TRUE(voice.append(n3).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_id = event_id(n2);
  ASSERT_TRUE(
      voice
          .add_beam_override(make_beam_override(
              BeamOverride::Kind::kJoin, {event_id(n1), old_id, event_id(n3)}))
          .ok());

  // Replace the middle Note (n2) with a Chord having a different top-level id.
  const Chord chord =
      make_chord(duration(NoteValue::kEighth),
                 {{old_id, pitch(Letter::kD), false},
                  {NotationEntityId::generate(), pitch(Letter::kF), false}});
  const auto new_top_id = event_id(chord);
  EXPECT_NE(new_top_id, old_id);
  ASSERT_TRUE(
      voice.replace_event(*Rational::create(1, 8), chord, Rational(1)).ok());

  ASSERT_EQ(voice.beam_overrides().size(), 1u);
  ASSERT_EQ(voice.beam_overrides()[0].events.size(), 3u);
  EXPECT_EQ(voice.beam_overrides()[0].events[1], new_top_id);
  EXPECT_NE(voice.beam_overrides()[0].events[1], old_id);
}

TEST(ReplaceEventRemapTest, RemapsBothEndpointsOfHairpinAndSlur) {
  VoiceContent voice;
  const Note left = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  const Note right =
      make_note(pitch(Letter::kG), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(left).ok());
  ASSERT_TRUE(voice.append(right).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId left_id  = event_id(left);
  const NotationEntityId right_id = event_id(right);

  // Hairpin and slur span both notes.
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(left_id, right_id,
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(voice.add_slur(make_slur(left_id, right_id)).ok());

  // Replace left note with a Chord (new top-level id).
  const Chord chord_left =
      make_chord(duration(NoteValue::kQuarter),
                 {{left_id, pitch(Letter::kC), false},
                  {NotationEntityId::generate(), pitch(Letter::kE), false}});
  const auto new_left_id = chord_left.id;
  EXPECT_NE(new_left_id, left_id);
  ASSERT_TRUE(voice.replace_event(Rational(0), chord_left, Rational(1)).ok());

  // Replace right note with a Chord (new top-level id).
  const Chord chord_right =
      make_chord(duration(NoteValue::kQuarter),
                 {{right_id, pitch(Letter::kG), false},
                  {NotationEntityId::generate(), pitch(Letter::kB), false}});
  const auto new_right_id = chord_right.id;
  EXPECT_NE(new_right_id, right_id);
  ASSERT_TRUE(
      voice.replace_event(*Rational::create(1, 4), chord_right, Rational(1))
          .ok());

  // Both endpoints remapped for hairpin.
  ASSERT_EQ(voice.hairpins().size(), 1u);
  EXPECT_EQ(voice.hairpins()[0].start_event, new_left_id);
  EXPECT_EQ(voice.hairpins()[0].end_event, new_right_id);
  // Both endpoints remapped for slur.
  ASSERT_EQ(voice.slurs().size(), 1u);
  EXPECT_EQ(voice.slurs()[0].start_event, new_left_id);
  EXPECT_EQ(voice.slurs()[0].end_event, new_right_id);

  // Referential validation passes.
  EXPECT_TRUE(validate_voice_references(voice).empty());
}

TEST(ReplaceEventRemapTest, FailedReplacementLeavesReferencesIntact) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  const Note successor =
      make_note(pitch(Letter::kG), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.append(successor).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_id = event_id(original);
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(old_id, Dynamic::kF)).ok());

  // Attempt to replace with a chord whose notehead id collides with the
  // successor's id — must fail atomically.
  const NotationEntityId succ_event_id = event_id(successor);
  const Chord            bad_chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{succ_event_id, pitch(Letter::kE), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kG), false}},
            {},
            {}};
  EXPECT_FALSE(
      voice.replace_event(Rational(0), VoiceEvent(bad_chord), Rational(1))
          .ok());

  // References and events are unchanged.
  ASSERT_EQ(voice.dynamics().size(), 1u);
  EXPECT_EQ(voice.dynamics()[0].at_event, old_id);
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  EXPECT_EQ(event_id(voice.events()[0]), old_id);
}

// -- replace_event delta signaling for reference remapping --

TEST(ReplaceEventDeltaTest, SameDurationIdChangeEmitsFullReset) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  const Note successor =
      make_note(pitch(Letter::kG), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.append(successor).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_id  = event_id(original);
  const NotationEntityId succ_id = event_id(successor);

  // Attach one of each reference family to the original note.
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(old_id, Dynamic::kF)).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(old_id, succ_id,
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(voice.add_slur(make_slur(old_id, succ_id)).ok());
  ASSERT_TRUE(voice
                  .add_beam_override(make_beam_override(
                      BeamOverride::Kind::kJoin, {old_id, succ_id}))
                  .ok());
  ASSERT_TRUE(
      voice
          .add_grace_group(make_grace_group(
              old_id, {GraceNote{.pitch    = pitch(Letter::kB),
                                 .duration = duration(NoteValue::kEighth),
                                 .type     = GraceNoteType::kAppoggiatura}}))
          .ok());

  const auto rev0 = voice.capture_revision();

  // Same-duration replacement with different top-level ID.
  const Chord chord =
      make_chord(duration(NoteValue::kQuarter),
                 {{old_id, pitch(Letter::kC), false},
                  {NotationEntityId::generate(), pitch(Letter::kE), false}});
  const auto new_top_id = event_id(chord);
  EXPECT_NE(new_top_id, old_id);
  ASSERT_TRUE(voice.replace_event(Rational(0), chord, Rational(1)).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  EXPECT_TRUE(delta.full_reset);
  EXPECT_FALSE(delta.event_reorder);
}

TEST(ReplaceEventDeltaTest, ContractionIdChangeEmitsFullReset) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_id = event_id(original);
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(old_id, Dynamic::kP)).ok());

  const auto rev0 = voice.capture_revision();

  // Contraction: replace quarter note with eighth note (new top-level ID).
  const Note shorter =
      make_note(pitch(Letter::kD), duration(NoteValue::kEighth));
  EXPECT_NE(event_id(shorter), old_id);
  ASSERT_TRUE(voice.replace_event(Rational(0), shorter, Rational(1)).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  EXPECT_TRUE(delta.full_reset);
}

TEST(ReplaceEventDeltaTest, ExpansionIdChangeEmitsFullReset) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kEighth))).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_id = event_id(original);
  ASSERT_TRUE(
      voice.add_slur(make_slur(old_id, event_id(voice.events()[1]))).ok());

  const auto rev0 = voice.capture_revision();

  // Expansion: replace eighth note with quarter note, consuming the rest.
  const Note longer =
      make_note(pitch(Letter::kD), duration(NoteValue::kQuarter));
  EXPECT_NE(event_id(longer), old_id);
  ASSERT_TRUE(voice.replace_event(Rational(0), longer, Rational(1)).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  EXPECT_TRUE(delta.full_reset);
}

TEST(ReplaceEventDeltaTest, SameIdReplacementDoesNotEmitFullReset) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId top_id = event_id(original);
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(top_id, Dynamic::kP)).ok());

  const auto rev0 = voice.capture_revision();

  // Same-ID replacement: just change duration.
  Note replacement     = original;
  replacement.duration = duration(NoteValue::kHalf);
  EXPECT_EQ(event_id(replacement), top_id);
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  EXPECT_FALSE(delta.full_reset);
}

TEST(ReplaceEventDeltaTest, MergedDeltaPreservesFullReset) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_id = event_id(original);
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(old_id, Dynamic::kF)).ok());

  const auto rev0 = voice.capture_revision();

  // First mutation: add a slur (normal delta, no full_reset).
  ASSERT_TRUE(
      voice.add_slur(make_slur(old_id, NotationEntityId::generate())).ok());

  // Second mutation: replace with different top-level ID (triggers full_reset).
  const Chord chord =
      make_chord(duration(NoteValue::kQuarter),
                 {{old_id, pitch(Letter::kC), false},
                  {NotationEntityId::generate(), pitch(Letter::kE), false}});
  ASSERT_TRUE(voice.replace_event(Rational(0), chord, Rational(1)).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  // The merged delta must carry full_reset because the replace_event delta
  // had it.
  EXPECT_TRUE(delta.full_reset);
}

TEST(ReplaceEventDeltaTest, FailedReplacementPreservesRevisionAtomicity) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  const Note successor =
      make_note(pitch(Letter::kG), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.append(successor).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId old_id = event_id(original);
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(old_id, Dynamic::kF)).ok());

  const auto rev0 = voice.capture_revision();

  // Attempt to replace with a chord whose notehead id collides with the
  // successor's id — must fail atomically.
  const NotationEntityId succ_event_id = event_id(successor);
  const Chord            bad_chord =
      Chord{NotationEntityId::generate(),
            duration(NoteValue::kQuarter),
            {ChordNote{succ_event_id, pitch(Letter::kE), false},
             ChordNote{NotationEntityId::generate(), pitch(Letter::kG), false}},
            {},
            {}};
  EXPECT_FALSE(
      voice.replace_event(Rational(0), VoiceEvent(bad_chord), Rational(1))
          .ok());

  // Revision did not advance.
  EXPECT_EQ(voice.capture_revision(), rev0);
  // References are unchanged.
  ASSERT_EQ(voice.dynamics().size(), 1u);
  EXPECT_EQ(voice.dynamics()[0].at_event, old_id);
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
}

TEST(ReplaceEventDeltaTest, SameDurationNoRemapNoFullResetWhenIdUnchanged) {
  VoiceContent voice;
  const Note   original =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(original).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId top_id = event_id(original);
  ASSERT_TRUE(
      voice
          .add_hairpin(make_hairpin(top_id, NotationEntityId::generate(),
                                    HairpinDirection::kCrescendo))
          .ok());

  const auto rev0 = voice.capture_revision();

  // Same-ID, same-duration replacement (just change pitch).
  Note replacement  = original;
  replacement.pitch = pitch(Letter::kG);
  EXPECT_EQ(event_id(replacement), top_id);
  EXPECT_EQ(replacement.duration.resolved(), original.duration.resolved());
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  const auto d_opt = voice.delta_since(rev0);
  ASSERT_TRUE(d_opt.has_value());
  const auto& delta = *d_opt;
  EXPECT_FALSE(delta.full_reset);
  EXPECT_FALSE(delta.event_reorder);
}

// -- replace_event normalized rest gaps and non-overlap coverage --

// Helper: verify that a voice's events are non-overlapping and exactly tile
// the target duration.
void expect_exact_tile(const VoiceContent& voice, Rational target) {
  Rational cumulative(0);
  for (std::size_t i = 0; i < voice.events().size(); ++i) {
    const VoiceEvent& ev  = voice.events()[i];
    const Rational    dur = event_duration(ev).resolved();
    EXPECT_GT(dur, Rational(0))
        << "Event " << i << " has non-positive duration";
    // Verify non-overlap: cumulative position is this event's onset.
    const auto found_pos = voice.position_of_event(event_id(ev));
    ASSERT_TRUE(found_pos.has_value()) << "Event " << i << " not found";
    EXPECT_EQ(*found_pos, cumulative) << "Event " << i << " onset mismatch";
    cumulative = cumulative + dur;
  }
  EXPECT_EQ(cumulative, target) << "Voice does not exactly tile target";
  EXPECT_EQ(voice.total_length(), target);
}

// Helper: verify that the rest events after index `after_idx` (inclusive)
// exactly match the duration and shape of `decompose_rest(gap)`.
void expect_rest_gap(const VoiceContent& voice, std::size_t after_idx,
                     Rational gap) {
  const auto expected = decompose_rest(gap);
  ASSERT_TRUE(expected.has_value()) << "decompose_rest failed for gap";
  for (std::size_t j = 0; j < expected->size(); ++j) {
    const std::size_t idx = after_idx + j;
    ASSERT_LT(idx, voice.events().size()) << "Missing rest at index " << idx;
    ASSERT_TRUE(std::holds_alternative<Rest>(voice.events()[idx]))
        << "Event at " << idx << " expected Rest";
    const Rest& actual = std::get<Rest>(voice.events()[idx]);
    EXPECT_EQ(actual.duration.base(), (*expected)[j].duration.base())
        << "Rest " << j << " base mismatch";
    EXPECT_EQ(actual.duration.dots(), (*expected)[j].duration.dots())
        << "Rest " << j << " dots mismatch";
    EXPECT_EQ(actual.duration.resolved(), (*expected)[j].duration.resolved())
        << "Rest " << j << " duration mismatch";
  }
}

TEST(ReplaceEventNormalizationTest,
     ContractionAtStartGeneratesDecomposedRests) {
  VoiceContent voice;
  const Note   note = make_note(pitch(Letter::kC), duration(NoteValue::kWhole));
  ASSERT_TRUE(voice.append(note).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const Rational gap = *Rational::create(3, 4);
  const Note     replacement =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // The gap after the quarter note must match decompose_rest(3/4).
  expect_rest_gap(voice, 1, gap);
  expect_exact_tile(voice, Rational(1));
}

TEST(ReplaceEventNormalizationTest,
     ContractionInMiddlePreservesLaterSoundingOnset) {
  VoiceContent voice;
  // [note(1/4), note(1/4), rest(1/2)]
  const Note n1 = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(n1).ok());
  const Note n2 = make_note(pitch(Letter::kE), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(n2).ok());
  ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kHalf))).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId n2_id    = event_id(n2);
  const Rational         n2_onset = *Rational::create(1, 4);

  // Replace n1 (1/4) with eighth note (1/8).
  const Note replacement =
      make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // n2 must still be at onset 1/4.
  const auto pos = voice.position_of_event(n2_id);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, n2_onset);

  // The gap between replacement and n2 is 1/8, filled with a single rest.
  // The replacement event is at index 0; gap rests start at index 1.
  const auto gap_rests = decompose_rest(*Rational::create(1, 8));
  ASSERT_TRUE(gap_rests.has_value());
  ASSERT_EQ(gap_rests->size(), 1u);

  expect_rest_gap(voice, 1, *Rational::create(1, 8));
  expect_exact_tile(voice, Rational(1));
}

TEST(ReplaceEventNormalizationTest, ContractionAtEndPreservesEarlierEvents) {
  VoiceContent voice;
  // [rest(1/2), note(1/2)]
  const Rest first_rest = make_rest(duration(NoteValue::kHalf));
  ASSERT_TRUE(voice.append(first_rest).ok());
  const Note last_note =
      make_note(pitch(Letter::kG), duration(NoteValue::kHalf));
  ASSERT_TRUE(voice.append(last_note).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const Rational         half          = *Rational::create(1, 2);
  const Rational         eighth        = *Rational::create(1, 8);
  const Rational         gap           = *Rational::create(3, 8);
  const NotationEntityId first_rest_id = event_id(first_rest);

  // Replace the last note (1/2) with eighth note (1/8).
  const Note replacement =
      make_note(pitch(Letter::kG), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.replace_event(half, replacement, Rational(1)).ok());

  // The first rest is unchanged at onset 0.
  EXPECT_EQ(voice.position_of_event(first_rest_id).value_or(Rational(-1)),
            Rational(0));
  ASSERT_TRUE(std::holds_alternative<Rest>(voice.events()[0]));
  EXPECT_EQ(std::get<Rest>(voice.events()[0]).duration.resolved(), half);

  // The replacement is at onset 1/2.
  ASSERT_TRUE(std::holds_alternative<Note>(voice.events()[1]));
  EXPECT_EQ(event_duration(voice.events()[1]).resolved(), eighth);

  // The gap after it matches decompose_rest(3/8).
  expect_rest_gap(voice, 2, gap);
  expect_exact_tile(voice, Rational(1));
}

TEST(ReplaceEventNormalizationTest,
     ContractionWithDottedGapPreservesLaterEvent) {
  VoiceContent voice;
  // [note(7/8), rest(1/8)]
  const Note note =
      make_note(pitch(Letter::kC), *Duration::create(NoteValue::kHalf, 2));
  ASSERT_TRUE(voice.append(note).ok());
  const Rest tail = make_rest(duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(tail).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId tail_id = event_id(tail);

  // Replace double-dotted half (7/8) with quarter (1/4 = 2/8).  Gap = 5/8.
  const Note replacement =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // The tail rest must still be at onset 7/8.
  const auto tail_pos = voice.position_of_event(tail_id);
  ASSERT_TRUE(tail_pos.has_value());
  EXPECT_EQ(*tail_pos, *Rational::create(7, 8));

  // The gap fill (5/8) should match decompose_rest.
  expect_rest_gap(voice, 1, *Rational::create(5, 8));
  expect_exact_tile(voice, Rational(1));
}

TEST(ReplaceEventNormalizationTest, ExpansionConsumesFollowRestSuccessfully) {
  VoiceContent voice;
  // [note(1/4), rest(1/4), rest(1/2)]
  const Note note = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(note).ok());
  ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kQuarter))).ok());
  ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kHalf))).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  // Expand from 1/4 to 1/2 (consume the first rest).
  const Note replacement =
      make_note(pitch(Letter::kC), duration(NoteValue::kHalf));
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // Events: [note(1/2), rest(1/2)] — the consumed rest is gone.
  ASSERT_GE(voice.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  EXPECT_EQ(event_duration(voice.events()[0]).resolved(),
            *Rational::create(1, 2));
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[1]));
  EXPECT_EQ(event_duration(voice.events()[1]).resolved(),
            *Rational::create(1, 2));
  expect_exact_tile(voice, Rational(1));
}

TEST(ReplaceEventNormalizationTest,
     ExpansionConsumesMultipleRestsSuccessfully) {
  VoiceContent voice;
  // [note(1/8), rest(1/8), rest(1/8), rest(1/8), rest(1/8)] + normalize to 1.
  const Note note = make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(note).ok());
  for (int i = 0; i < 4; ++i)
    ASSERT_TRUE(voice.append(make_rest(duration(NoteValue::kEighth))).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const std::size_t orig_count = voice.events().size();

  // Expand from 1/8 to 3/8 (consume two 1/8 rests).
  const Note replacement = make_note(
      pitch(Letter::kC),
      *Duration::create(NoteValue::kQuarter, 1));  // dotted quarter = 3/8
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // Two rests consumed → 2 fewer events than original.
  EXPECT_EQ(voice.events().size(), orig_count - 2);
  EXPECT_EQ(event_duration(voice.events()[0]).resolved(),
            *Rational::create(3, 8));
  expect_exact_tile(voice, Rational(1));
}

TEST(ReplaceEventNormalizationTest,
     ExpansionConsumesPartialRestPreservingIdentity) {
  VoiceContent voice;
  // [note(1/8), rest(1/2), rest(3/8)]
  const Note note = make_note(pitch(Letter::kC), duration(NoteValue::kEighth));
  ASSERT_TRUE(voice.append(note).ok());
  const Rest rest_to_split = make_rest(duration(NoteValue::kHalf));
  ASSERT_TRUE(voice.append(rest_to_split).ok());
  ASSERT_TRUE(voice.append(make_rest(*Duration::create(NoteValue::kQuarter, 1)))
                  .ok());  // dotted quarter = 3/8
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const NotationEntityId split_id = event_id(rest_to_split);

  // Expand from 1/8 to 1/4 (need 1/8 more; consume partial of the 1/2 rest).
  const Note replacement =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // Events after replacement: [note(1/4), suffix_of_split, rest(3/8)]
  // The split rest's original ID should survive on the remainder.
  bool found_split_id = false;
  for (const auto& ev : voice.events()) {
    if (event_id(ev) == split_id) {
      found_split_id = true;
      ASSERT_TRUE(std::holds_alternative<Rest>(ev));
      // Original split was 1/2; consumed 1/8 → remainder is 3/8.
      EXPECT_EQ(std::get<Rest>(ev).duration.resolved(),
                *Rational::create(3, 8));
    }
  }
  EXPECT_TRUE(found_split_id) << "Split rest ID was not preserved";

  expect_exact_tile(voice, Rational(1));
}

TEST(ReplaceEventNormalizationTest,
     ExpansionInsufficientFollowRestRejectedAtomically) {
  VoiceContent voice;
  // [note(1/4), note(1/4)]
  const Note n1 = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(n1).ok());
  const Note n2 = make_note(pitch(Letter::kE), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(n2).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const VoiceContent pre = voice;

  // Try to expand n1 from 1/4 to 1/2 — needs 1/4 of rest but next event is
  // a sounding note.
  const Note longer = make_note(pitch(Letter::kC), duration(NoteValue::kHalf));
  EXPECT_FALSE(voice.replace_event(Rational(0), longer, Rational(1)).ok());

  // Voice unchanged.
  EXPECT_EQ(voice, pre);
}

TEST(ReplaceEventNormalizationTest,
     ExpansionPastTargetLengthRejectedAtomically) {
  VoiceContent voice;
  // Single note filling the whole target.
  const Note note = make_note(pitch(Letter::kC), duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(note).ok());
  ASSERT_TRUE(voice.normalize(*Rational::create(1, 4)).ok());

  const VoiceContent pre = voice;

  // Try to expand to whole note when target_length = 1/4.
  const Note longer = make_note(pitch(Letter::kC), duration(NoteValue::kWhole));
  EXPECT_FALSE(
      voice.replace_event(Rational(0), longer, *Rational::create(1, 4)).ok());
  EXPECT_EQ(voice, pre);
}

TEST(ReplaceEventNormalizationTest,
     ContractionFiveOneTwentyEighthsGapDecomposesExactly) {
  // dotted sixteenth (3/32 = 12/128) → double-dotted thirty-second
  // (7/128) leaves a 5/128 gap.  The old greedy picked 1/32 (4/128)
  // leaving 1/128 (dead end).  Exact DP produces:
  //   dotted sixty-fourth (3/128) + sixty-fourth (2/128).
  VoiceContent voice;
  const Note   note =
      make_note(pitch(Letter::kC), *Duration::create(NoteValue::kSixteenth, 1));
  ASSERT_TRUE(voice.append(note).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  const Note replacement = make_note(
      pitch(Letter::kC), *Duration::create(NoteValue::kThirtySecond, 2));
  ASSERT_TRUE(voice.replace_event(Rational(0), replacement, Rational(1)).ok());

  // Gap must match exact expected rest shapes.
  expect_rest_gap(voice, 1, *Rational::create(5, 128));
  expect_exact_tile(voice, Rational(1));

  // Replacement onset preserved.
  ASSERT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  EXPECT_EQ(event_duration(voice.events()[0]).resolved(),
            *Rational::create(7, 128));
}

TEST(ReplaceEventNormalizationTest, ContractionNonOverlapAndExactTileProperty) {
  // Deterministic property-style coverage over generated valid voices and
  // replacement operations.  Each case undergoes either a contraction or
  // expansion of note events; verify non-overlap, exact tile, and atomicity
  // on failure.
  struct Case {
    std::string                                  description;
    std::vector<std::pair<VoiceEvent, Rational>> initial;
    Rational                                     target;
    Rational                                     replace_pos;
    Duration                                     new_dur;
    bool                                         expect_success;
  };

  const Rational w = Rational(1);              // whole
  const Rational h = *Rational::create(1, 2);  // half
  const Rational q = *Rational::create(1, 4);  // quarter
  const Rational e = *Rational::create(1, 8);  // eighth

  const std::vector<Case> cases = {
      // Contractions: replace with shorter duration.
      {"whole→quarter at start",
       {{make_note(pitch(Letter::kC), *Duration::create(NoteValue::kWhole, 0)),
         Rational(0)}},
       w,
       Rational(0),
       duration(NoteValue::kQuarter),
       true},
      {"whole→half at start",
       {{make_note(pitch(Letter::kC), *Duration::create(NoteValue::kWhole, 0)),
         Rational(0)}},
       w,
       Rational(0),
       duration(NoteValue::kHalf),
       true},
      {"half→eighth at start",
       {{make_note(pitch(Letter::kC), *Duration::create(NoteValue::kHalf, 0)),
         Rational(0)},
        {make_rest(*Duration::create(NoteValue::kHalf, 0)), h}},
       w,
       Rational(0),
       duration(NoteValue::kEighth),
       true},
      // Contraction at middle: rest→note→rest, contract the middle note.
      {"half→quarter in middle",
       {{make_rest(*Duration::create(NoteValue::kQuarter, 0)), Rational(0)},
        {make_note(pitch(Letter::kE), *Duration::create(NoteValue::kHalf, 0)),
         q},
        {make_rest(*Duration::create(NoteValue::kQuarter, 0)), q + h}},
       w,
       q,
       duration(NoteValue::kQuarter),
       true},
      // Contraction at end.
      {"half→eighth at end",
       {{make_rest(*Duration::create(NoteValue::kHalf, 0)), Rational(0)},
        {make_note(pitch(Letter::kG), *Duration::create(NoteValue::kHalf, 0)),
         h}},
       w,
       h,
       duration(NoteValue::kEighth),
       true},
      // Expansion consuming rests (only rests follow).
      {"eighth→quarter consuming rest",
       {{make_note(pitch(Letter::kC), *Duration::create(NoteValue::kEighth, 0)),
         Rational(0)},
        {make_rest(*Duration::create(NoteValue::kEighth, 0)), e},
        {make_rest(*Duration::create(NoteValue::kEighth, 0)), e + e},
        {make_rest(*Duration::create(NoteValue::kEighth, 0)), e + e + e}},
       h,
       Rational(0),
       duration(NoteValue::kQuarter),
       true},
      // Expansion blocked by sounding event.
      {"quarter→half blocked by note",
       {{make_note(pitch(Letter::kC),
                   *Duration::create(NoteValue::kQuarter, 0)),
         Rational(0)},
        {make_note(pitch(Letter::kE),
                   *Duration::create(NoteValue::kQuarter, 0)),
         q}},
       h,
       Rational(0),
       duration(NoteValue::kHalf),
       false},
      // Expansion exceeds target_length.
      {"eighth→whole exceeds target",
       {{make_note(pitch(Letter::kC), *Duration::create(NoteValue::kEighth, 0)),
         Rational(0)}},
       q,
       Rational(0),
       duration(NoteValue::kWhole),
       false},
  };

  for (const auto& c : cases) {
    VoiceContent voice;
    for (const auto& [ev, pos] : c.initial) {
      // We need events at specific positions.  Build by appending.
      ASSERT_TRUE(voice.append(ev).ok()) << c.description << ": append failed";
      (void)pos;  // positions confirmed by ordering
    }

    // Normalize to target.
    const Rational initial_len = voice.total_length();
    if (initial_len <= c.target) {
      ASSERT_TRUE(voice.normalize(c.target).ok())
          << c.description << ": normalize failed";
    }

    const VoiceContent pre = voice;

    // Build replacement: a Note of the armed duration.  If the event at
    // replace_pos is already a Note, preserve its pitch.
    const auto idx_opt = voice.find_event_index_at(c.replace_pos);
    ASSERT_TRUE(idx_opt.has_value())
        << c.description << ": replace_pos not found";
    const VoiceEvent&  target_ev = voice.events()[*idx_opt];
    const SpelledPitch repl_pitch =
        std::holds_alternative<Note>(target_ev)
            ? std::get<Note>(target_ev).pitch
            : (std::holds_alternative<Chord>(target_ev)
                   ? std::get<Chord>(target_ev).notes[0].pitch
                   : pitch(Letter::kA));
    const VoiceEvent replacement = make_note(repl_pitch, c.new_dur);

    const bool ok =
        voice.replace_event(c.replace_pos, replacement, c.target).ok();
    EXPECT_EQ(ok, c.expect_success)
        << c.description << ": success expectation mismatch";

    if (ok) {
      expect_exact_tile(voice, c.target);
    } else {
      // Atomicity: voice unchanged.
      EXPECT_EQ(voice, pre) << c.description << ": mutated on failure";
    }
  }
}

// =========================================================================
// set_notehead_pitch (narrow pitch-only mutation, M5-phase-20)
// =========================================================================

TEST(VoiceContentTest, SetNoteheadPitchNotePreservesEverythingElse) {
  VoiceContent voice;
  const Note   note =
      make_note(pitch(Letter::kC), duration(NoteValue::kQuarter), true,
                {Articulation::kAccent}, StemDirection::kUp);
  ASSERT_TRUE(voice.append(note).ok());
  const Rational before_length = voice.total_length();

  ASSERT_TRUE(voice.set_notehead_pitch(note.id, pitch(Letter::kD)).ok());
  const Note& moved = std::get<Note>(voice.events().front());
  EXPECT_EQ(moved.id, note.id);
  EXPECT_EQ(moved.pitch, pitch(Letter::kD));
  EXPECT_TRUE(moved.tied_to_next);
  EXPECT_EQ(moved.articulations, note.articulations);
  EXPECT_EQ(moved.stem, note.stem);
  EXPECT_EQ(moved.duration.resolved(), note.duration.resolved());
  EXPECT_EQ(voice.total_length(), before_length);
}

TEST(VoiceContentTest, SetNoteheadPitchChordNotePreservesChordAndPeers) {
  VoiceContent    voice;
  const ChordNote tied{NotationEntityId::generate(), pitch(Letter::kC), true};
  const ChordNote peer{NotationEntityId::generate(), pitch(Letter::kE), false};
  const Chord     chord =
      make_chord(duration(NoteValue::kQuarter), {tied, peer},
                 {Articulation::kStaccato}, StemDirection::kDown);
  ASSERT_TRUE(voice.append(chord).ok());
  const Rational before_length = voice.total_length();

  ASSERT_TRUE(voice.set_notehead_pitch(tied.id, pitch(Letter::kD)).ok());
  const Chord& moved = std::get<Chord>(voice.events().front());
  EXPECT_EQ(moved.id, chord.id);
  ASSERT_EQ(moved.notes.size(), 2u);
  EXPECT_EQ(moved.notes[0].id, tied.id);
  EXPECT_EQ(moved.notes[0].pitch, pitch(Letter::kD));
  EXPECT_TRUE(moved.notes[0].tied_to_next);
  // The peer notehead is untouched: identity, pitch, and tie state.
  EXPECT_EQ(moved.notes[1].id, peer.id);
  EXPECT_EQ(moved.notes[1].pitch, pitch(Letter::kE));
  EXPECT_FALSE(moved.notes[1].tied_to_next);
  // Chord-level fields are untouched.
  EXPECT_EQ(moved.articulations, chord.articulations);
  EXPECT_EQ(moved.stem, chord.stem);
  EXPECT_EQ(moved.duration.resolved(), chord.duration.resolved());
  EXPECT_EQ(voice.total_length(), before_length);
}

TEST(VoiceContentTest, SetNoteheadPitchGracePreservesDurationTypeSlash) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const NotationEntityId principal = event_id(voice.events().front());
  const GraceGroup       group     = make_grace_group(
      principal, {GraceNote{NotationEntityId::generate(), pitch(Letter::kD),
                            duration(NoteValue::kEighth),
                            GraceNoteType::kAcciaccatura, true}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  ASSERT_TRUE(
      voice.set_notehead_pitch(group.notes[0].id, pitch(Letter::kE)).ok());
  ASSERT_EQ(voice.grace_groups().size(), 1u);
  const GraceGroup& moved = voice.grace_groups().front();
  EXPECT_EQ(moved.id, group.id);
  EXPECT_EQ(moved.principal_event, principal);
  ASSERT_EQ(moved.notes.size(), 1u);
  EXPECT_EQ(moved.notes[0].id, group.notes[0].id);
  EXPECT_EQ(moved.notes[0].pitch, pitch(Letter::kE));
  EXPECT_EQ(moved.notes[0].duration.resolved(),
            group.notes[0].duration.resolved());
  EXPECT_EQ(moved.notes[0].type, group.notes[0].type);
  EXPECT_TRUE(moved.notes[0].slashed);
}

TEST(VoiceContentTest, SetNoteheadPitchRejectsRestId) {
  VoiceContent voice;
  const Rest   rest = make_rest(duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(rest).ok());
  const VoiceContent before = voice;

  EXPECT_FALSE(voice.set_notehead_pitch(rest.id, pitch(Letter::kD)).ok());
  EXPECT_EQ(voice, before);
}

TEST(VoiceContentTest, SetNoteheadPitchRejectsChordTopLevelId) {
  VoiceContent voice;
  const Chord  chord = make_chord(
      duration(NoteValue::kQuarter),
      {ChordNote{NotationEntityId::generate(), pitch(Letter::kC), false},
        ChordNote{NotationEntityId::generate(), pitch(Letter::kE), false}});
  ASSERT_TRUE(voice.append(chord).ok());
  const VoiceContent before = voice;

  // The chord's own top-level id names the chord column, not a notehead.
  EXPECT_FALSE(voice.set_notehead_pitch(chord.id, pitch(Letter::kD)).ok());
  EXPECT_EQ(voice, before);
}

TEST(VoiceContentTest, SetNoteheadPitchRejectsUnknownId) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const VoiceContent before = voice;

  EXPECT_FALSE(
      voice.set_notehead_pitch(NotationEntityId::generate(), pitch(Letter::kD))
          .ok());
  EXPECT_EQ(voice, before);
}

TEST(VoiceContentTest, SetNoteheadPitchPreservesOrderAndRhythm) {
  VoiceContent voice;
  ASSERT_TRUE(
      voice.append(make_note(pitch(Letter::kC), duration(NoteValue::kQuarter)))
          .ok());
  const Rest second = make_rest(duration(NoteValue::kQuarter));
  ASSERT_TRUE(voice.append(second).ok());
  const Rational    before_length = voice.total_length();
  const std::size_t before_count  = voice.events().size();

  ASSERT_TRUE(voice
                  .set_notehead_pitch(event_id(voice.events().front()),
                                      pitch(Letter::kD))
                  .ok());
  EXPECT_EQ(voice.events().size(), before_count);
  EXPECT_EQ(voice.total_length(), before_length);
  // Order preserved: the note is still first, the rest still second.
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events().front()));
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events().back()));
  EXPECT_EQ(std::get<Rest>(voice.events().back()).id, second.id);
}
