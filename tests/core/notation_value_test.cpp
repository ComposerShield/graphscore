// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/core/graphscore_core.hpp>

using graphscore::Accidental;
using graphscore::Clef;
using graphscore::KeySignature;
using graphscore::Mode;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::Tempo;
using graphscore::TimeSignature;
using graphscore::Voice;

struct AccidentalCase {
  std::int8_t offset;
  Accidental  expected;
};

class AccidentalTableTest : public testing::TestWithParam<AccidentalCase> {};

TEST_P(AccidentalTableTest, RoundTripsThroughOffset) {
  const auto accidental = graphscore::accidental_from_offset(GetParam().offset);
  ASSERT_TRUE(accidental.has_value());
  EXPECT_EQ(*accidental, GetParam().expected);
  EXPECT_EQ(graphscore::to_semitone_offset(*accidental), GetParam().offset);
}

INSTANTIATE_TEST_SUITE_P(
    AllAccidentals, AccidentalTableTest,
    testing::Values(AccidentalCase{-2, Accidental::kDoubleFlat},
                    AccidentalCase{-1, Accidental::kFlat},
                    AccidentalCase{0, Accidental::kNatural},
                    AccidentalCase{1, Accidental::kSharp},
                    AccidentalCase{2, Accidental::kDoubleSharp}));

TEST(AccidentalTest, OutOfRangeOffsetIsInvalid) {
  EXPECT_FALSE(graphscore::accidental_from_offset(3).has_value());
  EXPECT_FALSE(graphscore::accidental_from_offset(-3).has_value());
}

struct ClefCase {
  std::uint8_t index;
  Clef         expected;
};

class ClefTableTest : public testing::TestWithParam<ClefCase> {};

TEST_P(ClefTableTest, RoundTripsThroughIndex) {
  const auto clef = graphscore::clef_from_index(GetParam().index);
  ASSERT_TRUE(clef.has_value());
  EXPECT_EQ(*clef, GetParam().expected);
  EXPECT_EQ(graphscore::to_index(*clef), GetParam().index);
}

INSTANTIATE_TEST_SUITE_P(AllClefs, ClefTableTest,
                         testing::Values(ClefCase{0, Clef::kTreble},
                                         ClefCase{1, Clef::kBass},
                                         ClefCase{2, Clef::kAlto},
                                         ClefCase{3, Clef::kTenor}));

TEST(ClefTest, OutOfRangeIndexIsInvalid) {
  EXPECT_FALSE(graphscore::clef_from_index(4).has_value());
}

TEST(KeySignatureTest, ZeroFifthsIsValid) {
  EXPECT_TRUE(KeySignature::create(0).has_value());
}

TEST(KeySignatureTest, SevenSharpsIsValid) {
  const auto key = KeySignature::create(7);
  ASSERT_TRUE(key.has_value());
  EXPECT_EQ(key->fifths(), 7);
}

TEST(KeySignatureTest, SevenFlatsIsValid) {
  const auto key = KeySignature::create(-7);
  ASSERT_TRUE(key.has_value());
  EXPECT_EQ(key->fifths(), -7);
}

TEST(KeySignatureTest, EightSharpsIsInvalid) {
  EXPECT_FALSE(KeySignature::create(8).has_value());
}

TEST(KeySignatureTest, EightFlatsIsInvalid) {
  EXPECT_FALSE(KeySignature::create(-8).has_value());
}

TEST(KeySignatureTest, DefaultModeIsMajor) {
  EXPECT_EQ(KeySignature::create(0)->mode(), Mode::kMajor);
}

TEST(KeySignatureTest, ExplicitMinorMode) {
  EXPECT_EQ(KeySignature::create(0, Mode::kMinor)->mode(), Mode::kMinor);
}

TEST(TimeSignatureTest, CommonTimeIsValid) {
  const auto meter = TimeSignature::create(4, 4);
  ASSERT_TRUE(meter.has_value());
  EXPECT_EQ(meter->numerator(), 4);
  EXPECT_EQ(meter->denominator(), 4);
}

TEST(TimeSignatureTest, DefaultIsCommonTime) {
  const TimeSignature meter;
  EXPECT_EQ(meter.numerator(), 4);
  EXPECT_EQ(meter.denominator(), 4);
}

TEST(TimeSignatureTest, ZeroNumeratorIsInvalid) {
  EXPECT_FALSE(TimeSignature::create(0, 4).has_value());
}

TEST(TimeSignatureTest, NonPowerOfTwoDenominatorIsInvalid) {
  EXPECT_FALSE(TimeSignature::create(4, 3).has_value());
  EXPECT_FALSE(TimeSignature::create(6, 6).has_value());
}

TEST(TimeSignatureTest, DenominatorOneAndOneTwentyEightAreValid) {
  EXPECT_TRUE(TimeSignature::create(1, 1).has_value());
  EXPECT_TRUE(TimeSignature::create(4, 128).has_value());
}

TEST(TimeSignatureTest, DenominatorAboveOneTwentyEightIsInvalid) {
  EXPECT_FALSE(TimeSignature::create(4, 256).has_value());
}

TEST(VoiceTest, MinIsValid) {
  EXPECT_TRUE(Voice::create(Voice::kMin).has_value());
}

TEST(VoiceTest, MaxIsValid) {
  EXPECT_TRUE(Voice::create(Voice::kMax).has_value());
}

TEST(VoiceTest, ZeroIsInvalid) {
  EXPECT_FALSE(Voice::create(0).has_value());
}

TEST(VoiceTest, FiveIsInvalid) {
  EXPECT_FALSE(Voice::create(5).has_value());
}

TEST(VoiceTest, DefaultIsVoiceOne) {
  EXPECT_EQ(Voice().index(), 1);
}

TEST(TempoTest, MinBpmIsValid) {
  EXPECT_TRUE(Tempo::create(Rational(10), NoteValue::kQuarter).has_value());
}

TEST(TempoTest, MaxBpmIsValid) {
  EXPECT_TRUE(Tempo::create(Rational(400), NoteValue::kQuarter).has_value());
}

TEST(TempoTest, JustBelowMinBpmIsInvalid) {
  EXPECT_FALSE(Tempo::create(Rational(9), NoteValue::kQuarter).has_value());
}

TEST(TempoTest, JustAboveMaxBpmIsInvalid) {
  EXPECT_FALSE(Tempo::create(Rational(401), NoteValue::kQuarter).has_value());
}

TEST(TempoTest, FractionalBpmIsExact) {
  const Rational half_bpm = Rational::create(241, 2).value();
  const auto     tempo    = Tempo::create(half_bpm, NoteValue::kEighth);
  ASSERT_TRUE(tempo.has_value());
  EXPECT_EQ(tempo->bpm(), half_bpm);
  EXPECT_EQ(tempo->beat_unit(), NoteValue::kEighth);
}

TEST(TempoTest, DefaultIsOneTwentyBpmQuarterNote) {
  const Tempo tempo;
  EXPECT_EQ(tempo.bpm(), Rational(120));
  EXPECT_EQ(tempo.beat_unit(), NoteValue::kQuarter);
}
