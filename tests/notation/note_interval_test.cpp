// SPDX-License-Identifier: Apache-2.0

#include "note_entry_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {
using note_entry_test::append_quarter_note;
using note_entry_test::append_whole_rest;
using note_entry_test::armed;
using note_entry_test::Fixture;
using note_entry_test::grace_note;
using note_entry_test::measure;

// ---- M5-phase-25: key-spelled diatonic interval entry ----------------------

[[nodiscard]] Measure keyed_measure(std::int8_t fifths) {
  return Measure{*TimeSignature::create(4, 4), *KeySignature::create(fifths)};
}

[[nodiscard]] Chord append_quarter_chord(
    Fixture& fixture, std::vector<SpelledPitch> pitches,
    std::vector<graphscore::Articulation> articulations = {},
    StemDirection                         stem = StemDirection::kAuto) {
  std::vector<ChordNote> notes;
  notes.reserve(pitches.size());
  for (const SpelledPitch& pitch : pitches) {
    notes.push_back({NotationEntityId::generate(), pitch, false});
  }
  Chord chord = make_chord(*Duration::create(NoteValue::kQuarter, 0),
                           std::move(notes), std::move(articulations), stem);
  EXPECT_TRUE(fixture.voice().append(chord).ok());
  return chord;
}

// `2` is one letter step; `8` is seven (an octave). Direction selects the
// side, and the target accidental is the key signature's own, never the
// source's. Every unmodified interval 2..8 (above) and every Shift interval
// 2..8 (below) is exercised from a single source, table-driven, so the
// dispatch of each digit and the below-direction arithmetic for 4..7 are
// both proven rather than spot-checked.
TEST(NoteIntervalTest, IntervalTargetPitchDiatonicStepCountAndDirection) {
  const SpelledPitch c4  = *SpelledPitch::create(Letter::kC, 4);
  const KeySignature key = *KeySignature::create(0);

  struct Case {
    std::uint8_t interval;
    Letter       above_letter;
    std::int8_t  above_octave;
    Letter       below_letter;
    std::int8_t  below_octave;
  };

  constexpr std::array<Case, 7> kCases{{
      {2, Letter::kD, 4, Letter::kB, 3},
      {3, Letter::kE, 4, Letter::kA, 3},
      {4, Letter::kF, 4, Letter::kG, 3},
      {5, Letter::kG, 4, Letter::kF, 3},
      {6, Letter::kA, 4, Letter::kE, 3},
      {7, Letter::kB, 4, Letter::kD, 3},
      {8, Letter::kC, 5, Letter::kC, 3},
  }};

  for (const Case& test_case : kCases) {
    EXPECT_EQ(
        interval_target_pitch(c4, test_case.interval, IntervalDirection::kAbove,
                              key),
        *SpelledPitch::create(test_case.above_letter, test_case.above_octave))
        << "interval " << static_cast<int>(test_case.interval) << " above";
    EXPECT_EQ(
        interval_target_pitch(c4, test_case.interval, IntervalDirection::kBelow,
                              key),
        *SpelledPitch::create(test_case.below_letter, test_case.below_octave))
        << "interval " << static_cast<int>(test_case.interval) << " below";
  }
}

TEST(NoteIntervalTest, IntervalTargetPitchOctaveCrossing) {
  const KeySignature key = *KeySignature::create(0);

  // B -> C upward increments the octave; C -> B downward decrements it, even
  // across a multi-step interval.
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kB, 4), 2,
                                  IntervalDirection::kAbove, key),
            *SpelledPitch::create(Letter::kC, 5));
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kB, 4), 3,
                                  IntervalDirection::kAbove, key),
            *SpelledPitch::create(Letter::kD, 5));
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kC, 4), 2,
                                  IntervalDirection::kBelow, key),
            *SpelledPitch::create(Letter::kB, 3));
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kC, 4), 3,
                                  IntervalDirection::kBelow, key),
            *SpelledPitch::create(Letter::kA, 3));
  // C0 stepping down one letter crosses into the negative octave (B-1).
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kC, 0), 2,
                                  IntervalDirection::kBelow, key),
            *SpelledPitch::create(Letter::kB, -1));
}

// The target accidental is the key signature's own, proven against the full
// standard sharp/flat table rather than selected thresholds: every fifth
// count -7..7, both major and minor modes, and every destination letter
// (interval 2..8 above C4 lands on D, E, F, G, A, B, then C5). The expected
// accidental is recomputed here from the sharp (F C G D A E B) and flat
// (B E A D G C F) orders, independent of the domain helper under test. Mode
// never alters the accidental set -- that is the existing domain contract --
// so both modes are asserted to produce the identical spelling.
TEST(NoteIntervalTest, IntervalTargetPitchUsesKeySignatureAccidental) {
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);

  constexpr std::array<Letter, 7> kSharps = {Letter::kF, Letter::kC, Letter::kG,
                                             Letter::kD, Letter::kA, Letter::kE,
                                             Letter::kB};
  constexpr std::array<Letter, 7> kFlats  = {Letter::kB, Letter::kE, Letter::kA,
                                             Letter::kD, Letter::kG, Letter::kC,
                                             Letter::kF};
  // Interval 2..8 above C4 selects destination letters D..C in order.
  constexpr std::array<Letter, 7> kDestination = {
      Letter::kD, Letter::kE, Letter::kF, Letter::kG,
      Letter::kA, Letter::kB, Letter::kC};

  for (std::int8_t fifths = KeySignature::kMinFifths;
       fifths <= KeySignature::kMaxFifths; ++fifths) {
    for (const Mode mode : {Mode::kMajor, Mode::kMinor}) {
      const KeySignature key = *KeySignature::create(fifths, mode);
      for (std::uint8_t interval = 2; interval <= 8; ++interval) {
        const Letter destination = kDestination[interval - 2];
        Accidental   expected    = Accidental::kNatural;
        if (fifths > 0 &&
            std::ranges::find(kSharps.begin(), kSharps.begin() + fifths,
                              destination) != kSharps.begin() + fifths) {
          expected = Accidental::kSharp;
        } else if (fifths < 0 &&
                   std::ranges::find(kFlats.begin(), kFlats.begin() - fifths,
                                     destination) != kFlats.begin() - fifths) {
          expected = Accidental::kFlat;
        }
        const std::optional<SpelledPitch> expected_pitch =
            SpelledPitch::create(destination, interval == 8 ? 5 : 4, expected);
        ASSERT_TRUE(expected_pitch.has_value());
        EXPECT_EQ(
            interval_target_pitch(c4, interval, IntervalDirection::kAbove, key),
            *expected_pitch)
            << "fifths=" << static_cast<int>(fifths)
            << " mode=" << static_cast<int>(mode)
            << " interval=" << static_cast<int>(interval);
      }
    }
  }
}

// An altered source still names its diatonic source letter; only the
// destination reads the key signature.
TEST(NoteIntervalTest, IntervalTargetPitchIgnoresSourceAccidental) {
  // C#4 up a second in C major is D natural, not D#.
  EXPECT_EQ(interval_target_pitch(
                *SpelledPitch::create(Letter::kC, 4, Accidental::kSharp), 2,
                IntervalDirection::kAbove, *KeySignature::create(0)),
            *SpelledPitch::create(Letter::kD, 4, Accidental::kNatural));
  // Db4 up a second in Db major (5 flats) is Eb4 (E is the key's second
  // flat), not E natural.
  EXPECT_EQ(interval_target_pitch(
                *SpelledPitch::create(Letter::kD, 4, Accidental::kFlat), 2,
                IntervalDirection::kAbove, *KeySignature::create(-5)),
            *SpelledPitch::create(Letter::kE, 4, Accidental::kFlat));
}

TEST(NoteIntervalTest, IntervalTargetPitchRejectsOutOfRange) {
  const KeySignature key = *KeySignature::create(0);

  // `interval` is 2..8 only.
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kC, 4), 0,
                                  IntervalDirection::kAbove, key),
            std::nullopt);
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kC, 4), 1,
                                  IntervalDirection::kAbove, key),
            std::nullopt);
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kC, 4), 9,
                                  IntervalDirection::kAbove, key),
            std::nullopt);

  // Octave range: B9 up a second leaves the octave range.
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kB, 9), 2,
                                  IntervalDirection::kAbove, key),
            std::nullopt);
  // Sounding MIDI range: G9 up a second is A9, MIDI 129, past 127.
  EXPECT_EQ(interval_target_pitch(*SpelledPitch::create(Letter::kG, 9), 2,
                                  IntervalDirection::kAbove, key),
            std::nullopt);
}

TEST(NoteIntervalTest, NoteheadMeasureIndexAndKeySignatureResolveMeasure) {
  Fixture            fixture({keyed_measure(0), keyed_measure(4)});
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4 = *SpelledPitch::create(Letter::kD, 4);
  const Note whole0 = make_note(c4, *Duration::create(NoteValue::kWhole, 0));
  const Note whole1 = make_note(d4, *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice().append(whole0).ok());
  ASSERT_TRUE(fixture.voice().append(whole1).ok());
  fixture.normalize_voice();

  const NoteheadItem item0{fixture.node_id, fixture.track(), fixture.stave_id(),
                           *Voice::create(1), whole0.id};
  const NoteheadItem item1{fixture.node_id, fixture.track(), fixture.stave_id(),
                           *Voice::create(1), whole1.id};

  EXPECT_EQ(notehead_measure_index(fixture.project, item0), std::size_t{0});
  EXPECT_EQ(notehead_measure_index(fixture.project, item1), std::size_t{1});
  EXPECT_EQ(notehead_key_signature(fixture.project, item0),
            *KeySignature::create(0));
  EXPECT_EQ(notehead_key_signature(fixture.project, item1),
            *KeySignature::create(4));
}

TEST(NoteIntervalTest, NoteheadMeasureIndexRejectsStaleAndGraceNote) {
  Fixture            fixture;
  const SpelledPitch c4        = *SpelledPitch::create(Letter::kC, 4);
  const Note         principal = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  // Stale id.
  const NoteheadItem stale{fixture.node_id, fixture.track(), fixture.stave_id(),
                           *Voice::create(1), NotationEntityId::generate()};
  EXPECT_EQ(notehead_measure_index(fixture.project, stale), std::nullopt);
  EXPECT_EQ(notehead_key_signature(fixture.project, stale), std::nullopt);

  // Grace note: no rhythmic measure of its own.
  const GraceGroup group = make_grace_group(principal.id, {grace_note(c4)});
  ASSERT_TRUE(fixture.voice().add_grace_group(group).ok());
  const NoteheadItem grace_item{fixture.node_id, fixture.track(),
                                fixture.stave_id(), *Voice::create(1),
                                group.notes[0].id};
  EXPECT_EQ(notehead_measure_index(fixture.project, grace_item), std::nullopt);
  EXPECT_EQ(notehead_key_signature(fixture.project, grace_item), std::nullopt);
}

TEST(NoteIntervalTest, AddIntervalPromotesNoteToChordPreservingIdentity) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  auto command = make_add_interval_command(fixture.project, item, 2,
                                           IntervalDirection::kAbove);
  ASSERT_NE(command, nullptr);
  const auto* add_command =
      static_cast<const AddIntervalCommand*>(command.get());
  const NotationEntityId inserted = add_command->inserted_notehead_id();

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  const VoiceEvent& event = fixture.voice().events().front();
  const auto*       chord = std::get_if<Chord>(&event);
  ASSERT_NE(chord, nullptr);
  EXPECT_EQ(chord->notes.size(), 2u);
  // The Note's own id becomes the first ChordNote; the new pitch is D4
  // natural (C major) with the fresh id.
  EXPECT_EQ(chord->notes[0].id, original.id);
  EXPECT_EQ(chord->notes[0].pitch, c4);
  EXPECT_EQ(chord->notes[0].tied_to_next, false);
  EXPECT_EQ(chord->notes[1].id, inserted);
  EXPECT_NE(inserted, original.id);
  EXPECT_EQ(chord->notes[1].pitch, *SpelledPitch::create(Letter::kD, 4));
  // Duration preserved.
  EXPECT_EQ(chord->duration, *Duration::create(NoteValue::kQuarter, 0));
}

TEST(NoteIntervalTest, AddIntervalExtendsChordPreservingEverything) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  const std::vector<graphscore::Articulation> accent = {
      graphscore::Articulation::kAccent};
  const Chord chord =
      append_quarter_chord(fixture, {c4, e4}, accent, StemDirection::kUp);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), chord.notes[0].id};
  auto command = make_add_interval_command(fixture.project, item, 4,
                                           IntervalDirection::kAbove);
  ASSERT_NE(command, nullptr);
  const auto* add_command =
      static_cast<const AddIntervalCommand*>(command.get());
  const NotationEntityId inserted = add_command->inserted_notehead_id();

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  const VoiceEvent& event = fixture.voice().events().front();
  const auto*       grown = std::get_if<Chord>(&event);
  ASSERT_NE(grown, nullptr);
  // Top-level identity, duration, articulations, and stem override preserved.
  EXPECT_EQ(grown->id, chord.id);
  EXPECT_EQ(grown->duration, chord.duration);
  EXPECT_EQ(grown->articulations, chord.articulations);
  EXPECT_EQ(grown->stem, chord.stem);
  ASSERT_EQ(grown->notes.size(), 3u);
  // Existing noteheads byte-for-byte preserved.
  EXPECT_EQ(grown->notes[0], chord.notes[0]);
  EXPECT_EQ(grown->notes[1], chord.notes[1]);
  // New notehead: F4 natural (C major), fresh id, untied.
  EXPECT_EQ(grown->notes[2].id, inserted);
  EXPECT_EQ(grown->notes[2].pitch, *SpelledPitch::create(Letter::kF, 4));
  EXPECT_EQ(grown->notes[2].tied_to_next, false);
}

// Promotion is not just identity-preserving for a default Note: a Note with
// non-default state -- an outgoing tie, articulations, and a stem override --
// carries all of that into the promoted Chord, and the source notehead keeps
// its own outgoing tie.
TEST(NoteIntervalTest, AddIntervalPromotesNotePreservingNonDefaultState) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const std::vector<graphscore::Articulation> markings = {
      graphscore::Articulation::kAccent, graphscore::Articulation::kStaccato};
  const Note original =
      make_note(c4, *Duration::create(NoteValue::kQuarter, 0),
                /*tied_to_next=*/true, markings, StemDirection::kUp);
  ASSERT_TRUE(fixture.voice().append(original).ok());
  // The outgoing tie must resolve into a following event sounding C4 for the
  // voice to stay valid both before and after promotion.
  append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  auto command = make_add_interval_command(fixture.project, item, 2,
                                           IntervalDirection::kAbove);
  ASSERT_NE(command, nullptr);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  const auto* chord = std::get_if<Chord>(&fixture.voice().events().front());
  ASSERT_NE(chord, nullptr);
  EXPECT_EQ(chord->articulations, markings);
  EXPECT_EQ(chord->stem, StemDirection::kUp);
  EXPECT_EQ(chord->duration, original.duration);
  ASSERT_EQ(chord->notes.size(), 2u);
  EXPECT_EQ(chord->notes[0].id, original.id);
  EXPECT_EQ(chord->notes[0].pitch, c4);
  EXPECT_EQ(chord->notes[0].tied_to_next, true);
  EXPECT_EQ(chord->notes[1].pitch, *SpelledPitch::create(Letter::kD, 4));
  EXPECT_EQ(chord->notes[1].tied_to_next, false);
}

// Chord growth preserves each existing notehead's own outgoing tie, alongside
// the top-level identity, articulations, and stem override already asserted
// by AddIntervalExtendsChordPreservingEverything.
TEST(NoteIntervalTest, AddIntervalExtendsChordPreservingTiedNoteheads) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4 = *SpelledPitch::create(Letter::kE, 4);
  const std::vector<graphscore::Articulation> markings = {
      graphscore::Articulation::kTenuto};
  const ChordNote c_note{NotationEntityId::generate(), c4, true};
  const ChordNote e_note{NotationEntityId::generate(), e4, true};
  const Chord     chord =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), {c_note, e_note},
                 markings, StemDirection::kDown);
  ASSERT_TRUE(fixture.voice().append(chord).ok());
  // Both outgoing ties resolve into a following chord sounding the same two
  // pitches.
  const Chord resolve =
      make_chord(*Duration::create(NoteValue::kQuarter, 0),
                 {ChordNote{NotationEntityId::generate(), c4, false},
                  ChordNote{NotationEntityId::generate(), e4, false}});
  ASSERT_TRUE(fixture.voice().append(resolve).ok());
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), chord.notes[0].id};
  auto command = make_add_interval_command(fixture.project, item, 4,
                                           IntervalDirection::kAbove);
  ASSERT_NE(command, nullptr);
  const auto* add_command =
      static_cast<const AddIntervalCommand*>(command.get());
  const NotationEntityId inserted = add_command->inserted_notehead_id();

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());

  const auto* grown = std::get_if<Chord>(&fixture.voice().events().front());
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(grown->id, chord.id);
  EXPECT_EQ(grown->duration, chord.duration);
  EXPECT_EQ(grown->articulations, markings);
  EXPECT_EQ(grown->stem, StemDirection::kDown);
  ASSERT_EQ(grown->notes.size(), 3u);
  // Existing noteheads preserved byte-for-byte, outgoing ties included.
  EXPECT_EQ(grown->notes[0], chord.notes[0]);
  EXPECT_EQ(grown->notes[1], chord.notes[1]);
  EXPECT_EQ(grown->notes[0].tied_to_next, true);
  EXPECT_EQ(grown->notes[1].tied_to_next, true);
  // New notehead: F4 natural, fresh id, untied.
  EXPECT_EQ(grown->notes[2].id, inserted);
  EXPECT_EQ(grown->notes[2].pitch, *SpelledPitch::create(Letter::kF, 4));
  EXPECT_EQ(grown->notes[2].tied_to_next, false);
}

TEST(NoteIntervalTest, AddIntervalDuplicatePitchIsRejectedAtomically) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4    = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4    = *SpelledPitch::create(Letter::kD, 4);
  const Chord        chord = append_quarter_chord(fixture, {c4, d4});
  fixture.normalize_voice();
  const VoiceContent pre = fixture.voice();

  // C4 up a second is D4, which already exists in the chord.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), chord.notes[0].id};
  auto command = make_add_interval_command(fixture.project, item, 2,
                                           IntervalDirection::kAbove);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  EXPECT_FALSE(history.execute_new(std::move(command), fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre);
  EXPECT_FALSE(history.can_undo());
}

TEST(NoteIntervalTest, AddIntervalUndoRedoRoundTrips) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();
  const VoiceContent pre = fixture.voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  auto command = make_add_interval_command(fixture.project, item, 2,
                                           IntervalDirection::kAbove);
  ASSERT_NE(command, nullptr);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(command), fixture.project).ok());
  ASSERT_NE(std::get_if<Chord>(&fixture.voice().events().front()), nullptr);

  ASSERT_TRUE(history.undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre);
  // Back to a top-level Note, not a Chord.
  EXPECT_NE(std::get_if<Note>(&fixture.voice().events().front()), nullptr);
  EXPECT_EQ(std::get_if<Chord>(&fixture.voice().events().front()), nullptr);
  EXPECT_TRUE(history.can_redo());

  ASSERT_TRUE(history.redo(fixture.project).ok());
  EXPECT_NE(std::get_if<Chord>(&fixture.voice().events().front()), nullptr);
}

TEST(NoteIntervalTest, MakeAddIntervalCommandRejectsIneligibleSources) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();
  const NoteheadItem valid{fixture.node_id, fixture.track(), fixture.stave_id(),
                           *Voice::create(1), original.id};

  // Stale id.
  const NoteheadItem stale{fixture.node_id, fixture.track(), fixture.stave_id(),
                           *Voice::create(1), NotationEntityId::generate()};
  EXPECT_EQ(make_add_interval_command(fixture.project, stale, 2,
                                      IntervalDirection::kAbove),
            nullptr);

  // Grace note.
  const GraceGroup group = make_grace_group(original.id, {grace_note(c4)});
  ASSERT_TRUE(fixture.voice().add_grace_group(group).ok());
  const NoteheadItem grace_item{fixture.node_id, fixture.track(),
                                fixture.stave_id(), *Voice::create(1),
                                group.notes[0].id};
  EXPECT_EQ(make_add_interval_command(fixture.project, grace_item, 2,
                                      IntervalDirection::kAbove),
            nullptr);

  // A valid source still builds.
  EXPECT_NE(make_add_interval_command(fixture.project, valid, 2,
                                      IntervalDirection::kAbove),
            nullptr);
}

TEST(NoteIntervalTest, AddIntervalRejectsTargetOutsideRange) {
  // G9 up a second is A9, MIDI 129: the command must fail atomically.
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch g9 = *SpelledPitch::create(Letter::kG, 9);
  const Note note = make_note(g9, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), note.id};
  auto command = make_add_interval_command(fixture.project, item, 2,
                                           IntervalDirection::kAbove);
  ASSERT_NE(command, nullptr);
  CommandHistory history;
  EXPECT_FALSE(history.execute_new(std::move(command), fixture.project).ok());
}

TEST(NoteIntervalTest, AuditionAddIntervalNoteToChordSoundsBothPitches) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  const auto request = audition_for_add_interval(fixture.project, item, 2,
                                                 IntervalDirection::kAbove);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->track_id, fixture.track());
  ASSERT_EQ(request->pitches.size(), 2u);
  EXPECT_EQ(request->pitches[0].value(), 60);  // C4
  EXPECT_EQ(request->pitches[1].value(), 62);  // D4
}

TEST(NoteIntervalTest, AuditionAddIntervalChordExtensionSoundsWholeChord) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4    = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e4    = *SpelledPitch::create(Letter::kE, 4);
  const Chord        chord = append_quarter_chord(fixture, {c4, e4});
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), chord.notes[0].id};
  const auto request = audition_for_add_interval(fixture.project, item, 4,
                                                 IntervalDirection::kAbove);
  ASSERT_TRUE(request.has_value());
  ASSERT_EQ(request->pitches.size(), 3u);
  EXPECT_EQ(request->pitches[0].value(), 60);  // C4
  EXPECT_EQ(request->pitches[1].value(), 64);  // E4
  EXPECT_EQ(request->pitches[2].value(), 65);  // F4 (4th above C)
}

TEST(NoteIntervalTest, AuditionAddIntervalDuplicateAndRejectionsAreSilent) {
  Fixture            fixture({keyed_measure(0)});
  const SpelledPitch c4    = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d4    = *SpelledPitch::create(Letter::kD, 4);
  const Chord        chord = append_quarter_chord(fixture, {c4, d4});
  fixture.normalize_voice();

  // C4 up a second duplicates D4: silent.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), chord.notes[0].id};
  EXPECT_EQ(audition_for_add_interval(fixture.project, item, 2,
                                      IntervalDirection::kAbove),
            std::nullopt);

  // Out-of-range interval is silent for an otherwise-valid source.
  EXPECT_EQ(audition_for_add_interval(fixture.project, item, 9,
                                      IntervalDirection::kAbove),
            std::nullopt);
}

}  // namespace
