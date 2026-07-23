// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::apply_emphasis;
using graphscore::Articulation;
using graphscore::Dynamic;
using graphscore::grace_steal_durations;
using graphscore::grace_steal_remaining_duration;
using graphscore::GraceNoteType;
using graphscore::interpolate_hairpin_velocity;
using graphscore::kAccentVelocityBoost;
using graphscore::kAcciaccaturaPerNoteStealFraction;
using graphscore::kAppoggiaturaPerNoteStealFraction;
using graphscore::kDefaultSoundedDurationRatio;
using graphscore::kGraceFallbackNoteDuration;
using graphscore::kMarcatoVelocityBoost;
using graphscore::kMaxGraceNotesPerGroup;
using graphscore::kMaxGraceStealFraction;
using graphscore::kStaccatissimoSoundedDurationRatio;
using graphscore::kStaccatoSoundedDurationRatio;
using graphscore::kTenutoSoundedDurationRatio;
using graphscore::legato_sounded_duration;
using graphscore::MidiVelocity;
using graphscore::Rational;
using graphscore::sounded_duration_for_articulation;
using graphscore::velocity_for_dynamic;

namespace {

MidiVelocity velocity(std::uint8_t value) {
  return *MidiVelocity::create(value);
}

}  // namespace

// -- velocity_for_dynamic ----------------------------------------------------

TEST(PlaybackMappingTest,
     DynamicVelocityTableIsStrictlyMonotonicallyIncreasing) {
  const std::vector<Dynamic> levels = {
      Dynamic::kPpp, Dynamic::kPp, Dynamic::kP,  Dynamic::kMp,
      Dynamic::kMf,  Dynamic::kF,  Dynamic::kFf, Dynamic::kFff,
  };
  for (std::size_t i = 1; i < levels.size(); ++i) {
    EXPECT_LT(velocity_for_dynamic(levels[i - 1]).value(),
              velocity_for_dynamic(levels[i]).value())
        << "index " << i;
  }
}

TEST(PlaybackMappingTest, DynamicVelocitySpansLegalRangeWithoutTouchingZero) {
  EXPECT_GT(velocity_for_dynamic(Dynamic::kPpp).value(), 0u);
  EXPECT_EQ(velocity_for_dynamic(Dynamic::kFff).value(), MidiVelocity::kMax);
}

// -- interpolate_hairpin_velocity --------------------------------------------

TEST(PlaybackMappingTest, HairpinInterpolationAtStartReturnsFrom) {
  const MidiVelocity result =
      interpolate_hairpin_velocity(velocity(40), velocity(100), Rational(0));
  EXPECT_EQ(result.value(), 40u);
}

TEST(PlaybackMappingTest, HairpinInterpolationAtEndReturnsTo) {
  const MidiVelocity result =
      interpolate_hairpin_velocity(velocity(40), velocity(100), Rational(1));
  EXPECT_EQ(result.value(), 100u);
}

TEST(PlaybackMappingTest, HairpinInterpolationAtMidpointIsExact) {
  const MidiVelocity result = interpolate_hairpin_velocity(
      velocity(40), velocity(100), *Rational::create(1, 2));
  EXPECT_EQ(result.value(), 70u);
}

TEST(PlaybackMappingTest, HairpinInterpolationExactTieRoundsToEvenDown) {
  // 60 + (61 - 60) * 1/2 == 60.5 -- ties to the even neighbor, 60.
  const MidiVelocity result = interpolate_hairpin_velocity(
      velocity(60), velocity(61), *Rational::create(1, 2));
  EXPECT_EQ(result.value(), 60u);
}

TEST(PlaybackMappingTest, HairpinInterpolationExactTieRoundsToEvenUp) {
  // 61 + (62 - 61) * 1/2 == 61.5 -- ties to the even neighbor, 62.
  const MidiVelocity result = interpolate_hairpin_velocity(
      velocity(61), velocity(62), *Rational::create(1, 2));
  EXPECT_EQ(result.value(), 62u);
}

TEST(PlaybackMappingTest, HairpinInterpolationDescendingSpanWorks) {
  const MidiVelocity result = interpolate_hairpin_velocity(
      velocity(100), velocity(40), *Rational::create(1, 2));
  EXPECT_EQ(result.value(), 70u);
}

TEST(PlaybackMappingTest, HairpinInterpolationClampsOutOfRangePosition) {
  const MidiVelocity below =
      interpolate_hairpin_velocity(velocity(40), velocity(100), Rational(-1));
  EXPECT_EQ(below.value(), 40u);

  const MidiVelocity above =
      interpolate_hairpin_velocity(velocity(40), velocity(100), Rational(2));
  EXPECT_EQ(above.value(), 100u);
}

// -- apply_emphasis -----------------------------------------------------------

TEST(PlaybackMappingTest, EmphasisNeitherAccentNorMarcatoLeavesBaseUnchanged) {
  const MidiVelocity result = apply_emphasis(velocity(80), false, false);
  EXPECT_EQ(result.value(), 80u);
}

TEST(PlaybackMappingTest, EmphasisAccentOnlyAddsAccentBoost) {
  const MidiVelocity result = apply_emphasis(velocity(80), true, false);
  EXPECT_EQ(result.value(), 80u + kAccentVelocityBoost);
}

TEST(PlaybackMappingTest, EmphasisMarcatoOnlyAddsMarcatoBoost) {
  const MidiVelocity result = apply_emphasis(velocity(80), false, true);
  EXPECT_EQ(result.value(), 80u + kMarcatoVelocityBoost);
}

TEST(PlaybackMappingTest, EmphasisBothPresentMarcatoWinsOutright) {
  const MidiVelocity result = apply_emphasis(velocity(80), true, true);
  EXPECT_EQ(result.value(), 80u + kMarcatoVelocityBoost);
}

TEST(PlaybackMappingTest, EmphasisSaturatesAtLegalMaximum) {
  const MidiVelocity accent_result = apply_emphasis(velocity(120), true, false);
  EXPECT_EQ(accent_result.value(), MidiVelocity::kMax);

  const MidiVelocity marcato_result =
      apply_emphasis(velocity(120), false, true);
  EXPECT_EQ(marcato_result.value(), MidiVelocity::kMax);

  const MidiVelocity already_max_result =
      apply_emphasis(velocity(MidiVelocity::kMax), true, true);
  EXPECT_EQ(already_max_result.value(), MidiVelocity::kMax);
}

// -- sounded_duration_for_articulation ---------------------------------------

TEST(PlaybackMappingTest, NoArticulationAppliesDefaultDetacheRatio) {
  const Rational result =
      sounded_duration_for_articulation(Rational(1), std::nullopt, false);
  EXPECT_EQ(result, kDefaultSoundedDurationRatio);
}

TEST(PlaybackMappingTest, StaccatoHalvesNotatedDuration) {
  const Rational result = sounded_duration_for_articulation(
      Rational(1), Articulation::kStaccato, false);
  EXPECT_EQ(result, kStaccatoSoundedDurationRatio);
}

TEST(PlaybackMappingTest, StaccatissimoIsShorterThanStaccato) {
  const Rational result = sounded_duration_for_articulation(
      Rational(1), Articulation::kStaccatissimo, false);
  EXPECT_EQ(result, kStaccatissimoSoundedDurationRatio);
  EXPECT_LT(result, kStaccatoSoundedDurationRatio);
}

TEST(PlaybackMappingTest, TenutoHoldsTheFullNotatedValue) {
  const Rational result = sounded_duration_for_articulation(
      *Rational::create(3, 4), Articulation::kTenuto, false);
  EXPECT_EQ(result, *Rational::create(3, 4));
  EXPECT_EQ(kTenutoSoundedDurationRatio, Rational(1));
}

TEST(PlaybackMappingTest, AccentAndMarcatoDoNotShortenLikeStaccato) {
  // kAccent/kMarcato are velocity-only and, through the domain-layer
  // wiring, never actually reach this function as `duration_articulation`
  // (is_duration_articulation() excludes them -- see
  // notation_playback.cpp's find_duration_articulation()). This function's
  // switch still handles them defensively for direct core-level callers:
  // they get the same default detache ratio as no explicit duration
  // articulation at all, never one of the three shortening ratios
  // (staccato/staccatissimo/tenuto).
  const Rational accent_result = sounded_duration_for_articulation(
      Rational(1), Articulation::kAccent, false);
  const Rational marcato_result = sounded_duration_for_articulation(
      Rational(1), Articulation::kMarcato, false);
  const Rational no_articulation_result =
      sounded_duration_for_articulation(Rational(1), std::nullopt, false);

  EXPECT_EQ(accent_result, kDefaultSoundedDurationRatio);
  EXPECT_EQ(marcato_result, kDefaultSoundedDurationRatio);
  EXPECT_EQ(accent_result, no_articulation_result);
  EXPECT_EQ(marcato_result, no_articulation_result);
  EXPECT_NE(accent_result, kStaccatoSoundedDurationRatio);
}

TEST(PlaybackMappingTest, TiedEventIgnoresArticulationShortening) {
  const Rational staccato_but_tied = sounded_duration_for_articulation(
      *Rational::create(3, 4), Articulation::kStaccato, /*is_tied=*/true);
  EXPECT_EQ(staccato_but_tied, *Rational::create(3, 4));

  const Rational staccatissimo_but_tied = sounded_duration_for_articulation(
      *Rational::create(3, 4), Articulation::kStaccatissimo,
      /*is_tied=*/true);
  EXPECT_EQ(staccatissimo_but_tied, *Rational::create(3, 4));

  const Rational untied = sounded_duration_for_articulation(
      *Rational::create(3, 4), Articulation::kStaccato, /*is_tied=*/false);
  EXPECT_NE(untied, *Rational::create(3, 4));
}

// -- legato_sounded_duration --------------------------------------------------

TEST(PlaybackMappingTest, LegatoExtendsFullyIntoTheGap) {
  const Rational gap    = *Rational::create(1, 8);
  const Rational result = legato_sounded_duration(*Rational::create(1, 2), gap);
  EXPECT_EQ(result, *Rational::create(5, 8));
}

TEST(PlaybackMappingTest,
     LegatoWithZeroGapReturnsArticulatedDurationUnchanged) {
  const Rational result =
      legato_sounded_duration(*Rational::create(1, 2), Rational(0));
  EXPECT_EQ(result, *Rational::create(1, 2));
}

TEST(PlaybackMappingTest, LegatoInteractsWithEveryArticulationRatio) {
  const Rational notated = Rational(1);
  const Rational gap     = *Rational::create(1, 4);

  const Rational default_then_legato = legato_sounded_duration(
      sounded_duration_for_articulation(notated, std::nullopt, false), gap);
  EXPECT_EQ(default_then_legato, kDefaultSoundedDurationRatio + gap);

  const Rational staccato_then_legato =
      legato_sounded_duration(sounded_duration_for_articulation(
                                  notated, Articulation::kStaccato, false),
                              gap);
  EXPECT_EQ(staccato_then_legato, kStaccatoSoundedDurationRatio + gap);

  const Rational staccatissimo_then_legato =
      legato_sounded_duration(sounded_duration_for_articulation(
                                  notated, Articulation::kStaccatissimo, false),
                              gap);
  EXPECT_EQ(staccatissimo_then_legato,
            kStaccatissimoSoundedDurationRatio + gap);

  const Rational tenuto_then_legato = legato_sounded_duration(
      sounded_duration_for_articulation(notated, Articulation::kTenuto, false),
      gap);
  EXPECT_EQ(tenuto_then_legato, kTenutoSoundedDurationRatio + gap);
}

TEST(PlaybackMappingTest,
     LegatoOverridesShorteningWhenCallerBypassesArticulation) {
  // notation_playback.hpp's precedence: a slurred note bypasses
  // sounded_duration_for_articulation() entirely and passes the raw
  // notated duration straight into legato_sounded_duration().
  const Rational notated = Rational(1);
  const Rational gap     = *Rational::create(1, 4);
  const Rational result  = legato_sounded_duration(notated, gap);
  EXPECT_EQ(result, *Rational::create(5, 4));
}

// -- grace_steal_durations / grace_steal_remaining_duration ------------------

TEST(PlaybackMappingTest, AcciaccaturaSingleNoteStealsAMinimalSliver) {
  const Rational              available = Rational(1);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAcciaccatura, 1, available);
  ASSERT_EQ(shares.size(), 1u);
  EXPECT_EQ(shares[0], kAcciaccaturaPerNoteStealFraction);
}

TEST(PlaybackMappingTest, AcciaccaturaMultiNoteFrontLoads) {
  const Rational              available = Rational(1);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAcciaccatura, 3, available);
  ASSERT_EQ(shares.size(), 3u);
  // Total fraction 3/16 (below the 1/2 cap), weights [1/2, 1/4, 1/4].
  EXPECT_EQ(shares[0], *Rational::create(3, 32));
  EXPECT_EQ(shares[1], *Rational::create(3, 64));
  EXPECT_EQ(shares[2], *Rational::create(3, 64));
  EXPECT_GT(shares[0], shares[1]);
  EXPECT_EQ(shares[1], shares[2]);
  for (const Rational& share : shares)
    EXPECT_GT(share, Rational(0));
}

TEST(PlaybackMappingTest, AppoggiaturaSingleNoteStealsAStructuredShare) {
  const Rational              available = Rational(1);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAppoggiatura, 1, available);
  ASSERT_EQ(shares.size(), 1u);
  EXPECT_EQ(shares[0], kAppoggiaturaPerNoteStealFraction);
  EXPECT_GT(kAppoggiaturaPerNoteStealFraction,
            kAcciaccaturaPerNoteStealFraction);
}

TEST(PlaybackMappingTest, AppoggiaturaMultiNoteDividesEvenly) {
  const Rational              available = Rational(1);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAppoggiatura, 2, available);
  ASSERT_EQ(shares.size(), 2u);
  EXPECT_EQ(shares[0], shares[1]);
  EXPECT_EQ(shares[0] + shares[1],
            kAppoggiaturaPerNoteStealFraction * Rational(2));
}

TEST(PlaybackMappingTest, AcciaccaturaStealClampsAtTheCap) {
  const Rational              available = Rational(1);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAcciaccatura, 10, available);
  ASSERT_EQ(shares.size(), 10u);
  Rational total(0);
  for (const Rational& share : shares) {
    total = total + share;
    EXPECT_GT(share, Rational(0));
  }
  EXPECT_EQ(total, kMaxGraceStealFraction * available);
}

TEST(PlaybackMappingTest, AcciaccaturaStealFractionExactlyAtCapBoundary) {
  // 8 * kAcciaccaturaPerNoteStealFraction (1/16) == 1/2 exactly:
  // total_fraction_uncapped lands exactly on kMaxGraceStealFraction, the
  // boundary between the uncapped and capped regimes, rather than strictly
  // under it (3, existing tests) or strictly over it (10, existing tests).
  const Rational available = Rational(1);
  ASSERT_EQ(kAcciaccaturaPerNoteStealFraction * Rational(8),
            kMaxGraceStealFraction);

  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAcciaccatura, 8, available);
  ASSERT_EQ(shares.size(), 8u);
  Rational total(0);
  for (const Rational& share : shares) {
    total = total + share;
    EXPECT_GT(share, Rational(0));
  }
  EXPECT_EQ(total, kMaxGraceStealFraction * available);
  EXPECT_GT(shares[0], shares[1]);
}

TEST(PlaybackMappingTest,
     AcciaccaturaRemainingDurationNeverConsumesTheEntirePrecedingNote) {
  const Rational available = Rational(1);
  const Rational remaining = grace_steal_remaining_duration(
      GraceNoteType::kAcciaccatura, 10, available);
  EXPECT_GE(remaining, available * kMaxGraceStealFraction);
}

TEST(PlaybackMappingTest, AppoggiaturaStealClampsAtTheCap) {
  const Rational              available = Rational(1);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAppoggiatura, 4, available);
  ASSERT_EQ(shares.size(), 4u);
  Rational total(0);
  for (const Rational& share : shares) {
    total = total + share;
    EXPECT_EQ(share, *Rational::create(1, 8));
  }
  EXPECT_EQ(total, kMaxGraceStealFraction * available);
}

TEST(PlaybackMappingTest, GraceStealZeroNoteCountReturnsEmpty) {
  EXPECT_TRUE(
      grace_steal_durations(GraceNoteType::kAcciaccatura, 0, Rational(1))
          .empty());
}

TEST(PlaybackMappingTest, NoPrecedingSoundedNoteFallsBackToFixedDuration) {
  const std::vector<Rational> zero_available =
      grace_steal_durations(GraceNoteType::kAppoggiatura, 3, Rational(0));
  ASSERT_EQ(zero_available.size(), 3u);
  for (const Rational& share : zero_available)
    EXPECT_EQ(share, kGraceFallbackNoteDuration);

  const std::vector<Rational> negative_available = grace_steal_durations(
      GraceNoteType::kAcciaccatura, 2, *Rational::create(-1, 4));
  ASSERT_EQ(negative_available.size(), 2u);
  for (const Rational& share : negative_available)
    EXPECT_EQ(share, kGraceFallbackNoteDuration);
}

TEST(PlaybackMappingTest, RemainingDurationIsAvailableMinusStolenTotal) {
  const Rational available = Rational(1);
  const Rational remaining = grace_steal_remaining_duration(
      GraceNoteType::kAcciaccatura, 3, available);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAcciaccatura, 3, available);
  Rational total(0);
  for (const Rational& share : shares)
    total = total + share;
  EXPECT_EQ(remaining, available - total);
  EXPECT_GT(remaining, Rational(0));
}

TEST(PlaybackMappingTest,
     RemainingDurationNeverConsumesTheEntirePrecedingNote) {
  const Rational available = Rational(1);
  const Rational remaining = grace_steal_remaining_duration(
      GraceNoteType::kAppoggiatura, 10, available);
  EXPECT_GE(remaining, available * kMaxGraceStealFraction);
}

TEST(PlaybackMappingTest, RemainingDurationWithNoPrecedingNoteIsUnchanged) {
  EXPECT_EQ(grace_steal_remaining_duration(GraceNoteType::kAcciaccatura, 2,
                                           Rational(0)),
            Rational(0));
  const Rational negative = *Rational::create(-1, 4);
  EXPECT_EQ(
      grace_steal_remaining_duration(GraceNoteType::kAppoggiatura, 2, negative),
      negative);
}

// -- kMaxGraceNotesPerGroup bound and degenerate shape past it ---------------

TEST(PlaybackMappingTest,
     AcciaccaturaWeightingAtExactlyTheBoundMatchesUnboundedShape) {
  // note_count == kMaxGraceNotesPerGroup is still within the ordinary
  // geometric-halving regime: this exercises the boundary where the depth
  // cap and note_count coincide exactly.
  const Rational              available = Rational(1);
  const std::vector<Rational> shares    = grace_steal_durations(
      GraceNoteType::kAcciaccatura, kMaxGraceNotesPerGroup, available);
  ASSERT_EQ(shares.size(), kMaxGraceNotesPerGroup);

  Rational total(0);
  for (const Rational& share : shares) {
    EXPECT_GT(share, Rational(0));
    total = total + share;
  }
  EXPECT_EQ(total, kMaxGraceStealFraction * available);
  // Strictly front-loaded, except the final two notes, which always share
  // the series' own smallest term equally by construction (see
  // AcciaccaturaMultiNoteFrontLoads' n=3 case: shares [1/2, 1/4, 1/4]).
  for (std::size_t index = 1; index + 1 < shares.size(); ++index)
    EXPECT_LT(shares[index], shares[index - 1]);
  EXPECT_EQ(shares[shares.size() - 1], shares[shares.size() - 2]);
}

TEST(PlaybackMappingTest,
     AcciaccaturaWeightingPastTheBoundSharesTheSmallestSlotEqually) {
  // note_count == kMaxGraceNotesPerGroup + 1: the halving stops at the
  // bound and the two notes past it split the smallest slot evenly rather
  // than one of them continuing to halve.
  const std::size_t           note_count = kMaxGraceNotesPerGroup + 1;
  const Rational              available  = Rational(1);
  const std::vector<Rational> shares     = grace_steal_durations(
      GraceNoteType::kAcciaccatura, note_count, available);
  ASSERT_EQ(shares.size(), note_count);

  Rational total(0);
  for (const Rational& share : shares) {
    EXPECT_GT(share, Rational(0));
    total = total + share;
  }
  EXPECT_EQ(total, kMaxGraceStealFraction * available);

  // The last two notes (index kMaxGraceNotesPerGroup - 1 and index
  // kMaxGraceNotesPerGroup) share the smallest slot equally, breaking the
  // otherwise strict front-loading.
  EXPECT_EQ(shares[kMaxGraceNotesPerGroup - 1], shares[kMaxGraceNotesPerGroup]);
  EXPECT_LT(shares[kMaxGraceNotesPerGroup - 1],
            shares[kMaxGraceNotesPerGroup - 2]);
}

TEST(PlaybackMappingTest,
     GraceStealDurationsSafeFarPastTheBoundNoShiftOrOverflowUb) {
  // Reproduces the reviewer's exact UBSan repro: note_count=70 previously
  // shifted a 64-bit integer by an exponent up to 69 (UB, silently wrong
  // under non-sanitized execution). With the depth-capped weighting this
  // executes cleanly and every note still gets a strictly positive,
  // well-defined share summing to exactly the capped total.
  const Rational              available = Rational(1);
  const std::vector<Rational> shares =
      grace_steal_durations(GraceNoteType::kAcciaccatura, 70, available);
  ASSERT_EQ(shares.size(), 70u);

  Rational total(0);
  for (const Rational& share : shares) {
    EXPECT_GT(share, Rational(0));
    total = total + share;
  }
  EXPECT_EQ(total, kMaxGraceStealFraction * available);
  // All notes past the bound collapse to the same shared, smallest slot.
  EXPECT_EQ(shares[kMaxGraceNotesPerGroup - 1], shares[69]);
}

TEST(PlaybackMappingTest,
     GraceStealRemainingDurationSafeFarPastTheBoundNoOverflowUb) {
  // Reproduces the reviewer's exact UBSan repro: note_count=32 previously
  // overflowed Rational's denominator arithmetic inside a summation loop
  // that no longer exists (the closed-form fix removes it entirely).
  const Rational available = Rational(1);
  const Rational remaining = grace_steal_remaining_duration(
      GraceNoteType::kAcciaccatura, 32, available);
  EXPECT_EQ(remaining, available * (Rational(1) - kMaxGraceStealFraction));
}
