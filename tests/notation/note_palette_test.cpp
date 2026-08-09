// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

using graphscore::Articulation;
using graphscore::BeamOverride;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::HairpinDirection;
using graphscore::is_duration_articulation;
using graphscore::kAllArticulations;
using graphscore::kArticulationCount;
using graphscore::NotePaletteEntryKind;
using graphscore::NotePaletteEntrySpec;
using graphscore::NotePaletteState;
using graphscore::NoteValue;
using graphscore::TupletRatio;
using graphscore::Voice;

[[nodiscard]] NotePaletteState default_state() {
  const auto state = NotePaletteState::create(
      NoteValue::kQuarter, 0, NotePaletteEntryKind::kNote, *Voice::create(1));
  return *state;
}

// ---- Duration side: note value, dots, note-vs-rest, tuplet ----

TEST(NotePaletteStateTest, AcceptsEveryNoteValueFromWholeThroughSixtyFourth) {
  constexpr std::array<NoteValue, 7> kValues = {
      NoteValue::kWhole,       NoteValue::kHalf,      NoteValue::kQuarter,
      NoteValue::kEighth,      NoteValue::kSixteenth, NoteValue::kThirtySecond,
      NoteValue::kSixtyFourth,
  };
  for (const NoteValue value : kValues) {
    const auto state = NotePaletteState::create(
        value, 0, NotePaletteEntryKind::kNote, *Voice::create(1));
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->note_value(), value);

    const auto via_setter = default_state().with_note_value(value);
    EXPECT_EQ(via_setter.note_value(), value);
  }
}

TEST(NotePaletteStateTest, AcceptsZeroOneAndTwoDots) {
  for (std::uint8_t dots = 0; dots <= Duration::kMaxDots; ++dots) {
    const auto created = NotePaletteState::create(NoteValue::kQuarter, dots,
                                                  NotePaletteEntryKind::kNote,
                                                  *Voice::create(1));
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->dots(), dots);

    const auto via_setter = default_state().with_dots(dots);
    ASSERT_TRUE(via_setter.has_value());
    EXPECT_EQ(via_setter->dots(), dots);
  }
}

TEST(NotePaletteStateTest, RejectsThreeOrMoreDots) {
  EXPECT_FALSE(NotePaletteState::create(NoteValue::kQuarter, 3,
                                        NotePaletteEntryKind::kNote,
                                        *Voice::create(1))
                   .has_value());
  EXPECT_FALSE(NotePaletteState::create(NoteValue::kQuarter, 255,
                                        NotePaletteEntryKind::kNote,
                                        *Voice::create(1))
                   .has_value());
  EXPECT_FALSE(default_state().with_dots(3).has_value());
  EXPECT_FALSE(default_state().with_dots(255).has_value());
}

TEST(NotePaletteStateTest, TogglesBetweenNoteAndRestEntryKind) {
  const NotePaletteState note_state = default_state();
  EXPECT_EQ(note_state.entry_kind(), NotePaletteEntryKind::kNote);

  const NotePaletteState rest_state =
      note_state.with_entry_kind(NotePaletteEntryKind::kRest);
  EXPECT_EQ(rest_state.entry_kind(), NotePaletteEntryKind::kRest);

  const auto created_rest = NotePaletteState::create(
      NoteValue::kEighth, 0, NotePaletteEntryKind::kRest, *Voice::create(1));
  ASSERT_TRUE(created_rest.has_value());
  EXPECT_EQ(created_rest->entry_kind(), NotePaletteEntryKind::kRest);

  const NotePaletteState back_to_note =
      rest_state.with_entry_kind(NotePaletteEntryKind::kNote);
  EXPECT_EQ(back_to_note.entry_kind(), NotePaletteEntryKind::kNote);
}

TEST(NotePaletteStateTest, AcceptsAValidTupletAndComposesItIntoDuration) {
  const auto ratio = TupletRatio::create(3, 2);
  ASSERT_TRUE(ratio.has_value());
  const NotePaletteState state = default_state().with_tuplet(*ratio);
  ASSERT_TRUE(state.tuplet().has_value());
  EXPECT_EQ(*state.tuplet(), *ratio);

  const Duration resolved = state.resolved_duration();
  const auto     expected =
      Duration::create(state.note_value(), state.dots(), *ratio);
  ASSERT_TRUE(expected.has_value());
  EXPECT_EQ(resolved.resolved(), expected->resolved());
}

// Documents that NotePaletteState delegates malformed-tuplet rejection
// entirely to TupletRatio::create (with_tuplet()/create() accept any
// TupletRatio the core type already validated); TupletRatio::create's own
// rejection behavior is validated by graphscore_core's own tests, not here.
TEST(NotePaletteStateTest,
     MalformedTupletRejectionDelegatesToTupletRatioCreate) {
  EXPECT_FALSE(TupletRatio::create(0, 2).has_value());
  EXPECT_FALSE(TupletRatio::create(3, 0).has_value());
  EXPECT_FALSE(TupletRatio::create(0, 0).has_value());
}

TEST(NotePaletteStateTest, ClearsAnArmedTupletBackToNullopt) {
  const auto             ratio      = TupletRatio::create(5, 4);
  const NotePaletteState with_ratio = default_state().with_tuplet(*ratio);
  ASSERT_TRUE(with_ratio.tuplet().has_value());

  const NotePaletteState cleared = with_ratio.with_tuplet(std::nullopt);
  EXPECT_FALSE(cleared.tuplet().has_value());
}

// ---- Voice side ----

TEST(NotePaletteStateTest, AcceptsAllFourVoices) {
  for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
    const auto voice = Voice::create(index);
    ASSERT_TRUE(voice.has_value());
    const auto state = NotePaletteState::create(
        NoteValue::kQuarter, 0, NotePaletteEntryKind::kNote, *voice);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->voice(), *voice);

    const NotePaletteState via_setter = default_state().with_voice(*voice);
    EXPECT_EQ(via_setter.voice(), *voice);
  }
}

// Documents that NotePaletteState delegates out-of-range voice rejection
// entirely to Voice::create (with_voice()/create() only ever accept an
// already-validated Voice); Voice::create's own rejection behavior is
// validated by graphscore_core's own tests, not here.
TEST(NotePaletteStateTest, OutOfRangeVoiceRejectionDelegatesToVoiceCreate) {
  EXPECT_FALSE(Voice::create(0).has_value());
  EXPECT_FALSE(Voice::create(5).has_value());
}

// ---- Armed markings: articulations ----

TEST(NotePaletteStateTest, ArmsAccentAndMarcatoTogether) {
  const auto with_accent =
      default_state().with_articulation_armed(Articulation::kAccent);
  ASSERT_TRUE(with_accent.has_value());
  EXPECT_TRUE(with_accent->has_articulation(Articulation::kAccent));

  const auto with_both =
      with_accent->with_articulation_armed(Articulation::kMarcato);
  ASSERT_TRUE(with_both.has_value());
  EXPECT_TRUE(with_both->has_articulation(Articulation::kAccent));
  EXPECT_TRUE(with_both->has_articulation(Articulation::kMarcato));
}

TEST(NotePaletteStateTest,
     RejectsArmingASecondDurationArticulationWhileOneIsArmed) {
  for (const Articulation first : kAllArticulations) {
    if (!is_duration_articulation(first))
      continue;
    for (const Articulation second : kAllArticulations) {
      if (first == second || !is_duration_articulation(second))
        continue;
      const auto with_first = default_state().with_articulation_armed(first);
      ASSERT_TRUE(with_first.has_value());
      const auto with_both = with_first->with_articulation_armed(second);
      EXPECT_FALSE(with_both.has_value())
          << "arming " << static_cast<int>(second) << " while "
          << static_cast<int>(first) << " is armed must be rejected";
    }
  }
}

TEST(NotePaletteStateTest, ReArmingTheSameDurationArticulationSucceeds) {
  const auto with_staccato =
      default_state().with_articulation_armed(Articulation::kStaccato);
  ASSERT_TRUE(with_staccato.has_value());
  const auto still_staccato =
      with_staccato->with_articulation_armed(Articulation::kStaccato);
  ASSERT_TRUE(still_staccato.has_value());
  EXPECT_TRUE(still_staccato->has_articulation(Articulation::kStaccato));
}

TEST(NotePaletteStateTest, DisarmingFreesTheDurationArticulationSlot) {
  const auto with_tenuto =
      default_state().with_articulation_armed(Articulation::kTenuto);
  ASSERT_TRUE(with_tenuto.has_value());

  const NotePaletteState disarmed =
      with_tenuto->with_articulation_disarmed(Articulation::kTenuto);
  EXPECT_FALSE(disarmed.has_articulation(Articulation::kTenuto));

  const auto with_staccatissimo =
      disarmed.with_articulation_armed(Articulation::kStaccatissimo);
  ASSERT_TRUE(with_staccatissimo.has_value());
  EXPECT_TRUE(
      with_staccatissimo->has_articulation(Articulation::kStaccatissimo));
}

// Arms the maximal set the mutual-exclusion rule allows: both freely
// combining markings (accent, marcato) plus exactly one duration
// articulation. This is the case that would catch a stale
// kAllArticulations copy (M1): if armed_articulations() derived from its
// own independent, out-of-date list, one of these three would silently go
// missing from the result.
TEST(NotePaletteStateTest, ArmedArticulationsReportsAMaximalArmedSet) {
  const auto with_accent =
      default_state().with_articulation_armed(Articulation::kAccent);
  ASSERT_TRUE(with_accent.has_value());
  const auto with_accent_and_marcato =
      with_accent->with_articulation_armed(Articulation::kMarcato);
  ASSERT_TRUE(with_accent_and_marcato.has_value());
  const auto with_all_three =
      with_accent_and_marcato->with_articulation_armed(Articulation::kStaccato);
  ASSERT_TRUE(with_all_three.has_value());

  const std::vector<Articulation> armed = with_all_three->armed_articulations();
  EXPECT_EQ(armed, (std::vector<Articulation>{Articulation::kAccent,
                                              Articulation::kMarcato,
                                              Articulation::kStaccato}));
}

TEST(NotePaletteStateTest, DefaultStateHasNoArmedArticulation) {
  EXPECT_TRUE(default_state().armed_articulations().empty());
  for (const Articulation articulation : kAllArticulations) {
    EXPECT_FALSE(default_state().has_articulation(articulation));
  }
}

// Pins articulation_bit()'s out-of-range policy (M2) and
// with_articulation_armed()'s rejection of an out-of-range value: an
// Articulation value at or past kArticulationCount is always reported
// unarmed, is rejected by with_articulation_armed() rather than silently
// accepted, and can never disarm or otherwise corrupt any in-range slot of
// the mask, through the public API alone. Covers both an adjacent
// out-of-range value (kArticulationCount) and a far one (200) large enough
// to have driven articulation_bit()'s pre-fix shift past the type's width
// (undefined behavior, not merely an aliased bit).
TEST(NotePaletteStateTest,
     OutOfRangeArticulationIsRejectedAndNeverCorruptsTheMask) {
  // Test-only malformed-input boundary: intentionally construct unknown
  // enum values to verify articulation_bit()'s out-of-range policy.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  const auto adjacent = static_cast<Articulation>(kArticulationCount);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  const auto far = static_cast<Articulation>(200);

  for (const Articulation out_of_range : {adjacent, far}) {
    EXPECT_FALSE(default_state().has_articulation(out_of_range));

    EXPECT_FALSE(
        default_state().with_articulation_armed(out_of_range).has_value());

    const auto with_accent =
        default_state().with_articulation_armed(Articulation::kAccent);
    ASSERT_TRUE(with_accent.has_value());
    EXPECT_FALSE(
        with_accent->with_articulation_armed(out_of_range).has_value());

    const NotePaletteState disarmed_out_of_range =
        with_accent->with_articulation_disarmed(out_of_range);
    EXPECT_EQ(disarmed_out_of_range, *with_accent);
    EXPECT_TRUE(disarmed_out_of_range.has_articulation(Articulation::kAccent));
  }
}

// ---- Armed markings: dynamics ----

TEST(NotePaletteStateTest, ArmsAndReadsEveryDynamicEnumerator) {
  constexpr std::array<Dynamic, 8> kDynamics = {
      Dynamic::kPpp, Dynamic::kPp, Dynamic::kP,  Dynamic::kMp,
      Dynamic::kMf,  Dynamic::kF,  Dynamic::kFf, Dynamic::kFff,
  };
  for (const Dynamic dynamic : kDynamics) {
    const NotePaletteState state = default_state().with_dynamic(dynamic);
    ASSERT_TRUE(state.dynamic().has_value());
    EXPECT_EQ(*state.dynamic(), dynamic);
  }
}

TEST(NotePaletteStateTest, ClearsAnArmedDynamicBackToNullopt) {
  const NotePaletteState armed = default_state().with_dynamic(Dynamic::kFff);
  ASSERT_TRUE(armed.dynamic().has_value());
  const NotePaletteState cleared = armed.with_dynamic(std::nullopt);
  EXPECT_FALSE(cleared.dynamic().has_value());
}

// ---- Armed markings: hairpins ----

TEST(NotePaletteStateTest, ArmsBothHairpinDirections) {
  const NotePaletteState crescendo =
      default_state().with_hairpin_direction(HairpinDirection::kCrescendo);
  ASSERT_TRUE(crescendo.hairpin_direction().has_value());
  EXPECT_EQ(*crescendo.hairpin_direction(), HairpinDirection::kCrescendo);

  const NotePaletteState diminuendo =
      default_state().with_hairpin_direction(HairpinDirection::kDiminuendo);
  ASSERT_TRUE(diminuendo.hairpin_direction().has_value());
  EXPECT_EQ(*diminuendo.hairpin_direction(), HairpinDirection::kDiminuendo);

  const NotePaletteState cleared =
      crescendo.with_hairpin_direction(std::nullopt);
  EXPECT_FALSE(cleared.hairpin_direction().has_value());
}

// ---- Armed markings: tie/slur/pedal ----

TEST(NotePaletteStateTest, ArmsAndDisarmsTieToNext) {
  EXPECT_FALSE(default_state().tie_to_next_armed());
  const NotePaletteState armed = default_state().with_tie_to_next_armed(true);
  EXPECT_TRUE(armed.tie_to_next_armed());
  const NotePaletteState disarmed = armed.with_tie_to_next_armed(false);
  EXPECT_FALSE(disarmed.tie_to_next_armed());
}

TEST(NotePaletteStateTest, ArmsAndDisarmsSlur) {
  EXPECT_FALSE(default_state().slur_armed());
  const NotePaletteState armed = default_state().with_slur_armed(true);
  EXPECT_TRUE(armed.slur_armed());
  const NotePaletteState disarmed = armed.with_slur_armed(false);
  EXPECT_FALSE(disarmed.slur_armed());
}

TEST(NotePaletteStateTest, ArmsAndDisarmsPedal) {
  EXPECT_FALSE(default_state().pedal_armed());
  const NotePaletteState armed = default_state().with_pedal_armed(true);
  EXPECT_TRUE(armed.pedal_armed());
  const NotePaletteState disarmed = armed.with_pedal_armed(false);
  EXPECT_FALSE(disarmed.pedal_armed());
}

// ---- Armed markings: beam override ----

TEST(NotePaletteStateTest, ArmsBothBeamOverrideKinds) {
  const NotePaletteState broken =
      default_state().with_beam_override_kind(BeamOverride::Kind::kBreak);
  ASSERT_TRUE(broken.beam_override_kind().has_value());
  EXPECT_EQ(*broken.beam_override_kind(), BeamOverride::Kind::kBreak);

  const NotePaletteState joined =
      default_state().with_beam_override_kind(BeamOverride::Kind::kJoin);
  ASSERT_TRUE(joined.beam_override_kind().has_value());
  EXPECT_EQ(*joined.beam_override_kind(), BeamOverride::Kind::kJoin);

  const NotePaletteState cleared = broken.with_beam_override_kind(std::nullopt);
  EXPECT_FALSE(cleared.beam_override_kind().has_value());
}

// ---- Derived queries ----

TEST(NotePaletteStateTest, ResolvedDurationMatchesDirectDurationCreate) {
  const auto state = NotePaletteState::create(
      NoteValue::kSixteenth, 2, NotePaletteEntryKind::kNote, *Voice::create(3));
  ASSERT_TRUE(state.has_value());
  const Duration resolved = state->resolved_duration();
  const auto     expected = Duration::create(NoteValue::kSixteenth, 2);
  ASSERT_TRUE(expected.has_value());
  EXPECT_EQ(resolved, *expected);
}

TEST(NotePaletteStateTest, ResolvedDurationComposesTheTupletPath) {
  const auto ratio = TupletRatio::create(5, 4);
  ASSERT_TRUE(ratio.has_value());
  const auto state = NotePaletteState::create(NoteValue::kEighth, 1,
                                              NotePaletteEntryKind::kNote,
                                              *Voice::create(2), *ratio);
  ASSERT_TRUE(state.has_value());

  const Duration resolved = state->resolved_duration();
  const auto     expected = Duration::create(NoteValue::kEighth, 1, *ratio);
  ASSERT_TRUE(expected.has_value());
  EXPECT_EQ(resolved.resolved(), expected->resolved());
  EXPECT_EQ(*resolved.tuplet(), *ratio);
}

// Builds a NotePaletteState with every field armed to a non-default value,
// used by NextEntrySpecComposesEveryArmedField and both
// EqualityComparesEveryField tests below so each genuinely exercises every
// field rather than two or three of them.
[[nodiscard]] NotePaletteState fully_populated_state() {
  const auto ratio = TupletRatio::create(3, 2);
  const auto built = NotePaletteState::create(NoteValue::kEighth, 1,
                                              NotePaletteEntryKind::kRest,
                                              *Voice::create(4), *ratio);
  const auto with_accent =
      built->with_articulation_armed(Articulation::kAccent);
  return with_accent->with_dynamic(Dynamic::kMf)
      .with_hairpin_direction(HairpinDirection::kCrescendo)
      .with_tie_to_next_armed(true)
      .with_slur_armed(true)
      .with_pedal_armed(true)
      .with_beam_override_kind(BeamOverride::Kind::kJoin);
}

TEST(NotePaletteStateTest, NextEntrySpecComposesEveryArmedField) {
  const NotePaletteState state = fully_populated_state();

  const NotePaletteEntrySpec spec = state.next_entry_spec();
  EXPECT_EQ(spec.duration, state.resolved_duration());
  EXPECT_EQ(spec.duration.base(), NoteValue::kEighth);
  EXPECT_EQ(spec.entry_kind, NotePaletteEntryKind::kRest);
  EXPECT_EQ(spec.voice, *Voice::create(4));
  EXPECT_EQ(spec.articulations,
            (std::vector<Articulation>{Articulation::kAccent}));
  ASSERT_TRUE(spec.dynamic.has_value());
  EXPECT_EQ(*spec.dynamic, Dynamic::kMf);
  ASSERT_TRUE(spec.hairpin.has_value());
  EXPECT_EQ(*spec.hairpin, HairpinDirection::kCrescendo);
  EXPECT_TRUE(spec.tie_to_next);
  EXPECT_TRUE(spec.slur);
  EXPECT_TRUE(spec.pedal);
  ASSERT_TRUE(spec.beam_override.has_value());
  EXPECT_EQ(*spec.beam_override, BeamOverride::Kind::kJoin);
}

TEST(NotePaletteStateTest, NextEntrySpecOnDefaultStateHasNoArmedMarkings) {
  const NotePaletteEntrySpec spec = default_state().next_entry_spec();
  EXPECT_TRUE(spec.articulations.empty());
  EXPECT_FALSE(spec.dynamic.has_value());
  EXPECT_FALSE(spec.hairpin.has_value());
  EXPECT_FALSE(spec.tie_to_next);
  EXPECT_FALSE(spec.slur);
  EXPECT_FALSE(spec.pedal);
  EXPECT_FALSE(spec.beam_override.has_value());
}

// ---- Value semantics ----

// Differences one field at a time against a fully populated baseline (every
// field non-default; see fully_populated_state()) so this genuinely proves
// operator== considers all twelve fields, not just the two or three a
// smaller fixture would happen to exercise.
TEST(NotePaletteStateTest, EqualityComparesEveryField) {
  const NotePaletteState populated = fully_populated_state();
  EXPECT_EQ(populated, populated);

  EXPECT_NE(populated, populated.with_note_value(NoteValue::kQuarter));
  EXPECT_NE(populated, *populated.with_dots(0));
  EXPECT_NE(populated, populated.with_entry_kind(NotePaletteEntryKind::kNote));
  EXPECT_NE(populated, populated.with_tuplet(std::nullopt));
  EXPECT_NE(populated, populated.with_voice(*Voice::create(1)));
  EXPECT_NE(populated,
            populated.with_articulation_disarmed(Articulation::kAccent));
  EXPECT_NE(populated, populated.with_dynamic(std::nullopt));
  EXPECT_NE(populated, populated.with_hairpin_direction(std::nullopt));
  EXPECT_NE(populated, populated.with_tie_to_next_armed(false));
  EXPECT_NE(populated, populated.with_slur_armed(false));
  EXPECT_NE(populated, populated.with_pedal_armed(false));
  EXPECT_NE(populated, populated.with_beam_override_kind(std::nullopt));
}

// Same approach as NotePaletteStateTest.EqualityComparesEveryField, over
// NotePaletteEntrySpec's ten fields (duration through beam_override).
TEST(NotePaletteEntrySpecTest, EqualityComparesEveryField) {
  const NotePaletteEntrySpec base = fully_populated_state().next_entry_spec();
  EXPECT_EQ(base, base);

  NotePaletteEntrySpec differs = base;
  differs.duration             = *Duration::create(NoteValue::kQuarter, 0);
  EXPECT_NE(base, differs);

  differs            = base;
  differs.entry_kind = NotePaletteEntryKind::kNote;
  EXPECT_NE(base, differs);

  differs       = base;
  differs.voice = *Voice::create(1);
  EXPECT_NE(base, differs);

  differs               = base;
  differs.articulations = {};
  EXPECT_NE(base, differs);

  differs         = base;
  differs.dynamic = std::nullopt;
  EXPECT_NE(base, differs);

  differs         = base;
  differs.hairpin = std::nullopt;
  EXPECT_NE(base, differs);

  differs             = base;
  differs.tie_to_next = false;
  EXPECT_NE(base, differs);

  differs      = base;
  differs.slur = false;
  EXPECT_NE(base, differs);

  differs       = base;
  differs.pedal = false;
  EXPECT_NE(base, differs);

  differs               = base;
  differs.beam_override = std::nullopt;
  EXPECT_NE(base, differs);
}

}  // namespace
