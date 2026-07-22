// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::decompose_rest;
using graphscore::Duration;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_note;
using graphscore::make_rest;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::SpelledPitch;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

namespace {

SpelledPitch pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

Duration duration(NoteValue base, std::uint8_t dots = 0) {
  return *Duration::create(base, dots);
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
  const Chord  chord =
      make_chord(duration(NoteValue::kQuarter), {ChordNote{pitch(Letter::kC)}});
  EXPECT_FALSE(voice.append(VoiceEvent(chord)).ok());
  EXPECT_EQ(voice.total_length(), Rational(0));
}

TEST(VoiceContentTest, AppendAcceptsTwoNoteChord) {
  VoiceContent voice;
  const Chord  chord =
      make_chord(duration(NoteValue::kQuarter),
                 {ChordNote{pitch(Letter::kC)}, ChordNote{pitch(Letter::kE)}});
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
