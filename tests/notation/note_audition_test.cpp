// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using graphscore::Accidental;
using graphscore::audition_for_note_entry;
using graphscore::Chord;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_note;
using graphscore::make_note_entry_command;
using graphscore::make_rest;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::MidiPitch;
using graphscore::MidiVelocity;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteAuditionRequest;
using graphscore::NotePaletteEntryKind;
using graphscore::NotePaletteEntrySpec;
using graphscore::NotePaletteState;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::velocity_for_dynamic;
using graphscore::Voice;
using graphscore::VoiceContent;

[[nodiscard]] Measure measure(std::uint8_t  numerator   = 4,
                              std::uint16_t denominator = 4) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 graphscore::KeySignature{}};
}

struct Fixture {
  Project project{ProjectId::generate(), "Audition"};
  NodeId  node_id;
  TrackId track_id;

  explicit Fixture(std::vector<Measure> measures = {measure(), measure()}) {
    const auto added = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
    EXPECT_TRUE(added.has_value());
    track_id   = *added;
    node_id    = project.add_node("Node");
    auto* lane = project.find_node(node_id)->lane(track_id);
    lane->ensure_stave(stave_id());
    auto timeline = NodeTimeline::create(
        std::move(measures), {project.active_tracks()[0].layout().staves()[0]});
    EXPECT_TRUE(timeline.has_value());
    project.find_node(node_id)->set_timeline(std::move(*timeline));
  }

  [[nodiscard]] StaveId stave_id() const {
    return project.active_tracks()[0].layout().staves()[0].id;
  }

  [[nodiscard]] TrackId track() const { return track_id; }

  [[nodiscard]] VoiceContent& voice(std::uint8_t voice_index = 1) {
    return project.find_node(node_id)
        ->lane(track_id)
        ->stave(stave_id())
        ->voice(*Voice::create(voice_index));
  }

  void normalize_voice(std::uint8_t voice_index = 1) {
    const Rational end = node_end();
    EXPECT_TRUE(voice(voice_index).normalize(end).ok());
  }

  [[nodiscard]] Rational node_end() const {
    return project.find_node(node_id)->timeline()->node_end();
  }

  // The audition for a click on this fixture, with make_note_entry_command's
  // own parameter list.
  [[nodiscard]] std::optional<NoteAuditionRequest> audition(
      Rational position, const NotePaletteEntrySpec& armed,
      std::optional<SpelledPitch> candidate_pitch) {
    return audition_for_note_entry(project, node_id, track(), stave_id(),
                                   position, armed, candidate_pitch);
  }

  // The command for the same click, so a test can assert both stay in
  // lockstep.
  [[nodiscard]] bool command_exists(
      Rational position, const NotePaletteEntrySpec& armed,
      std::optional<SpelledPitch> candidate_pitch) const {
    return make_note_entry_command(project, node_id, track_id, stave_id(),
                                   position, armed, candidate_pitch) != nullptr;
  }
};

Rest append_whole_rest(Fixture& fixture, std::uint8_t voice_index = 1) {
  Rest rest = make_rest(*Duration::create(NoteValue::kWhole, 0));
  EXPECT_TRUE(fixture.voice(voice_index).append(rest).ok());
  return rest;
}

Note append_quarter_note(Fixture& fixture, const SpelledPitch& pitch,
                         std::uint8_t voice_index = 1) {
  Note note = make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  EXPECT_TRUE(fixture.voice(voice_index).append(note).ok());
  return note;
}

[[nodiscard]] NotePaletteEntrySpec armed(
    NoteValue            note_value  = NoteValue::kQuarter,
    NotePaletteEntryKind entry_kind  = NotePaletteEntryKind::kNote,
    std::uint8_t         voice_index = 1) {
  const NotePaletteState state = *NotePaletteState::create(
      note_value, 0, entry_kind, *Voice::create(voice_index));
  return state.next_entry_spec();
}

[[nodiscard]] std::vector<std::uint8_t> pitch_values(
    const NoteAuditionRequest& request) {
  std::vector<std::uint8_t> values;
  values.reserve(request.pitches.size());
  for (const MidiPitch& pitch : request.pitches)
    values.push_back(pitch.value());
  return values;
}

// ---- Branches that newly sound something ----

TEST(NoteAuditionTest, FirstNoteIntoAnEmptyArmedVoiceAuditionsTheOnePitch) {
  Fixture fixture;
  ASSERT_TRUE(fixture.voice(2).events().empty());
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);

  const std::optional<NoteAuditionRequest> request =
      fixture.audition(Rational(0), spec, pitch);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->track_id, fixture.track());
  EXPECT_EQ(pitch_values(*request), (std::vector<std::uint8_t>{60}));
  EXPECT_EQ(request->velocity,
            velocity_for_dynamic(fixture.project.default_dynamic()));
}

TEST(NoteAuditionTest, RestReplacedByNoteAuditionsTheOnePitch) {
  Fixture fixture;
  append_whole_rest(fixture);
  fixture.normalize_voice();
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kD, 5);

  const std::optional<NoteAuditionRequest> request = fixture.audition(
      Rational(0), armed(NoteValue::kEighth, NotePaletteEntryKind::kNote),
      pitch);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(pitch_values(*request), (std::vector<std::uint8_t>{74}));
}

// Adding a pitch to a note auditions the whole resulting harmony, not only
// the pitch the composer clicked.
TEST(NoteAuditionTest, NoteToChordAuditionsBothPitchesAscending) {
  Fixture            fixture;
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, e);
  fixture.normalize_voice();

  // Click the LOWER pitch so the ascending-order requirement is not
  // satisfied by insertion order alone.
  const std::optional<NoteAuditionRequest> request =
      fixture.audition(Rational(0), armed(), c);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(pitch_values(*request), (std::vector<std::uint8_t>{60, 64}));
}

TEST(NoteAuditionTest, ChordExtensionAuditionsEveryResultingPitchAscending) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  append_whole_rest(fixture);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_chord(*Duration::create(NoteValue::kQuarter, 0),
                             {{NotationEntityId::generate(), c, false},
                              {NotationEntityId::generate(), g, false}}))
          .ok());
  fixture.normalize_voice();

  const std::optional<NoteAuditionRequest> request =
      fixture.audition(Rational(1), armed(), e);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(pitch_values(*request), (std::vector<std::uint8_t>{60, 64, 67}));
}

// ---- Branches that newly sound nothing ----

TEST(NoteAuditionTest, SamePitchDurationOnlyReplacementIsSilent) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);

  EXPECT_TRUE(fixture.command_exists(Rational(0), spec, pitch));
  EXPECT_FALSE(fixture.audition(Rational(0), spec, pitch).has_value());
}

TEST(NoteAuditionTest, DuplicatePitchDurationOnlyReplacementOnAChordIsSilent) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  append_whole_rest(fixture);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_chord(*Duration::create(NoteValue::kQuarter, 0),
                             {{NotationEntityId::generate(), c, false},
                              {NotationEntityId::generate(), e, false}}))
          .ok());
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);

  EXPECT_TRUE(fixture.command_exists(Rational(1), spec, e));
  EXPECT_FALSE(fixture.audition(Rational(1), spec, e).has_value());
}

TEST(NoteAuditionTest, RestEntryNeverAuditions) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kWhole, NotePaletteEntryKind::kRest);

  // Note → Rest builds a command and sounds nothing.
  EXPECT_TRUE(fixture.command_exists(Rational(0), spec, std::nullopt));
  EXPECT_FALSE(fixture.audition(Rational(0), spec, std::nullopt).has_value());

  // A rest click carrying a stray candidate pitch is still silent.
  EXPECT_FALSE(fixture.audition(Rational(0), spec, pitch).has_value());

  // Rest → Rest (duration-only on an existing rest).
  Fixture rest_fixture;
  append_whole_rest(rest_fixture);
  rest_fixture.normalize_voice();
  const NotePaletteEntrySpec half_rest =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kRest);
  EXPECT_TRUE(
      rest_fixture.command_exists(Rational(0), half_rest, std::nullopt));
  EXPECT_FALSE(
      rest_fixture.audition(Rational(0), half_rest, std::nullopt).has_value());

  // A rest into an entirely empty voice materializes the stream and still
  // sounds nothing.
  const NotePaletteEntrySpec empty_voice_spec =
      armed(NoteValue::kWhole, NotePaletteEntryKind::kRest, 2);
  ASSERT_TRUE(fixture.voice(2).events().empty());
  EXPECT_TRUE(
      fixture.command_exists(Rational(0), empty_voice_spec, std::nullopt));
  EXPECT_FALSE(fixture.audition(Rational(0), empty_voice_spec, std::nullopt)
                   .has_value());
}

// Every input make_note_entry_command rejects must also produce no
// audition; asserted together so the two can never drift apart.
TEST(NoteAuditionTest, RejectedEntriesProduceNoCommandAndNoAudition) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d = *SpelledPitch::create(Letter::kD, 4);
  append_quarter_note(fixture, c);  // deliberately NOT normalized

  // No event starts at Rational(1) in the (non-empty) armed voice.
  EXPECT_FALSE(fixture.command_exists(Rational(1), armed(), d));
  EXPECT_FALSE(fixture.audition(Rational(1), armed(), d).has_value());

  // kNote armed with no candidate pitch.
  EXPECT_FALSE(fixture.command_exists(Rational(0), armed(), std::nullopt));
  EXPECT_FALSE(
      fixture.audition(Rational(0), armed(), std::nullopt).has_value());

  // A node the project does not own.
  EXPECT_EQ(make_note_entry_command(fixture.project, NodeId::generate(),
                                    fixture.track(), fixture.stave_id(),
                                    Rational(0), armed(), d),
            nullptr);
  EXPECT_FALSE(audition_for_note_entry(fixture.project, NodeId::generate(),
                                       fixture.track(), fixture.stave_id(),
                                       Rational(0), armed(), d)
                   .has_value());

  // A position that is not an onset of an empty voice's hypothetical fill.
  const NotePaletteEntrySpec empty_voice_spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  const Rational off_onset = Rational(1) / Rational(3);
  EXPECT_FALSE(fixture.command_exists(off_onset, empty_voice_spec, d));
  EXPECT_FALSE(fixture.audition(off_onset, empty_voice_spec, d).has_value());
}

// ---- Enharmonic collapse ----

// The duplicate-pitch branch keys on SpelledPitch equality, so C-sharp 4
// added to a chord already holding D-flat 4 is a real chord extension, not
// a duration-only click; the audition's own MidiPitch deduplication is what
// keeps the shared sounding pitch from being played twice.
TEST(NoteAuditionTest, EnharmonicSpellingsCollapseToOneSoundingPitch) {
  Fixture            fixture;
  const SpelledPitch c_natural = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d_flat =
      *SpelledPitch::create(Letter::kD, 4, Accidental::kFlat);
  const SpelledPitch c_sharp =
      *SpelledPitch::create(Letter::kC, 4, Accidental::kSharp);
  ASSERT_NE(d_flat, c_sharp);
  ASSERT_EQ(d_flat.to_midi_pitch(), c_sharp.to_midi_pitch());

  append_whole_rest(fixture);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_chord(*Duration::create(NoteValue::kQuarter, 0),
                             {{NotationEntityId::generate(), c_natural, false},
                              {NotationEntityId::generate(), d_flat, false}}))
          .ok());
  fixture.normalize_voice();

  // The duplicate check does not fire: this really is a chord extension.
  EXPECT_TRUE(fixture.command_exists(Rational(1), armed(), c_sharp));

  const std::optional<NoteAuditionRequest> request =
      fixture.audition(Rational(1), armed(), c_sharp);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(pitch_values(*request), (std::vector<std::uint8_t>{60, 61}));
}

// ---- Velocity ----

TEST(NoteAuditionTest, VelocityTracksTheProjectDefaultDynamic) {
  Fixture fixture;
  append_whole_rest(fixture);
  fixture.normalize_voice();
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kA, 4);

  const std::optional<NoteAuditionRequest> at_default =
      fixture.audition(Rational(0), armed(), pitch);
  ASSERT_TRUE(at_default.has_value());
  EXPECT_EQ(at_default->velocity, velocity_for_dynamic(Dynamic::kMf));

  fixture.project.set_default_dynamic(Dynamic::kFf);
  const std::optional<NoteAuditionRequest> at_ff =
      fixture.audition(Rational(0), armed(), pitch);
  ASSERT_TRUE(at_ff.has_value());
  EXPECT_EQ(at_ff->velocity, velocity_for_dynamic(Dynamic::kFf));
  EXPECT_NE(at_ff->velocity, at_default->velocity);
}

// ---- to_midi_pitch() failure ----

TEST(NoteAuditionTest, UnsoundableInsertedPitchProducesNoAudition) {
  Fixture fixture;
  append_whole_rest(fixture);
  fixture.normalize_voice();
  // B9 resolves to semitone 131, outside MidiPitch's [0, 127] range.
  const SpelledPitch unsoundable = *SpelledPitch::create(Letter::kB, 9);
  ASSERT_FALSE(unsoundable.to_midi_pitch().has_value());

  // The entry itself is still a valid, notatable edit -- only the audition
  // is dropped.
  EXPECT_TRUE(fixture.command_exists(Rational(0), armed(), unsoundable));
  EXPECT_FALSE(fixture.audition(Rational(0), armed(), unsoundable).has_value());
}

TEST(NoteAuditionTest, UnsoundablePreExistingChordPitchIsSilentlySkipped) {
  Fixture            fixture;
  const SpelledPitch unsoundable = *SpelledPitch::create(Letter::kB, 9);
  const SpelledPitch c           = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e           = *SpelledPitch::create(Letter::kE, 4);
  ASSERT_FALSE(unsoundable.to_midi_pitch().has_value());

  append_whole_rest(fixture);
  ASSERT_TRUE(fixture.voice()
                  .append(make_chord(
                      *Duration::create(NoteValue::kQuarter, 0),
                      {{NotationEntityId::generate(), c, false},
                       {NotationEntityId::generate(), unsoundable, false}}))
                  .ok());
  fixture.normalize_voice();

  const std::optional<NoteAuditionRequest> request =
      fixture.audition(Rational(1), armed(), e);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(pitch_values(*request), (std::vector<std::uint8_t>{60, 64}));
}

// ---- Value semantics ----

TEST(NoteAuditionTest, RequestsCompareByValue) {
  Fixture fixture;
  append_whole_rest(fixture);
  fixture.normalize_voice();
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kG, 4);

  const std::optional<NoteAuditionRequest> first =
      fixture.audition(Rational(0), armed(), pitch);
  const std::optional<NoteAuditionRequest> second =
      fixture.audition(Rational(0), armed(), pitch);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, *second);

  NoteAuditionRequest louder = *first;
  louder.velocity            = *MidiVelocity::create(1);
  EXPECT_NE(*first, louder);
}

// ---- Anti-divergence regression ----

// For a representative spread of clicks, an audition exists exactly when
// make_note_entry_command builds a command AND the click introduces a newly
// sounding pitch. This is the regression guarding the shared branch
// resolution the two entry points are built on.
TEST(NoteAuditionTest, AuditionAgreesWithTheCommandOnEveryRepresentativeClick) {
  struct Case {
    std::string                   name;
    std::function<void(Fixture&)> setup;
    Rational                      position;
    NotePaletteEntrySpec          spec;
    std::optional<SpelledPitch>   candidate_pitch;
    bool                          expects_command = false;
    bool                          newly_sounds    = false;
  };

  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);

  const auto with_note = [c](Fixture& fixture) {
    append_quarter_note(fixture, c);
    fixture.normalize_voice();
  };
  const auto with_rest = [](Fixture& fixture) {
    append_whole_rest(fixture);
    fixture.normalize_voice();
  };
  const auto with_chord = [c, e](Fixture& fixture) {
    append_whole_rest(fixture);
    EXPECT_TRUE(
        fixture.voice()
            .append(make_chord(*Duration::create(NoteValue::kQuarter, 0),
                               {{NotationEntityId::generate(), c, false},
                                {NotationEntityId::generate(), e, false}}))
            .ok());
    fixture.normalize_voice();
  };
  const auto untouched = [](Fixture&) {};

  const std::vector<Case> cases = {
      {"empty voice, note", untouched, Rational(0),
       armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2), c, true,
       true},
      {"empty voice, rest", untouched, Rational(0),
       armed(NoteValue::kQuarter, NotePaletteEntryKind::kRest, 2), std::nullopt,
       true, false},
      {"empty voice, note off an onset", untouched, Rational(1) / Rational(3),
       armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2), c, false,
       false},
      {"empty voice, note with no pitch", untouched, Rational(0),
       armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2), std::nullopt,
       false, false},
      {"rest to note", with_rest, Rational(0), armed(), g, true, true},
      {"rest duration only", with_rest, Rational(0),
       armed(NoteValue::kHalf, NotePaletteEntryKind::kRest), std::nullopt, true,
       false},
      {"note duration only", with_note, Rational(0),
       armed(NoteValue::kHalf, NotePaletteEntryKind::kNote), c, true, false},
      {"note to chord", with_note, Rational(0), armed(), g, true, true},
      {"note to rest", with_note, Rational(0),
       armed(NoteValue::kQuarter, NotePaletteEntryKind::kRest), std::nullopt,
       true, false},
      {"note, no candidate pitch", with_note, Rational(0), armed(),
       std::nullopt, false, false},
      {"no event at position", with_note, Rational(1) / Rational(3), armed(), g,
       false, false},
      {"chord extension", with_chord, Rational(1), armed(), g, true, true},
      {"chord duplicate pitch", with_chord, Rational(1),
       armed(NoteValue::kHalf, NotePaletteEntryKind::kNote), e, true, false},
      {"chord to rest", with_chord, Rational(1),
       armed(NoteValue::kQuarter, NotePaletteEntryKind::kRest), std::nullopt,
       true, false},
  };

  for (const Case& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    Fixture fixture;
    test_case.setup(fixture);
    const bool has_command = fixture.command_exists(
        test_case.position, test_case.spec, test_case.candidate_pitch);
    const std::optional<NoteAuditionRequest> request = fixture.audition(
        test_case.position, test_case.spec, test_case.candidate_pitch);
    EXPECT_EQ(has_command, test_case.expects_command);
    EXPECT_EQ(request.has_value(),
              test_case.expects_command && test_case.newly_sounds);
    if (request.has_value())
      EXPECT_FALSE(request->pitches.empty());
  }
}

}  // namespace
