// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::apply_emphasis;
using graphscore::Articulation;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_id;
using graphscore::event_note_on_velocity;
using graphscore::event_sounded_duration;
using graphscore::grace_group_remaining_preceding_duration;
using graphscore::grace_group_steal_durations;
using graphscore::grace_steal_durations;
using graphscore::grace_steal_remaining_duration;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::HairpinVelocityContext;
using graphscore::interpolate_hairpin_velocity;
using graphscore::kDefaultSoundedDurationRatio;
using graphscore::kStaccatoSoundedDurationRatio;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_grace_group;
using graphscore::make_note;
using graphscore::MidiVelocity;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::Rational;
using graphscore::sounded_duration_for_articulation;
using graphscore::SpelledPitch;
using graphscore::velocity_for_dynamic;
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
  EXPECT_EQ(result, quarter().resolved() * kStaccatoSoundedDurationRatio);
}

TEST(NotationPlaybackTest, StaccatoNoteTiedSuppressesShortening) {
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), true, {Articulation::kStaccato});
  const Rational result =
      event_sounded_duration(event, /*is_tied=*/true, std::nullopt);
  EXPECT_EQ(result, quarter().resolved());
}

TEST(NotationPlaybackTest, SlurredNoteOverridesArticulationShortening) {
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), false, {Articulation::kStaccato});
  const Rational gap = *Rational::create(1, 8);
  const Rational result =
      event_sounded_duration(event, /*is_tied=*/false, std::make_optional(gap));
  EXPECT_EQ(result, quarter().resolved() + gap);
}

TEST(NotationPlaybackTest,
     SlurredTiedStaccatoNoteIgnoresIsTiedAndArticulationAlike) {
  // Documented precedence (playback_mapping.hpp's overview, "Legato (slur)
  // overlap and its precedence over shortening"): a slur wins outright over
  // BOTH duration-articulation shortening AND tie-boundary suppression.
  // is_tied=true and Articulation::kStaccato are both silently ignored when
  // a slur governs the outgoing edge; the result is the plain legato
  // overlap of the raw notated duration.
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), /*tied_to_next=*/true,
                {Articulation::kStaccato});
  const Rational gap = *Rational::create(1, 8);
  const Rational result =
      event_sounded_duration(event, /*is_tied=*/true, std::make_optional(gap));
  EXPECT_EQ(result, quarter().resolved() + gap);

  // Confirm this is genuinely different from what either is_tied or
  // staccato alone would have produced, so the test would fail if
  // precedence regressed to tie-wins or staccato-wins.
  EXPECT_NE(result, quarter().resolved());
  EXPECT_NE(result, quarter().resolved() * kStaccatoSoundedDurationRatio);
}

TEST(NotationPlaybackTest, ChordUsesItsOwnArticulationSet) {
  const VoiceEvent chord = make_chord(
      eighth(), {ChordNote{pitch(Letter::kC)}, ChordNote{pitch(Letter::kE)}},
      {Articulation::kTenuto});
  const Rational result =
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

TEST(NotationPlaybackTest, VelocityWithAccentAppliesEmphasis) {
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), false, {Articulation::kAccent});
  const MidiVelocity result =
      event_note_on_velocity(event, Dynamic::kF, std::nullopt);
  EXPECT_EQ(result,
            apply_emphasis(velocity_for_dynamic(Dynamic::kF), true, false));
}

TEST(NotationPlaybackTest, VelocityWithMarcatoOutranksAccent) {
  const VoiceEvent event =
      make_note(pitch(Letter::kC), quarter(), false,
                {Articulation::kAccent, Articulation::kMarcato});
  const MidiVelocity result =
      event_note_on_velocity(event, Dynamic::kMf, std::nullopt);
  EXPECT_EQ(result,
            apply_emphasis(velocity_for_dynamic(Dynamic::kMf), true, true));
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

// -- grace_group_steal_durations / grace_group_remaining_preceding_duration --

TEST(NotationPlaybackTest, GraceGroupStealMatchesCoreForUniformType) {
  const VoiceEvent       principal = make_note(pitch(Letter::kC), quarter());
  const GraceNoteType    kind      = GraceNoteType::kAcciaccatura;
  std::vector<GraceNote> notes     = {
      GraceNote{pitch(Letter::kB), eighth(), kind},
      GraceNote{pitch(Letter::kA), eighth(), kind},
      GraceNote{pitch(Letter::kG), eighth(), kind},
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
  const GraceNote  first{pitch(Letter::kB), eighth(),
                        GraceNoteType::kAppoggiatura};
  const GraceNote  second{pitch(Letter::kA), eighth(),
                         GraceNoteType::kAcciaccatura};
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
  const GraceGroup group     = make_grace_group(
      event_id(principal),
      {GraceNote{pitch(Letter::kB), eighth(), GraceNoteType::kAppoggiatura}});

  const std::vector<Rational> result =
      grace_group_steal_durations(group, Rational(0));
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0], grace_steal_durations(GraceNoteType::kAppoggiatura, 1,
                                             Rational(0))[0]);
}
