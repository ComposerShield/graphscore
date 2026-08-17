// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Articulation;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_duration;
using graphscore::event_id;
using graphscore::event_note_on_velocity;
using graphscore::event_sounded_duration;
using graphscore::grace_group_preceding_available_duration;
using graphscore::grace_group_remaining_preceding_duration;
using graphscore::grace_group_steal_durations;
using graphscore::grace_steal_durations;
using graphscore::grace_steal_remaining_duration;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::HairpinDirection;
using graphscore::HairpinVelocityContext;
using graphscore::interpolate_hairpin_velocity;
using graphscore::kDefaultSoundedDurationRatio;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::MidiVelocity;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteOnVelocityContext;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::sounded_duration_for_articulation;
using graphscore::SpelledPitch;
using graphscore::velocity_for_dynamic;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

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

MidiVelocity velocity(std::uint8_t value) {
  return *MidiVelocity::create(value);
}

}  // namespace

// -- event_sounded_duration ---------------------------------------------------

TEST(NotationPlaybackTest, PlainNoteAppliesDefaultDetacheRatio) {
  const VoiceEvent event = make_note(pitch(Letter::kC), quarter());
  const Rational   result =
      event_sounded_duration(event, /*is_tied=*/false, std::nullopt);
  EXPECT_EQ(result, quarter().resolved() * kDefaultSoundedDurationRatio);
}

TEST(NotationPlaybackTest, StaccatoNoteShortensToHalf) {
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), false, {Articulation::kStaccato});
  const Rational result =
      event_sounded_duration(event, /*is_tied=*/false, std::nullopt);
  EXPECT_EQ(result, quarter().resolved() * Rational(1) / Rational(2));
}

TEST(NotationPlaybackTest, StaccatoNoteTiedSuppressesShortening) {
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), true, {Articulation::kStaccato});
  const Rational result =
      event_sounded_duration(event, /*is_tied=*/true, std::nullopt);
  EXPECT_EQ(result, quarter().resolved());
}

TEST(NotationPlaybackTest, ExplicitDurationArticulationOverridesSlur) {
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), false, {Articulation::kStaccato});
  const Rational gap = *Rational::create(1, 8);
  const Rational result =
      event_sounded_duration(event, /*is_tied=*/false, std::make_optional(gap));
  EXPECT_EQ(result, quarter().resolved() * Rational(1) / Rational(2));
}

TEST(NotationPlaybackTest, PresentZeroSlurGapUsesRawDuration) {
  const VoiceEvent event = make_note(pitch(Letter::kC), quarter());
  const Rational   result =
      event_sounded_duration(event, /*is_tied=*/false, Rational(0));
  EXPECT_EQ(result, quarter().resolved());
}

TEST(NotationPlaybackTest, EveryDurationArticulationOverridesSlur) {
  const Rational gap = *Rational::create(1, 8);
  const std::vector<std::pair<Articulation, Rational>> cases = {
      {Articulation::kStaccatissimo, Rational(1) / Rational(4)},
      {Articulation::kTenuto, Rational(1)},
  };

  for (const auto& [articulation, ratio] : cases) {
    const VoiceEvent event =
        make_note(pitch(Letter::kC), quarter(), false, {articulation});
    EXPECT_EQ(event_sounded_duration(event, /*is_tied=*/false,
                                     std::make_optional(gap)),
              quarter().resolved() * ratio);
  }
}

TEST(NotationPlaybackTest,
     ExplicitDurationArticulationPreservesTieSuppressionOverSlur) {
  // A duration articulation is more specific than a slur. Once it overrides
  // the slur, the ordinary tie rule still suppresses its shortening.
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true,
                {Articulation::kStaccato});
  const Rational gap = *Rational::create(1, 8);
  const Rational result =
      event_sounded_duration(event, /*is_tied=*/true, std::make_optional(gap));
  EXPECT_EQ(result, quarter().resolved());
  EXPECT_NE(result, quarter().resolved() * Rational(1) / Rational(2));
}

TEST(NotationPlaybackTest, SlurRemainsActiveForVelocityOnlyArticulations) {
  const Rational   gap = *Rational::create(1, 8);
  const VoiceEvent accent =
      make_note(pitch(Letter::kC), quarter(), false, {Articulation::kAccent});
  const VoiceEvent marcato =
      make_note(pitch(Letter::kD), quarter(), false, {Articulation::kMarcato});

  EXPECT_EQ(event_sounded_duration(accent, /*is_tied=*/false,
                                   std::make_optional(gap)),
            quarter().resolved() + gap);
  EXPECT_EQ(event_sounded_duration(marcato, /*is_tied=*/false,
                                   std::make_optional(gap)),
            quarter().resolved() + gap);
}

TEST(NotationPlaybackTest, ChordUsesItsOwnArticulationSet) {
  const VoiceEvent chord = make_chord(eighth(),
                                      {ChordNote{.pitch = pitch(Letter::kC)},
                                       ChordNote{.pitch = pitch(Letter::kE)}},
                                      {Articulation::kTenuto});
  const Rational   result =
      event_sounded_duration(chord, /*is_tied=*/false, std::nullopt);
  EXPECT_EQ(result, eighth().resolved());
}

// -- event_note_on_velocity ---------------------------------------------------

TEST(NotationPlaybackTest, VelocityWithNoHairpinUsesGoverningDynamic) {
  const VoiceEvent   event = make_note(pitch(Letter::kC), quarter());
  const MidiVelocity result =
      event_note_on_velocity(event, Dynamic::kF, std::nullopt);
  EXPECT_EQ(result, velocity_for_dynamic(Dynamic::kF));
}

TEST(NotationPlaybackTest, VelocityArticulationsApplyDocumentedBoosts) {
  const VoiceEvent accent =
      make_note(pitch(Letter::kC), quarter(), false, {Articulation::kAccent});
  const VoiceEvent marcato =
      make_note(pitch(Letter::kD), quarter(), false, {Articulation::kMarcato});
  const VoiceEvent combined =
      make_note(pitch(Letter::kC), quarter(), false,
                {Articulation::kAccent, Articulation::kMarcato});
  const MidiVelocity base = velocity_for_dynamic(Dynamic::kMf);

  EXPECT_EQ(event_note_on_velocity(accent, Dynamic::kMf, std::nullopt).value(),
            base.value() + 16u);
  EXPECT_EQ(event_note_on_velocity(marcato, Dynamic::kMf, std::nullopt).value(),
            base.value() + 24u);
  EXPECT_EQ(
      event_note_on_velocity(combined, Dynamic::kMf, std::nullopt).value(),
      base.value() + 24u);
}

TEST(NotationPlaybackTest, VelocityWithHairpinContextIgnoresGoverningDynamic) {
  const VoiceEvent             event = make_note(pitch(Letter::kC), quarter());
  const HairpinVelocityContext hairpin{velocity(20), velocity(100),
                                       *Rational::create(1, 4)};
  const MidiVelocity           result =
      event_note_on_velocity(event, Dynamic::kPpp, std::make_optional(hairpin));
  EXPECT_EQ(result, interpolate_hairpin_velocity(hairpin.from, hairpin.to,
                                                 hairpin.position));
}

TEST(NotationPlaybackTest,
     VelocityResolutionUsesProjectDefaultAndPointDynamics) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent second = make_note(pitch(Letter::kD), quarter());
  const VoiceEvent third  = make_note(pitch(Letter::kE), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(second).ok());
  ASSERT_TRUE(voice.append(third).ok());
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(second), Dynamic::kF))
          .ok());

  ASSERT_EQ(event_note_on_velocity(voice, event_id(first), Dynamic::kP),
            velocity_for_dynamic(Dynamic::kP));
  ASSERT_EQ(event_note_on_velocity(voice, event_id(second), Dynamic::kP),
            velocity_for_dynamic(Dynamic::kF));
  ASSERT_EQ(event_note_on_velocity(voice, event_id(third), Dynamic::kP),
            velocity_for_dynamic(Dynamic::kF));
}

TEST(NotationPlaybackTest, ProjectOverloadUsesCurrentEditableDefault) {
  Project          project(ProjectId::generate());
  VoiceContent     voice;
  const VoiceEvent event = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(event).ok());

  project.set_default_dynamic(Dynamic::kFf);
  EXPECT_EQ(event_note_on_velocity(project, voice, event_id(event)),
            velocity_for_dynamic(Dynamic::kFf));
}

TEST(NotationPlaybackTest, VelocityResolutionInterpolatesHairpinByMusicalTime) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent middle = make_note(pitch(Letter::kD), eighth());
  const VoiceEvent last   = make_note(pitch(Letter::kE), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(middle).ok());
  ASSERT_TRUE(voice.append(last).ok());
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(first), Dynamic::kP))
          .ok());
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(last), Dynamic::kF))
          .ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(first), event_id(last),
                                            HairpinDirection::kCrescendo))
                  .ok());

  const std::optional<NoteOnVelocityContext> context =
      resolve_note_on_velocity_context(voice, event_id(middle), Dynamic::kMf);
  ASSERT_TRUE(context.has_value());
  ASSERT_TRUE(context->hairpin.has_value());
  EXPECT_EQ(context->hairpin->position, *Rational::create(2, 3));
  EXPECT_EQ(event_note_on_velocity(voice, event_id(middle), Dynamic::kMf),
            interpolate_hairpin_velocity(velocity_for_dynamic(Dynamic::kP),
                                         velocity_for_dynamic(Dynamic::kF),
                                         *Rational::create(2, 3)));
}

TEST(NotationPlaybackTest, HairpinWithoutTargetDynamicUsesDirectionalEndpoint) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent middle = make_note(pitch(Letter::kD), quarter());
  const VoiceEvent last   = make_note(pitch(Letter::kE), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(middle).ok());
  ASSERT_TRUE(voice.append(last).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(first), event_id(last),
                                            HairpinDirection::kDiminuendo))
                  .ok());

  const std::optional<NoteOnVelocityContext> context =
      resolve_note_on_velocity_context(voice, event_id(last), Dynamic::kMf);
  ASSERT_TRUE(context.has_value());
  ASSERT_TRUE(context->hairpin.has_value());
  EXPECT_EQ(context->hairpin->from, velocity_for_dynamic(Dynamic::kMf));
  EXPECT_EQ(context->hairpin->to, velocity_for_dynamic(Dynamic::kPpp));
  EXPECT_EQ(context->hairpin->position, Rational(1));
}

TEST(NotationPlaybackTest,
     HairpinInteriorDynamicDoesNotReplaceDirectionalEndpoint) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent middle = make_note(pitch(Letter::kD), quarter());
  const VoiceEvent last   = make_note(pitch(Letter::kE), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(middle).ok());
  ASSERT_TRUE(voice.append(last).ok());
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(first), Dynamic::kP))
          .ok());
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(event_id(middle), Dynamic::kF))
          .ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(first), event_id(last),
                                            HairpinDirection::kCrescendo))
                  .ok());

  const std::optional<NoteOnVelocityContext> context =
      resolve_note_on_velocity_context(voice, event_id(last), Dynamic::kMf);
  ASSERT_TRUE(context.has_value());
  ASSERT_TRUE(context->hairpin.has_value());
  EXPECT_EQ(context->hairpin->to, velocity_for_dynamic(Dynamic::kFff));
}

TEST(NotationPlaybackTest, InvalidHairpinDoesNotHideLaterValidHairpin) {
  VoiceContent     voice;
  const VoiceEvent first  = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent middle = make_note(pitch(Letter::kD), quarter());
  const VoiceEvent last   = make_note(pitch(Letter::kE), quarter());
  ASSERT_TRUE(voice.append(first).ok());
  ASSERT_TRUE(voice.append(middle).ok());
  ASSERT_TRUE(voice.append(last).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(first), event_id(first),
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(event_id(first), event_id(last),
                                            HairpinDirection::kCrescendo))
                  .ok());

  const std::optional<NoteOnVelocityContext> context =
      resolve_note_on_velocity_context(voice, event_id(middle), Dynamic::kMf);
  ASSERT_TRUE(context.has_value());
  ASSERT_TRUE(context->hairpin.has_value());
  EXPECT_EQ(context->hairpin->position, *Rational::create(1, 2));
}

TEST(NotationPlaybackTest, VelocityResolutionRejectsNonEventIds) {
  VoiceContent     voice;
  const VoiceEvent event = make_note(pitch(Letter::kC), quarter());
  ASSERT_TRUE(voice.append(event).ok());
  const NotationEntityId missing = NotationEntityId::generate();
  EXPECT_FALSE(resolve_note_on_velocity_context(voice, missing, Dynamic::kMf)
                   .has_value());
  EXPECT_FALSE(
      event_note_on_velocity(voice, missing, Dynamic::kMf).has_value());
}

// -- grace_group_steal_durations / grace_group_remaining_preceding_duration --

TEST(NotationPlaybackTest, GraceGroupStealMatchesCoreForUniformType) {
  const VoiceEvent       principal = make_note(pitch(Letter::kC), quarter());
  const GraceNoteType    kind      = GraceNoteType::kAcciaccatura;
  std::vector<GraceNote> notes     = {
      GraceNote{.pitch = pitch(Letter::kB), .duration = eighth(), .type = kind},
      GraceNote{.pitch = pitch(Letter::kA), .duration = eighth(), .type = kind},
      GraceNote{.pitch = pitch(Letter::kG), .duration = eighth(), .type = kind},
  };
  const GraceGroup group =
      make_grace_group(event_id(principal), std::move(notes));

  const Rational              available = Rational(1);
  const std::vector<Rational> result =
      grace_group_steal_durations(group, available);
  const std::vector<Rational> expected =
      grace_steal_durations(GraceNoteType::kAcciaccatura, 3, available);
  EXPECT_EQ(result, expected);

  EXPECT_EQ(grace_group_remaining_preceding_duration(group, available),
            grace_steal_remaining_duration(GraceNoteType::kAcciaccatura, 3,
                                           available));
}

TEST(NotationPlaybackTest, GraceGroupStealFollowsFirstNoteTypeWhenMixed) {
  const VoiceEvent principal = make_note(pitch(Letter::kC), quarter());
  const GraceNote  first{.pitch    = pitch(Letter::kB),
                         .duration = eighth(),
                         .type     = GraceNoteType::kAppoggiatura};
  const GraceNote  second{.pitch    = pitch(Letter::kA),
                          .duration = eighth(),
                          .type     = GraceNoteType::kAcciaccatura};
  const GraceGroup group =
      make_grace_group(event_id(principal), {first, second});

  const Rational              available = Rational(1);
  const std::vector<Rational> result =
      grace_group_steal_durations(group, available);
  const std::vector<Rational> expected =
      grace_steal_durations(GraceNoteType::kAppoggiatura, 2, available);
  EXPECT_EQ(result, expected);
}

TEST(NotationPlaybackTest, GraceGroupWithNoNotesStealsNothing) {
  const VoiceEvent principal = make_note(pitch(Letter::kC), quarter());
  const GraceGroup group     = make_grace_group(event_id(principal), {});

  const Rational available = Rational(1);
  EXPECT_TRUE(grace_group_steal_durations(group, available).empty());
  EXPECT_EQ(grace_group_remaining_preceding_duration(group, available),
            available);
}

TEST(NotationPlaybackTest, GraceGroupWithNoPrecedingNoteFallsBack) {
  const VoiceEvent principal = make_note(pitch(Letter::kC), quarter());
  const GraceNote  gn0{.pitch    = pitch(Letter::kB),
                       .duration = eighth(),
                       .type     = GraceNoteType::kAppoggiatura};
  const GraceGroup group = make_grace_group(event_id(principal), {gn0});

  const std::vector<Rational> result =
      grace_group_steal_durations(group, Rational(0));
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0], grace_steal_durations(GraceNoteType::kAppoggiatura, 1,
                                             Rational(0))[0]);
}

TEST(NotationPlaybackTest, GraceGroupStealsFromImmediatelyPrecedingNote) {
  VoiceContent     voice;
  const VoiceEvent preceding = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent principal = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(preceding).ok());
  ASSERT_TRUE(voice.append(principal).ok());

  const GraceGroup group = make_grace_group(
      event_id(principal), {GraceNote{.pitch    = pitch(Letter::kC),
                                      .duration = eighth(),
                                      .type = GraceNoteType::kAppoggiatura}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  EXPECT_EQ(grace_group_preceding_available_duration(voice, group),
            quarter().resolved());
  EXPECT_EQ(grace_group_steal_durations(voice, group),
            grace_steal_durations(GraceNoteType::kAppoggiatura, 1,
                                  quarter().resolved()));
  EXPECT_EQ(grace_group_remaining_preceding_duration(voice, group),
            grace_steal_remaining_duration(GraceNoteType::kAppoggiatura, 1,
                                           quarter().resolved()));
  EXPECT_EQ(event_duration(voice.events()[1]).resolved(), quarter().resolved());
}

TEST(NotationPlaybackTest, GraceGroupDoesNotStealFromRest) {
  VoiceContent     voice;
  const VoiceEvent rest      = make_rest(quarter());
  const VoiceEvent principal = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(rest).ok());
  ASSERT_TRUE(voice.append(principal).ok());

  const GraceGroup group = make_grace_group(
      event_id(principal), {GraceNote{.pitch    = pitch(Letter::kC),
                                      .duration = eighth(),
                                      .type = GraceNoteType::kAcciaccatura}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  EXPECT_EQ(grace_group_preceding_available_duration(voice, group),
            Rational(0));
  EXPECT_EQ(
      grace_group_steal_durations(voice, group),
      grace_steal_durations(GraceNoteType::kAcciaccatura, 1, Rational(0)));
  EXPECT_EQ(grace_group_remaining_preceding_duration(voice, group),
            Rational(0));
  EXPECT_EQ(event_duration(voice.events()[0]).resolved(), quarter().resolved());
}

TEST(NotationPlaybackTest, GraceGroupAtVoiceStartUsesFallback) {
  VoiceContent     voice;
  const VoiceEvent principal = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(principal).ok());

  const GraceGroup group = make_grace_group(
      event_id(principal), {GraceNote{.pitch    = pitch(Letter::kC),
                                      .duration = eighth(),
                                      .type = GraceNoteType::kAppoggiatura}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  EXPECT_EQ(
      grace_group_steal_durations(voice, group),
      grace_steal_durations(GraceNoteType::kAppoggiatura, 1, Rational(0)));
  EXPECT_EQ(grace_group_remaining_preceding_duration(voice, group),
            Rational(0));
}

TEST(NotationPlaybackTest, GraceGroupStealsExactTimeFromChordPredecessor) {
  VoiceContent     voice;
  const Duration   dotted_quarter = *Duration::create(NoteValue::kQuarter, 1);
  const VoiceEvent chord =
      make_chord(dotted_quarter, {ChordNote{.pitch = pitch(Letter::kC)},
                                  ChordNote{.pitch = pitch(Letter::kE)}});
  const VoiceEvent principal = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(chord).ok());
  ASSERT_TRUE(voice.append(principal).ok());

  const GraceGroup group = make_grace_group(
      event_id(principal), {GraceNote{.pitch    = pitch(Letter::kC),
                                      .duration = eighth(),
                                      .type     = GraceNoteType::kAppoggiatura},
                            GraceNote{.pitch    = pitch(Letter::kB),
                                      .duration = eighth(),
                                      .type = GraceNoteType::kAppoggiatura}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  const Rational available = dotted_quarter.resolved();
  EXPECT_EQ(grace_group_preceding_available_duration(voice, group), available);
  EXPECT_EQ(grace_group_steal_durations(voice, group),
            grace_steal_durations(GraceNoteType::kAppoggiatura, 2, available));
  EXPECT_EQ(grace_group_remaining_preceding_duration(voice, group),
            grace_steal_remaining_duration(GraceNoteType::kAppoggiatura, 2,
                                           available));
  EXPECT_EQ(event_duration(voice.events()[0]).resolved(), available);
}

TEST(NotationPlaybackTest, GraceGroupWithDanglingPrincipalUsesFallback) {
  VoiceContent     voice;
  const VoiceEvent present = make_note(pitch(Letter::kC), quarter());
  const VoiceEvent missing = make_note(pitch(Letter::kD), quarter());
  ASSERT_TRUE(voice.append(present).ok());

  const GraceGroup group = make_grace_group(
      event_id(missing), {GraceNote{.pitch    = pitch(Letter::kB),
                                    .duration = eighth(),
                                    .type     = GraceNoteType::kAcciaccatura}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());

  EXPECT_EQ(grace_group_preceding_available_duration(voice, group),
            Rational(0));
  EXPECT_EQ(
      grace_group_steal_durations(voice, group),
      grace_steal_durations(GraceNoteType::kAcciaccatura, 1, Rational(0)));
  EXPECT_EQ(grace_group_remaining_preceding_duration(voice, group),
            Rational(0));
  EXPECT_EQ(event_duration(voice.events()[0]).resolved(), quarter().resolved());
}
