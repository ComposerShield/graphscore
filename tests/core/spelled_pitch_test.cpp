// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::Accidental;
using graphscore::Letter;
using graphscore::SpelledPitch;

struct SpelledPitchCase {
  Letter       letter;
  std::int8_t  octave;
  Accidental   accidental;
  std::uint8_t expected_midi;
};

class SpelledPitchTableTest : public testing::TestWithParam<SpelledPitchCase> {
};

TEST_P(SpelledPitchTableTest, ResolvesToExpectedMidiPitch) {
  const auto pitch = SpelledPitch::create(GetParam().letter, GetParam().octave,
                                          GetParam().accidental);
  ASSERT_TRUE(pitch.has_value());

  const auto midi = pitch->to_midi_pitch();
  ASSERT_TRUE(midi.has_value());
  EXPECT_EQ(midi->value(), GetParam().expected_midi);
}

INSTANTIATE_TEST_SUITE_P(
    Naturals, SpelledPitchTableTest,
    testing::Values(SpelledPitchCase{Letter::kC, 4, Accidental::kNatural, 60},
                    SpelledPitchCase{Letter::kA, 4, Accidental::kNatural, 69},
                    SpelledPitchCase{Letter::kB, 3, Accidental::kNatural, 59},
                    SpelledPitchCase{Letter::kG, 9, Accidental::kNatural, 127},
                    SpelledPitchCase{Letter::kC, -1, Accidental::kNatural, 0}));

INSTANTIATE_TEST_SUITE_P(
    SharpsAndFlats, SpelledPitchTableTest,
    testing::Values(SpelledPitchCase{Letter::kC, 4, Accidental::kSharp, 61},
                    SpelledPitchCase{Letter::kD, 4, Accidental::kFlat, 61},
                    SpelledPitchCase{Letter::kF, 4, Accidental::kSharp, 66}));

INSTANTIATE_TEST_SUITE_P(
    DoubleAccidentals, SpelledPitchTableTest,
    testing::Values(
        SpelledPitchCase{Letter::kC, 4, Accidental::kDoubleSharp, 62},
        SpelledPitchCase{Letter::kD, 4, Accidental::kDoubleFlat, 60},
        SpelledPitchCase{Letter::kF, 9, Accidental::kDoubleSharp, 127}));

TEST(SpelledPitchTest, CDoubleFlatBelowZeroIsRejected) {
  const auto pitch =
      SpelledPitch::create(Letter::kC, -1, Accidental::kDoubleFlat);
  ASSERT_TRUE(pitch.has_value());
  EXPECT_FALSE(pitch->to_midi_pitch().has_value());
}

TEST(SpelledPitchTest, GDoubleSharpAboveOneTwentySevenIsRejected) {
  const auto pitch =
      SpelledPitch::create(Letter::kG, 9, Accidental::kDoubleSharp);
  ASSERT_TRUE(pitch.has_value());
  EXPECT_FALSE(pitch->to_midi_pitch().has_value());
}

TEST(SpelledPitchTest, GSharpAtTopOctaveIsRejected) {
  const auto pitch = SpelledPitch::create(Letter::kG, 9, Accidental::kSharp);
  ASSERT_TRUE(pitch.has_value());
  EXPECT_FALSE(pitch->to_midi_pitch().has_value());
}

TEST(SpelledPitchTest, OctaveBelowMinimumIsRejected) {
  EXPECT_FALSE(SpelledPitch::create(Letter::kC, -2).has_value());
}

TEST(SpelledPitchTest, OctaveAboveMaximumIsRejected) {
  EXPECT_FALSE(SpelledPitch::create(Letter::kC, 10).has_value());
}

TEST(SpelledPitchTest, EnharmonicSpellingsShareOneMidiPitch) {
  const auto c_sharp = SpelledPitch::create(Letter::kC, 4, Accidental::kSharp);
  const auto d_flat  = SpelledPitch::create(Letter::kD, 4, Accidental::kFlat);
  ASSERT_TRUE(c_sharp.has_value());
  ASSERT_TRUE(d_flat.has_value());
  EXPECT_NE(*c_sharp, *d_flat);
  EXPECT_EQ(c_sharp->to_midi_pitch(), d_flat->to_midi_pitch());
}

TEST(SpelledPitchTest, DefaultIsMiddleC) {
  const SpelledPitch pitch;
  const auto         midi = pitch.to_midi_pitch();
  ASSERT_TRUE(midi.has_value());
  EXPECT_EQ(midi->value(), 60);
}
