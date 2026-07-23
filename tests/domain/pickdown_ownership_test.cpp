// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Accidental;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::classify_note_ownership;
using graphscore::classify_voice_ownership;
using graphscore::Duration;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_note;
using graphscore::make_rest;
using graphscore::Measure;
using graphscore::MusicalSpan;
using graphscore::NodeTimeline;
using graphscore::Note;
using graphscore::NoteOwnership;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::SpelledPitch;
using graphscore::tied_note_spans;
using graphscore::TiedNoteSpan;
using graphscore::TimeSignature;
using graphscore::VoiceEvent;

namespace {

SpelledPitch pitch(Letter letter, std::int8_t octave = 4,
                   Accidental accidental = Accidental::kNatural) {
  return *SpelledPitch::create(letter, octave, accidental);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration half() {
  return *Duration::create(NoteValue::kHalf, 0);
}

Measure make_measure(std::uint8_t numerator, std::uint16_t denominator) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 *KeySignature::create(0)};
}

// One whole-note measure of 4/4 (boundary at Rational(1)) with a 1/4
// pickdown (node_end at 5/4).
std::optional<NodeTimeline> make_timeline_with_pickdown() {
  auto timeline = NodeTimeline::create({make_measure(4, 4)}, {});
  if (!timeline.has_value())
    return std::nullopt;
  if (!timeline->set_pickdown(*Rational::create(1, 4)).ok())
    return std::nullopt;
  return timeline;
}

}  // namespace

// -- tied_note_spans -------------------------------------------------------

TEST(TiedNoteSpansTest, RestContributesNoSpan) {
  const std::vector<VoiceEvent> events = {make_rest(quarter())};
  EXPECT_TRUE(tied_note_spans(events, Rational(0)).empty());
}

TEST(TiedNoteSpansTest, UntiedNoteYieldsItsOwnColumnAsItsWholeSpan) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kF), quarter())};
  const auto spans = tied_note_spans(events, Rational(0));
  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].start_event_index, 0u);
  EXPECT_EQ(spans[0].pitch, pitch(Letter::kF));
  EXPECT_EQ(spans[0].span, (MusicalSpan{Rational(0), *Rational::create(1, 4)}));
}

TEST(TiedNoteSpansTest, TieChainExtendsTheSpanThroughEveryLinkedColumn) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), half(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kC), half(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/false),
  };
  const auto spans = tied_note_spans(events, Rational(0));
  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].start_event_index, 0u);
  EXPECT_EQ(spans[0].pitch, pitch(Letter::kC));
  EXPECT_EQ(spans[0].span, (MusicalSpan{Rational(0), *Rational::create(5, 4)}));
}

TEST(TiedNoteSpansTest, ARestBreaksAnActiveTieInsteadOfMergingAcrossIt) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kG), quarter(), /*tied_to_next=*/true),
      make_rest(quarter()),
      make_note(pitch(Letter::kG), quarter(), /*tied_to_next=*/false),
  };
  const auto spans = tied_note_spans(events, Rational(0));
  ASSERT_EQ(spans.size(), 2u);
  EXPECT_EQ(spans[0].span, (MusicalSpan{Rational(0), *Rational::create(1, 4)}));
  EXPECT_EQ(spans[1].start_event_index, 2u);
  EXPECT_EQ(spans[1].span,
            (MusicalSpan{*Rational::create(1, 2), *Rational::create(3, 4)}));
}

TEST(TiedNoteSpansTest, ChordNoteheadsTieIndependently) {
  const std::vector<VoiceEvent> events = {
      make_chord(quarter(), {ChordNote{pitch(Letter::kC), /*tied=*/true},
                             ChordNote{pitch(Letter::kE), /*tied=*/false}}),
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/false),
  };
  const auto spans = tied_note_spans(events, Rational(0));
  ASSERT_EQ(spans.size(), 2u);

  EXPECT_EQ(spans[0].pitch, pitch(Letter::kC));
  EXPECT_EQ(spans[0].span, (MusicalSpan{Rational(0), *Rational::create(1, 2)}));

  EXPECT_EQ(spans[1].pitch, pitch(Letter::kE));
  EXPECT_EQ(spans[1].span, (MusicalSpan{Rational(0), *Rational::create(1, 4)}));
}

TEST(TiedNoteSpansTest, TrailingTieWithNoFollowingColumnEndsTheChainInPlace) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kA), quarter(), /*tied_to_next=*/true)};
  const auto spans = tied_note_spans(events, Rational(0));
  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].start_event_index, 0u);
  EXPECT_EQ(spans[0].pitch, pitch(Letter::kA));
  EXPECT_EQ(spans[0].span, (MusicalSpan{Rational(0), *Rational::create(1, 4)}));
}

TEST(TiedNoteSpansTest,
     ATieToADifferentPitchStartsAFreshChainInsteadOfMerging) {
  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kD), quarter(), /*tied_to_next=*/false),
  };
  const auto spans = tied_note_spans(events, Rational(0));
  ASSERT_EQ(spans.size(), 2u);

  EXPECT_EQ(spans[0].pitch, pitch(Letter::kC));
  EXPECT_EQ(spans[0].span, (MusicalSpan{Rational(0), *Rational::create(1, 4)}));

  EXPECT_EQ(spans[1].start_event_index, 1u);
  EXPECT_EQ(spans[1].pitch, pitch(Letter::kD));
  EXPECT_EQ(spans[1].span,
            (MusicalSpan{*Rational::create(1, 4), *Rational::create(1, 2)}));
}

TEST(TiedNoteSpansTest, AnEmptyVoiceYieldsNoSpans) {
  const std::vector<VoiceEvent> events;
  EXPECT_TRUE(tied_note_spans(events, Rational(0)).empty());
}

// -- classify_note_ownership / classify_voice_ownership ---------------------

TEST(PickdownOwnershipTest, NoteEntirelyInMainRegionHasNoTailOwnership) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());

  const TiedNoteSpan  note{0, pitch(Letter::kF),
                          MusicalSpan{Rational(0), *Rational::create(1, 4)}};
  const NoteOwnership ownership = classify_note_ownership(*timeline, note);

  ASSERT_TRUE(ownership.main_part.has_value());
  EXPECT_EQ(*ownership.main_part, note.span);
  EXPECT_FALSE(ownership.pickdown_tail_part.has_value());
  EXPECT_FALSE(ownership.is_pickdown_tail_material());
}

TEST(PickdownOwnershipTest,
     TieChainCrossingTheBoundaryTransfersPostBoundaryOwnershipToTheTail) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());

  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kC), half(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kC), half(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/false),
  };
  const auto spans = tied_note_spans(events, Rational(0));
  ASSERT_EQ(spans.size(), 1u);

  const NoteOwnership ownership = classify_note_ownership(*timeline, spans[0]);
  ASSERT_TRUE(ownership.main_part.has_value());
  EXPECT_EQ(*ownership.main_part, (MusicalSpan{Rational(0), Rational(1)}));
  ASSERT_TRUE(ownership.pickdown_tail_part.has_value());
  EXPECT_EQ(*ownership.pickdown_tail_part,
            (MusicalSpan{Rational(1), *Rational::create(5, 4)}));
  EXPECT_TRUE(ownership.is_pickdown_tail_material());
}

TEST(PickdownOwnershipTest, ANewAttackStartingInsideThePickdownIsAllTail) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());

  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kE), quarter(), /*tied_to_next=*/false)};
  const auto spans = tied_note_spans(events, Rational(1));
  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].span, (MusicalSpan{Rational(1), *Rational::create(5, 4)}));

  const NoteOwnership ownership = classify_note_ownership(*timeline, spans[0]);
  EXPECT_FALSE(ownership.main_part.has_value());
  ASSERT_TRUE(ownership.pickdown_tail_part.has_value());
  EXPECT_EQ(*ownership.pickdown_tail_part, spans[0].span);
  EXPECT_TRUE(ownership.is_pickdown_tail_material());
}

TEST(PickdownOwnershipTest, ClassifyVoiceOwnershipComposesBothSteps) {
  auto timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());

  const std::vector<VoiceEvent> events = {
      make_note(pitch(Letter::kF), quarter(), /*tied_to_next=*/false),
      make_note(pitch(Letter::kC), half(), /*tied_to_next=*/true),
      make_note(pitch(Letter::kC), half(), /*tied_to_next=*/false),
  };
  const auto owned = classify_voice_ownership(*timeline, events, Rational(0));

  ASSERT_EQ(owned.size(), 2u);
  EXPECT_FALSE(owned[0].is_pickdown_tail_material());
  EXPECT_TRUE(owned[1].is_pickdown_tail_material());
  ASSERT_TRUE(owned[1].main_part.has_value());
  ASSERT_TRUE(owned[1].pickdown_tail_part.has_value());
}
