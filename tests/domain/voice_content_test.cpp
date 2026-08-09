// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <ranges>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Articulation;
using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
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
using graphscore::Letter;
using graphscore::make_beam_override;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::RefOpKind;
using graphscore::Rest;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StemDirection;
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
