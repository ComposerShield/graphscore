// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Command;
using graphscore::CommandHistory;
using graphscore::CommandTransaction;
using graphscore::decompose_measure_aligned_rests;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Hairpin;
using graphscore::HairpinDirection;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_move_notehead_command;
using graphscore::make_note;
using graphscore::make_note_entry_command;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteheadItem;
using graphscore::NoteheadStepDirection;
using graphscore::NotePaletteEntryKind;
using graphscore::NotePaletteEntrySpec;
using graphscore::NotePaletteState;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::SetEventCommand;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::StemDirection;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

[[nodiscard]] Measure measure(std::uint8_t  numerator   = 4,
                              std::uint16_t denominator = 4) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 graphscore::KeySignature{}};
}

struct Fixture {
  Project project{ProjectId::generate(), "Entry"};
  NodeId  node_id;
  TrackId track_id;

  // Defaults to two 4/4 measures (node_end() == 2); pass an explicit
  // `measures` to exercise a meter where the measure-aligned fill differs
  // from a naive whole-span decomposition (e.g. three 3/4 bars).
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

  // Normalizes the voice to the node's total length (fills with rests).
  void normalize_voice(std::uint8_t voice_index = 1) {
    const Rational end = node_end();
    EXPECT_TRUE(voice(voice_index).normalize(end).ok());
  }

  [[nodiscard]] Rational node_end() const {
    return project.find_node(node_id)->timeline()->node_end();
  }
};

// Helper: appends a whole-note rest and returns it.
Rest append_whole_rest(Fixture& fixture, std::uint8_t voice_index = 1) {
  Rest rest = make_rest(*Duration::create(NoteValue::kWhole, 0));
  EXPECT_TRUE(fixture.voice(voice_index).append(rest).ok());
  return rest;
}

// Helper: appends a quarter note, returns it.
Note append_quarter_note(Fixture& fixture, const SpelledPitch& pitch,
                         std::uint8_t voice_index = 1) {
  Note note = make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  EXPECT_TRUE(fixture.voice(voice_index).append(note).ok());
  return note;
}

// Helper: construct an armed entry spec.
[[nodiscard]] NotePaletteEntrySpec armed(
    NoteValue            note_value  = NoteValue::kQuarter,
    NotePaletteEntryKind entry_kind  = NotePaletteEntryKind::kNote,
    std::uint8_t         voice_index = 1) {
  const NotePaletteState state = *NotePaletteState::create(
      note_value, 0, entry_kind, *Voice::create(voice_index));
  return state.next_entry_spec();
}

// ---- Duration-only replacement ----

TEST(NoteEntryTest, SamePitchReplacesDurationAndPreservesIdentity) {
  Fixture            fixture;
  const SpelledPitch pitch    = *SpelledPitch::create(Letter::kE, 4);
  const Note         original = append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const VoiceEvent& ev = fixture.voice().events().front();
  ASSERT_TRUE(std::holds_alternative<Note>(ev));
  const Note& updated = std::get<Note>(ev);
  EXPECT_EQ(updated.id, original.id);
  EXPECT_EQ(updated.pitch, pitch);
  EXPECT_EQ(updated.duration.base(), NoteValue::kHalf);
  // Undo / redo round-trip.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice().events().front()));
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()).id, original.id);
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()).duration.base(),
            NoteValue::kQuarter);
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()).duration.base(),
            NoteValue::kHalf);
}

TEST(NoteEntryTest, SamePitchOnChordPreservesEveryIdentity) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  append_whole_rest(fixture);  // fill measure 0
  const Chord original = make_chord(*Duration::create(NoteValue::kQuarter, 0),
                                    {{NotationEntityId::generate(), c, false},
                                     {NotationEntityId::generate(), e, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();
  // Arm half note and click on the E4 pitch (already in the chord).
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(1), spec, e);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const VoiceEvent& ev = fixture.voice().events()[1];
  ASSERT_TRUE(std::holds_alternative<Chord>(ev));
  const Chord& updated = std::get<Chord>(ev);
  EXPECT_EQ(updated.id, original.id);
  EXPECT_EQ(updated.notes.size(), 2u);
  EXPECT_EQ(updated.duration.base(), NoteValue::kHalf);
  // All notehead ids preserved.
  EXPECT_EQ(updated.notes[0].id, original.notes[0].id);
  EXPECT_EQ(updated.notes[1].id, original.notes[1].id);
  // Undo/redo.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events()[1]).duration.base(),
            NoteValue::kQuarter);
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events()[1]).duration.base(),
            NoteValue::kHalf);
}

TEST(NoteEntryTest, DuplicatePitchClickDoesNotAddDuplicateNotehead) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  append_whole_rest(fixture);
  const Chord original = make_chord(*Duration::create(NoteValue::kQuarter, 0),
                                    {{NotationEntityId::generate(), c, false},
                                     {NotationEntityId::generate(), e, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();
  // Click on C4 which is already a notehead in the chord.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(1), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const Chord& updated = std::get<Chord>(fixture.voice().events()[1]);
  EXPECT_EQ(updated.notes.size(), 2u);
  EXPECT_EQ(updated.duration.base(), NoteValue::kHalf);
}

// ---- Chord building ----

TEST(NoteEntryTest, DifferentPitchPromotesNoteToChord) {
  Fixture            fixture;
  const SpelledPitch c        = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e        = *SpelledPitch::create(Letter::kE, 4);
  const Note         original = append_quarter_note(fixture, c);
  fixture.normalize_voice();
  // Arm eighths, click on E4 (different from the existing C4).
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, e);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const VoiceEvent& ev = fixture.voice().events().front();
  ASSERT_TRUE(std::holds_alternative<Chord>(ev));
  const Chord& chord = std::get<Chord>(ev);
  EXPECT_EQ(chord.notes.size(), 2u);
  EXPECT_EQ(chord.duration.base(), NoteValue::kEighth);
  // The original Note's id survives as a ChordNote.
  EXPECT_EQ(chord.notes[0].id, original.id);
  EXPECT_EQ(chord.notes[0].pitch, c);
  // New notehead has a fresh id.
  EXPECT_NE(chord.notes[1].id, original.id);
  EXPECT_EQ(chord.notes[1].pitch, e);
  // Undo/redo.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(fixture.voice().events().front()));
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(fixture.voice().events().front()));
}

TEST(NoteEntryTest, DifferentPitchAddsToExistingChord) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);
  append_whole_rest(fixture);
  const Chord original = make_chord(*Duration::create(NoteValue::kQuarter, 0),
                                    {{NotationEntityId::generate(), c, false},
                                     {NotationEntityId::generate(), e, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();
  // Arm sixteenth, click on G4 (new to the chord).
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kSixteenth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(1), spec, g);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const Chord& updated = std::get<Chord>(fixture.voice().events()[1]);
  EXPECT_EQ(updated.id, original.id);
  EXPECT_EQ(updated.notes.size(), 3u);
  EXPECT_EQ(updated.duration.base(), NoteValue::kSixteenth);
  EXPECT_EQ(updated.notes[2].pitch, g);
  // Existing noteheads preserved.
  EXPECT_EQ(updated.notes[0].id, original.notes[0].id);
  EXPECT_EQ(updated.notes[1].id, original.notes[1].id);
  // Undo restores 2-note chord.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events()[1]).notes.size(), 2u);
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(std::get<Chord>(fixture.voice().events()[1]).notes.size(), 3u);
}

// ---- Rest conversion ----

TEST(NoteEntryTest, RestReplacedByNoteWhenNoteEntryArmed) {
  Fixture fixture;
  append_whole_rest(fixture);
  fixture.normalize_voice();
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kD, 5);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const VoiceEvent& ev = fixture.voice().events().front();
  ASSERT_TRUE(std::holds_alternative<Note>(ev));
  EXPECT_EQ(std::get<Note>(ev).pitch, pitch);
  EXPECT_EQ(std::get<Note>(ev).duration.base(), NoteValue::kEighth);
  // Undo restores the rest.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(fixture.voice().events().front()));
}

TEST(NoteEntryTest, NoteReplacedByRestWhenRestEntryArmed) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kWhole, NotePaletteEntryKind::kRest);
  auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                     fixture.track(), fixture.stave_id(),
                                     Rational(0), spec, std::nullopt);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
  EXPECT_EQ(std::get<Rest>(fixture.voice().events().front()).duration.base(),
            NoteValue::kWhole);
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(fixture.voice().events().front()));
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(fixture.voice().events().front()));
}

TEST(NoteEntryTest, RestDurationOnlyPreservesRestIdentity) {
  Fixture                    fixture;
  const Rest                 original = append_whole_rest(fixture);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kRest);
  auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                     fixture.track(), fixture.stave_id(),
                                     Rational(0), spec, std::nullopt);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const Rest& updated = std::get<Rest>(fixture.voice().events().front());
  EXPECT_EQ(updated.id, original.id);
  EXPECT_EQ(updated.duration.base(), NoteValue::kHalf);
}

// ---- Rejection and error cases ----

TEST(NoteEntryTest, ReturnsNullptrWhenNoEventAtPosition) {
  Fixture fixture;
  append_quarter_note(fixture, *SpelledPitch::create(Letter::kC, 4));
  auto cmd = make_note_entry_command(
      fixture.project, fixture.node_id, fixture.track(), fixture.stave_id(),
      Rational(1), armed(), *SpelledPitch::create(Letter::kD, 4));
  EXPECT_EQ(cmd, nullptr);
}

TEST(NoteEntryTest, ReturnsNullptrWhenCandidatePitchMissingForNoteEntry) {
  Fixture fixture;
  append_quarter_note(fixture, *SpelledPitch::create(Letter::kC, 4));
  auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                     fixture.track(), fixture.stave_id(),
                                     Rational(0), armed(), std::nullopt);
  EXPECT_EQ(cmd, nullptr);
}

TEST(NoteEntryTest, ReturnsNullptrWhenNodeMissing) {
  Fixture fixture;
  append_quarter_note(fixture, *SpelledPitch::create(Letter::kC, 4));
  auto cmd = make_note_entry_command(
      fixture.project, NodeId::generate(), fixture.track(), fixture.stave_id(),
      Rational(0), armed(), *SpelledPitch::create(Letter::kD, 4));
  EXPECT_EQ(cmd, nullptr);
}

// ---- Untouched voices and score material ----

TEST(NoteEntryTest, OtherVoicesUnaffected) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d = *SpelledPitch::create(Letter::kD, 4);
  append_quarter_note(fixture, c, 1);
  const Note voice2_original = append_quarter_note(fixture, d, 2);
  fixture.normalize_voice(1);
  fixture.normalize_voice(2);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  // Voice 2 unchanged.
  const VoiceEvent& v2_ev = fixture.voice(2).events().front();
  ASSERT_TRUE(std::holds_alternative<Note>(v2_ev));
  EXPECT_EQ(std::get<Note>(v2_ev), voice2_original);
}

// The palette-selected voice is the sole source of truth; only that voice
// may change.  Here armed.voice is voice 2, but a separate caller would have
// no argument for an alternative voice, proving the contract.
TEST(NoteEntryTest, ArmedVoiceIsOnlyChangedVoice) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d = *SpelledPitch::create(Letter::kD, 4);
  append_quarter_note(fixture, c, 2);
  append_quarter_note(fixture, d, 3);
  fixture.normalize_voice(2);
  fixture.normalize_voice(3);
  // Arm a half note in voice 2.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  // Voice 2 changed.
  EXPECT_EQ(std::get<Note>(fixture.voice(2).events().front()).duration.base(),
            NoteValue::kHalf);
  // Voice 3 untouched.
  EXPECT_EQ(std::get<Note>(fixture.voice(3).events().front()).duration.base(),
            NoteValue::kQuarter);
}

// ---- CommandHistory integration ----

TEST(NoteEntryTest, CommandExecutedThroughCommandHistoryWithUndoRedo) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);

  CommandHistory history;
  EXPECT_TRUE(history.execute_new(std::move(cmd), fixture.project).ok());
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()).duration.base(),
            NoteValue::kHalf);

  // Undo via history.
  EXPECT_TRUE(history.undo(fixture.project).ok());
  EXPECT_FALSE(history.can_undo());
  EXPECT_TRUE(history.can_redo());
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()).duration.base(),
            NoteValue::kQuarter);

  // Redo via history.
  EXPECT_TRUE(history.redo(fixture.project).ok());
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()).duration.base(),
            NoteValue::kHalf);
}

// ---- Atomicity: failed execute must leave project unchanged ----

TEST(NoteEntryTest, DoubleExecuteIsRejected) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  EXPECT_FALSE(cmd->execute(fixture.project).ok());
  // Project still in post-execute state.
  EXPECT_EQ(std::get<Note>(fixture.voice().events().front()).duration.base(),
            NoteValue::kHalf);
}

TEST(NoteEntryTest, DurationExpansionIntoLaterSoundingMaterialFailsAtomically) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d = *SpelledPitch::create(Letter::kD, 4);
  // Fill the voice: quarter rest at 0, then a quarter note at 1/4.
  EXPECT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kQuarter, 0)))
                  .ok());
  EXPECT_TRUE(
      fixture.voice()
          .append(make_note(d, *Duration::create(NoteValue::kQuarter, 0)))
          .ok());
  fixture.normalize_voice();
  const VoiceContent pre = fixture.voice();

  // Arm a half note and try to replace the rest at position 0 with a half
  // note.  This would expand the duration from 1/4 to 1/2, which needs to
  // consume 1/4 worth of rests after position 0.  The immediately following
  // event is a sounding note (D4), not a rest, so replacement must be
  // rejected atomically and the voice unchanged.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_FALSE(cmd->execute(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre);
}

// ---- Undo/redo full round-trip for all event kinds ----

TEST(NoteEntryTest, FullUndoRedoRoundTripForDurationOnly) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const auto                 voice_snapshot = fixture.voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kWhole, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), voice_snapshot);
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), voice_snapshot);
}

// ---- Normalization: duration expansion/contraction via SetEventCommand ----

TEST(NoteEntryTest, SetEventCommandNormalizesDurationContraction) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  // Fill first measure with a whole note, second with a whole rest.
  Note note = make_note(pitch, *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  // Replace with a quarter note: SetEventCommand must fill the gap with
  // normalized rests (decompose_rest).
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  // First event is the quarter note.
  EXPECT_EQ(std::get<Note>(fixture.voice().events()[0]).duration.base(),
            NoteValue::kQuarter);
  // Voice total length must still match node_end().
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
  // Undo restores original.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(std::get<Note>(fixture.voice().events()[0]).duration.base(),
            NoteValue::kWhole);
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
}

TEST(NoteEntryTest, SetEventCommandContractionFiveOneTwentyEighthsGap) {
  // dotted sixteenth (3/32 = 12/128) → double-dotted thirty-second
  // (7/128) leaves 5/128 gap.  Old greedy dead-ended; exact DP produces
  // [dotted 64th (3/128), 64th (2/128)].  Exercise full notation path
  // through make_note_entry_command + exact undo/redo.
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(c, *Duration::create(NoteValue::kSixteenth, 1)))
          .ok());
  fixture.normalize_voice();
  const VoiceContent pre_snapshot = fixture.voice();

  const NotePaletteState state =
      *NotePaletteState::create(NoteValue::kThirtySecond, 2,
                                NotePaletteEntryKind::kNote, *Voice::create(1));
  const NotePaletteEntrySpec spec = state.next_entry_spec();

  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, c);
  ASSERT_NE(cmd, nullptr);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());

  // Replacement event is double-dotted thirty-second at onset 0.
  ASSERT_GE(fixture.voice().events().size(), 2u);
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice().events()[0]));
  EXPECT_EQ(event_duration(fixture.voice().events()[0]).resolved(),
            *Rational::create(7, 128));

  // Gap must match decompose_rest(5/128): dotted 64th + 64th.
  const auto expected = decompose_rest(*Rational::create(5, 128));
  ASSERT_TRUE(expected.has_value());
  ASSERT_EQ(expected->size(), 2u);
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events()[1]));
  EXPECT_EQ(std::get<Rest>(fixture.voice().events()[1]).duration.base(),
            (*expected)[0].duration.base());
  EXPECT_EQ(std::get<Rest>(fixture.voice().events()[1]).duration.dots(),
            (*expected)[0].duration.dots());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events()[2]));
  EXPECT_EQ(std::get<Rest>(fixture.voice().events()[2]).duration.base(),
            (*expected)[1].duration.base());
  EXPECT_EQ(std::get<Rest>(fixture.voice().events()[2]).duration.dots(),
            (*expected)[1].duration.dots());

  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());

  // Exact undo/redo.
  const VoiceContent post_snapshot = fixture.voice();
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre_snapshot);
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), post_snapshot);
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre_snapshot);
}

// ---- Note→Chord promotion: semantic field preservation ----

TEST(NoteEntryTest, NoteToChordPreservesArticulationsAndStem) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  // Create a Note with articulations and a manual stem override.
  Note original = make_note(
      c, *Duration::create(NoteValue::kQuarter, 0), false,
      {graphscore::Articulation::kAccent, graphscore::Articulation::kStaccato},
      StemDirection::kDown);
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, e);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  const VoiceEvent& ev = fixture.voice().events().front();
  ASSERT_TRUE(std::holds_alternative<Chord>(ev));
  const Chord& chord = std::get<Chord>(ev);
  EXPECT_EQ(chord.notes.size(), 2u);
  EXPECT_EQ(chord.stem, StemDirection::kDown);
  ASSERT_EQ(chord.articulations.size(), 2u);
  EXPECT_EQ(chord.articulations[0], graphscore::Articulation::kAccent);
  EXPECT_EQ(chord.articulations[1], graphscore::Articulation::kStaccato);

  // Undo restores the original Note with articulations and stem.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  const Note& restored = std::get<Note>(fixture.voice().events().front());
  EXPECT_EQ(restored.stem, StemDirection::kDown);
  ASSERT_EQ(restored.articulations.size(), 2u);
  EXPECT_EQ(restored.articulations[0], graphscore::Articulation::kAccent);
  EXPECT_EQ(restored.articulations[1], graphscore::Articulation::kStaccato);

  // Redo restores the Chord with articulations and stem.
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  const Chord& rechord = std::get<Chord>(fixture.voice().events().front());
  EXPECT_EQ(rechord.stem, StemDirection::kDown);
  ASSERT_EQ(rechord.articulations.size(), 2u);
}

// ---- Chord extension: semantic field preservation ----

TEST(NoteEntryTest, ChordExtensionPreservesArticulations) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);
  append_whole_rest(fixture);
  const Chord original = make_chord(
      *Duration::create(NoteValue::kQuarter, 0),
      {{NotationEntityId::generate(), c, false},
       {NotationEntityId::generate(), e, false}},
      {graphscore::Articulation::kMarcato, graphscore::Articulation::kTenuto},
      StemDirection::kUp);
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kSixteenth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(1), spec, g);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const Chord& updated = std::get<Chord>(fixture.voice().events()[1]);
  EXPECT_EQ(updated.id, original.id);
  EXPECT_EQ(updated.notes.size(), 3u);
  EXPECT_EQ(updated.stem, StemDirection::kUp);
  ASSERT_EQ(updated.articulations.size(), 2u);
  EXPECT_EQ(updated.articulations[0], graphscore::Articulation::kMarcato);
  EXPECT_EQ(updated.articulations[1], graphscore::Articulation::kTenuto);

  // Undo restores original chord with articulations.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  const Chord& restored = std::get<Chord>(fixture.voice().events()[1]);
  EXPECT_EQ(restored.notes.size(), 2u);
  ASSERT_EQ(restored.articulations.size(), 2u);

  // Redo restores extended chord.
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  const Chord& reupdated = std::get<Chord>(fixture.voice().events()[1]);
  EXPECT_EQ(reupdated.notes.size(), 3u);
  ASSERT_EQ(reupdated.articulations.size(), 2u);
}

// ---- Note→Chord: event-reference ID remapping ----

TEST(NoteEntryTest, NoteToChordRemapsDynamicReference) {
  Fixture            fixture;
  const SpelledPitch c        = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e        = *SpelledPitch::create(Letter::kE, 4);
  const Note         original = append_quarter_note(fixture, c);
  fixture.normalize_voice();
  const NotationEntityId old_top_level_id = original.id;

  // Attach a dynamic marking to the Note.
  ASSERT_TRUE(
      fixture.voice()
          .add_dynamic(make_dynamic_marking(old_top_level_id, Dynamic::kF))
          .ok());

  // Promote Note to Chord by clicking E4.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, e);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  const VoiceEvent& ev = fixture.voice().events().front();
  ASSERT_TRUE(std::holds_alternative<Chord>(ev));
  const Chord& chord            = std::get<Chord>(ev);
  const auto   new_top_level_id = chord.id;
  EXPECT_NE(new_top_level_id, old_top_level_id);

  // The old Note ID survives as the first ChordNote.
  EXPECT_EQ(chord.notes[0].id, old_top_level_id);

  // The dynamic marking must now reference the new Chord top-level ID.
  ASSERT_EQ(fixture.voice().dynamics().size(), 1u);
  EXPECT_EQ(fixture.voice().dynamics()[0].at_event, new_top_level_id);

  // Undo: dynamic should reference the Note's top-level ID again.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().dynamics().size(), 1u);
  EXPECT_EQ(fixture.voice().dynamics()[0].at_event, old_top_level_id);

  // Redo: dynamic should reference the Chord's top-level ID again.
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  ASSERT_EQ(fixture.voice().dynamics().size(), 1u);
  EXPECT_EQ(fixture.voice().dynamics()[0].at_event, new_top_level_id);
}

TEST(NoteEntryTest, NoteToChordRemapsAllFiveReferenceFamilies) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);

  // Build: Note C4, then Note G4 (so we have a successor for spans).
  // Use eighth notes so the beam override is valid.
  Note c_note_raw =
      make_note(c, *Duration::create(NoteValue::kEighth, 0), false);
  Note g_note_raw =
      make_note(g, *Duration::create(NoteValue::kEighth, 0), false);
  const Rational c_pos = Rational(0);
  ASSERT_TRUE(fixture.voice().append(c_note_raw).ok());
  ASSERT_TRUE(fixture.voice().append(g_note_raw).ok());
  fixture.normalize_voice();

  const NotationEntityId old_top_level_id = c_note_raw.id;
  const NotationEntityId successor_id     = g_note_raw.id;

  // Attach dynamic to C4.
  ASSERT_TRUE(
      fixture.voice()
          .add_dynamic(make_dynamic_marking(old_top_level_id, Dynamic::kMf))
          .ok());
  // Attach hairpin from C4 to G4.
  ASSERT_TRUE(fixture.voice()
                  .add_hairpin(make_hairpin(old_top_level_id, successor_id,
                                            HairpinDirection::kCrescendo))
                  .ok());
  // Attach slur from C4 to G4.
  ASSERT_TRUE(
      fixture.voice().add_slur(make_slur(old_top_level_id, successor_id)).ok());
  // Attach beam override over [C4, G4].
  ASSERT_TRUE(fixture.voice()
                  .add_beam_override(graphscore::make_beam_override(
                      graphscore::BeamOverride::Kind::kJoin,
                      {old_top_level_id, successor_id}))
                  .ok());
  // Attach grace group with C4 as principal.
  ASSERT_TRUE(
      fixture.voice()
          .add_grace_group(make_grace_group(
              old_top_level_id,
              {GraceNote{.pitch    = *SpelledPitch::create(Letter::kB, 3),
                         .duration = *Duration::create(NoteValue::kEighth, 0),
                         .type     = GraceNoteType::kAppoggiatura}}))
          .ok());

  // Promote Note C4 to Chord by clicking E4 at C4's position.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), c_pos, spec, e);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  // Grab the new Chord top-level ID.
  const VoiceEvent& ev0 = fixture.voice().events()[0];
  ASSERT_TRUE(std::holds_alternative<Chord>(ev0));
  const auto new_top_level_id = std::get<Chord>(ev0).id;
  EXPECT_NE(new_top_level_id, old_top_level_id);

  // All five reference families remapped old_id → new_top_level_id.
  ASSERT_EQ(fixture.voice().dynamics().size(), 1u);
  EXPECT_EQ(fixture.voice().dynamics()[0].at_event, new_top_level_id);

  ASSERT_EQ(fixture.voice().hairpins().size(), 1u);
  EXPECT_EQ(fixture.voice().hairpins()[0].start_event, new_top_level_id);
  EXPECT_EQ(fixture.voice().hairpins()[0].end_event, successor_id);

  ASSERT_EQ(fixture.voice().slurs().size(), 1u);
  EXPECT_EQ(fixture.voice().slurs()[0].start_event, new_top_level_id);
  EXPECT_EQ(fixture.voice().slurs()[0].end_event, successor_id);

  ASSERT_EQ(fixture.voice().beam_overrides().size(), 1u);
  ASSERT_EQ(fixture.voice().beam_overrides()[0].events.size(), 2u);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[0], new_top_level_id);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[1], successor_id);

  ASSERT_EQ(fixture.voice().grace_groups().size(), 1u);
  EXPECT_EQ(fixture.voice().grace_groups()[0].principal_event,
            new_top_level_id);

  // Undo: all references back to old_top_level_id.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice().dynamics()[0].at_event, old_top_level_id);
  EXPECT_EQ(fixture.voice().hairpins()[0].start_event, old_top_level_id);
  EXPECT_EQ(fixture.voice().slurs()[0].start_event, old_top_level_id);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[0], old_top_level_id);
  EXPECT_EQ(fixture.voice().grace_groups()[0].principal_event,
            old_top_level_id);

  // Redo: all references back to new_top_level_id.
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice().dynamics()[0].at_event, new_top_level_id);
  EXPECT_EQ(fixture.voice().hairpins()[0].start_event, new_top_level_id);
  EXPECT_EQ(fixture.voice().slurs()[0].start_event, new_top_level_id);
  EXPECT_EQ(fixture.voice().beam_overrides()[0].events[0], new_top_level_id);
  EXPECT_EQ(fixture.voice().grace_groups()[0].principal_event,
            new_top_level_id);
}

// ---- Generated notehead ID capture across undo/redo ----

TEST(NoteEntryTest, NoteToChordCapturesGeneratedChordNoteId) {
  Fixture            fixture;
  const SpelledPitch c        = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e        = *SpelledPitch::create(Letter::kE, 4);
  const Note         original = append_quarter_note(fixture, c);
  fixture.normalize_voice();

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, e);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  const Chord& chord = std::get<Chord>(fixture.voice().events().front());
  ASSERT_EQ(chord.notes.size(), 2u);
  const NotationEntityId generated_id = chord.notes[1].id;
  EXPECT_NE(generated_id, NotationEntityId{});
  EXPECT_NE(generated_id, chord.id);
  EXPECT_NE(generated_id, chord.notes[0].id);

  // Undo → Note; redo → Chord. The generated notehead must be the same.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  const Chord& rechord = std::get<Chord>(fixture.voice().events().front());
  EXPECT_EQ(rechord.notes[1].id, generated_id);
}

TEST(NoteEntryTest, ChordExtensionCapturesGeneratedChordNoteId) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);
  append_whole_rest(fixture);
  const Chord original = make_chord(*Duration::create(NoteValue::kQuarter, 0),
                                    {{NotationEntityId::generate(), c, false},
                                     {NotationEntityId::generate(), e, false}});
  ASSERT_TRUE(fixture.voice().append(original).ok());
  fixture.normalize_voice();

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kSixteenth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(1), spec, g);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  const Chord& chord = std::get<Chord>(fixture.voice().events()[1]);
  ASSERT_EQ(chord.notes.size(), 3u);
  const NotationEntityId generated_id = chord.notes[2].id;
  EXPECT_NE(generated_id, NotationEntityId{});
  EXPECT_NE(generated_id, chord.id);
  EXPECT_NE(generated_id, chord.notes[0].id);
  EXPECT_NE(generated_id, chord.notes[1].id);

  // Undo → 2-note chord; redo → 3-note chord. Generated ID preserved.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  const Chord& rechord = std::get<Chord>(fixture.voice().events()[1]);
  EXPECT_EQ(rechord.notes[2].id, generated_id);
}

// ---- Duration replacement: automatic-rest normalization ----

TEST(NoteEntryTest, ContractionAtStartFillsGapWithDecomposedRests) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  // Whole note at position 0, plus a later sounding event at position 1.
  Note note = make_note(pitch, *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());
  const Note later = make_note(*SpelledPitch::create(Letter::kE, 4),
                               *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(later).ok());
  fixture.normalize_voice();

  const NotationEntityId later_id    = later.id;
  const Rational         later_onset = Rational(1);

  // Arm quarter note and click on C4.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  // First event is now a quarter note.
  const VoiceEvent& ev0 = fixture.voice().events().front();
  ASSERT_TRUE(std::holds_alternative<Note>(ev0));
  EXPECT_EQ(std::get<Note>(ev0).duration.base(), NoteValue::kQuarter);

  // The later sounding event must still be at onset 1 (exactly).
  const auto pos = fixture.voice().position_of_event(later_id);
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, later_onset);

  // The gap between the quarter note and the later event must be filled
  // with decompose_rest(3/4): one double-dotted-half rest.
  // Verify that the events between the replacement and the later note are
  // all rests.
  Rational cumulative = *Rational::create(1, 4);
  for (std::size_t i = 1; i < fixture.voice().events().size(); ++i) {
    const VoiceEvent& ev = fixture.voice().events()[i];
    if (!std::holds_alternative<Rest>(ev)) {
      // This is the later sounding event.
      const auto ev_pos = fixture.voice().position_of_event(event_id(ev));
      ASSERT_TRUE(ev_pos.has_value());
      EXPECT_EQ(*ev_pos, cumulative);
      break;
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_EQ(cumulative, later_onset);

  // Voice total length must still match node_end.
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
}

TEST(NoteEntryTest, ContractionInMiddlePreservesEveryLaterOnset) {
  Fixture            fixture;
  const SpelledPitch e = *SpelledPitch::create(Letter::kE, 4);
  const SpelledPitch g = *SpelledPitch::create(Letter::kG, 4);

  // Build: rest(1/4), note_E(1/2), note_G(1/4), normalize.
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kQuarter, 0)))
                  .ok());
  const Note mid_note = make_note(e, *Duration::create(NoteValue::kHalf, 0));
  ASSERT_TRUE(fixture.voice().append(mid_note).ok());
  const Note end_note = make_note(g, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(end_note).ok());
  fixture.normalize_voice();

  const Rational         mid_onset   = *Rational::create(1, 4);
  const Rational         end_onset   = *Rational::create(3, 4);
  const NotationEntityId end_note_id = end_note.id;

  // Arm eighth note, click on E4 at mid position.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), mid_onset, spec, e);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  // The end note must still be at onset 3/4.
  const auto end_pos = fixture.voice().position_of_event(end_note_id);
  ASSERT_TRUE(end_pos.has_value());
  EXPECT_EQ(*end_pos, end_onset);

  // Voice exactly tiles node_end.
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
}

TEST(NoteEntryTest, ContractionAtEndDoesNotShiftEarlierEvents) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);

  // Build: note_C(1/4), whole rest(1), normalize.
  const Note first = append_quarter_note(fixture, c);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  fixture.normalize_voice();

  const NotationEntityId first_id = first.id;

  // Arm an eighth note and replace the rest at onset 1/4 (the whole rest).
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
  auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                     fixture.track(), fixture.stave_id(),
                                     *Rational::create(1, 4), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  // The first event (at onset 0) must remain unchanged.
  const auto first_pos = fixture.voice().position_of_event(first_id);
  ASSERT_TRUE(first_pos.has_value());
  EXPECT_EQ(*first_pos, Rational(0));

  // Voice still tiles node_end.
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
}

TEST(NoteEntryTest, ExpansionConsumesFollowRestsSuccessfully) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);

  // Build: eighth_note, eighth_rest, eighth_rest, normalize.
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(c, *Duration::create(NoteValue::kEighth, 0)))
          .ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kEighth, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kEighth, 0)))
                  .ok());
  fixture.normalize_voice();

  // Arm quarter note, click on C4 at position 0.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  // The note should now be a quarter note (consumed one eighth rest).
  const VoiceEvent& ev0 = fixture.voice().events().front();
  ASSERT_TRUE(std::holds_alternative<Note>(ev0));
  EXPECT_EQ(std::get<Note>(ev0).duration.base(), NoteValue::kQuarter);

  // Voice tiles node_end.
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
}

TEST(NoteEntryTest, ExpansionConsumesMultipleSeparateRests) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);

  // Build: eighth_note, four eighth_rests, normalize.
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(c, *Duration::create(NoteValue::kEighth, 0)))
          .ok());
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(fixture.voice()
                    .append(make_rest(*Duration::create(NoteValue::kEighth, 0)))
                    .ok());
  }
  fixture.normalize_voice();
  const std::size_t orig_count = fixture.voice().events().size();

  // Arm dotted quarter note (3/8), click on C4 at position 0.
  const NotePaletteEntrySpec spec(
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote));
  // We need a dotted quarter.  Construct manually.
  const NotePaletteState state = *NotePaletteState::create(
      NoteValue::kQuarter, 1, NotePaletteEntryKind::kNote, *Voice::create(1));
  const NotePaletteEntrySpec dotted_spec = state.next_entry_spec();
  // dotted quarter = 3/8, consumes two eighth rests.

  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), dotted_spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  // Two rests consumed → count decreased by 2.
  EXPECT_EQ(fixture.voice().events().size(), orig_count - 2);
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
}

TEST(NoteEntryTest, ContractionUndoRedoExactRoundTrip) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  // Whole note, normalize to 2 measures (8/4).
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(pitch, *Duration::create(NoteValue::kWhole, 0)))
          .ok());
  fixture.normalize_voice();
  const VoiceContent pre_snapshot = fixture.voice();

  // Arm half note, click on C4 at position 0.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  // After contraction: different from pre.
  EXPECT_NE(fixture.voice(), pre_snapshot);

  // Capture post-execute state before undo.
  const VoiceContent post_snapshot = fixture.voice();

  // Undo must exactly restore pre state.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre_snapshot);

  // Redo must return exactly to post-contraction state.
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), post_snapshot);

  // Second undo restores again.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre_snapshot);
}

TEST(NoteEntryTest, ExpansionUndoRedoExactRoundTrip) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  // Eighth note + two eighth rests, normalize.
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(c, *Duration::create(NoteValue::kEighth, 0)))
          .ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kEighth, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kEighth, 0)))
                  .ok());
  fixture.normalize_voice();
  const VoiceContent pre_snapshot = fixture.voice();

  // Arm quarter note, click on C4 at position 0.
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  EXPECT_NE(fixture.voice(), pre_snapshot);

  // Capture post-execute state before undo.
  const VoiceContent post_snapshot = fixture.voice();

  // Undo must exactly restore pre state.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre_snapshot);

  // Redo must return exactly to post-expansion state.
  EXPECT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), post_snapshot);

  // Final undo restores pre state again.
  EXPECT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre_snapshot);
}

// ---- Property-style generated valid measures: non-overlap and tile ----

// Deterministic coverage matrix: for a set of valid voice configurations
// and replacement operations, verify every success leaves the voice
// non-overlapping and exactly tiled.
TEST(NoteEntryTest, ReplacementInvariantsPropertyCoverage) {
  struct VoiceCase {
    std::string             description;
    std::vector<VoiceEvent> events;
  };

  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d = *SpelledPitch::create(Letter::kD, 4);

  const std::vector<VoiceCase> voices = {
      {"single whole note",
       {make_note(c, *Duration::create(NoteValue::kWhole, 0))}},
      {"note then rests",
       {make_note(c, *Duration::create(NoteValue::kQuarter, 0)),
        make_rest(*Duration::create(NoteValue::kQuarter, 0)),
        make_rest(*Duration::create(NoteValue::kHalf, 0))}},
      {"rests surrounding note",
       {make_rest(*Duration::create(NoteValue::kQuarter, 0)),
        make_note(d, *Duration::create(NoteValue::kHalf, 0)),
        make_rest(*Duration::create(NoteValue::kQuarter, 0))}},
      {"note at end after rests",
       {make_rest(*Duration::create(NoteValue::kHalf, 0)),
        make_rest(*Duration::create(NoteValue::kQuarter, 0)),
        make_note(c, *Duration::create(NoteValue::kQuarter, 0))}},
  };

  // For each voice case: try replacing at selected positions with
  // durations that are known-safe (same or shorter than the target event).
  for (const auto& vc : voices) {
    Fixture fixture;

    Rational                                           cumulative(0);
    std::vector<std::pair<Rational, NotationEntityId>> event_positions;
    for (const auto& ev : vc.events) {
      ASSERT_TRUE(fixture.voice().append(ev).ok())
          << vc.description << ": append failed";
      event_positions.emplace_back(cumulative, event_id(ev));
      cumulative = cumulative + event_duration(ev).resolved();
    }
    fixture.normalize_voice();

    for (std::size_t idx = 0; idx < vc.events.size(); ++idx) {
      const VoiceContent pre_snapshot = fixture.voice();
      const Rational     pos          = event_positions[idx].first;
      const Rational     old_dur_resolved =
          event_duration(vc.events[idx]).resolved();

      // Determine replacement pitch.
      const auto* old_note_ptr = std::get_if<Note>(&vc.events[idx]);
      const auto* old_chord_ptr =
          !old_note_ptr ? std::get_if<Chord>(&vc.events[idx]) : nullptr;
      const SpelledPitch repl_pitch =
          old_note_ptr != nullptr
              ? old_note_ptr->pitch
              : (old_chord_ptr != nullptr ? old_chord_ptr->notes[0].pitch : c);

      // --- Same-duration replacement ---
      // Build a spec with the exact same duration as the existing event.
      {
        const Duration&        orig_dur = event_duration(vc.events[idx]);
        const NotePaletteState state    = *NotePaletteState::create(
            orig_dur.base(), orig_dur.dots(), NotePaletteEntryKind::kNote,
            *Voice::create(1));
        const NotePaletteEntrySpec same_spec = state.next_entry_spec();

        auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                           fixture.track(), fixture.stave_id(),
                                           pos, same_spec, repl_pitch);
        ASSERT_NE(cmd, nullptr) << vc.description << " same-dur idx " << idx;
        ASSERT_TRUE(cmd->execute(fixture.project).ok())
            << vc.description << " same-dur idx " << idx;
        EXPECT_EQ(fixture.voice().total_length(), fixture.node_end())
            << vc.description << " same-dur tile idx " << idx;
        EXPECT_TRUE(cmd->undo(fixture.project).ok());
        EXPECT_EQ(fixture.voice(), pre_snapshot)
            << vc.description << " same-dur undo idx " << idx;
        EXPECT_TRUE(cmd->redo(fixture.project).ok());
        EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
        EXPECT_TRUE(cmd->undo(fixture.project).ok());
        ASSERT_EQ(fixture.voice(), pre_snapshot);
      }

      // --- Contraction: try a shorter duration ---
      if (old_dur_resolved >= *Rational::create(1, 4)) {
        const NotePaletteEntrySpec short_spec =
            armed(NoteValue::kEighth, NotePaletteEntryKind::kNote);
        auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                           fixture.track(), fixture.stave_id(),
                                           pos, short_spec, repl_pitch);
        ASSERT_NE(cmd, nullptr) << vc.description << " contraction idx " << idx;
        ASSERT_TRUE(cmd->execute(fixture.project).ok())
            << vc.description << " contraction idx " << idx;
        EXPECT_EQ(fixture.voice().total_length(), fixture.node_end())
            << vc.description << " contraction tile idx " << idx;
        EXPECT_TRUE(cmd->undo(fixture.project).ok());
        EXPECT_EQ(fixture.voice(), pre_snapshot)
            << vc.description << " contraction undo idx " << idx;
      }

      // --- Expansion: try when the current event is a Note and a rest
      //     follows (genuine new_dur > old_dur). ------------------------------
      if (old_note_ptr != nullptr && idx + 1 < vc.events.size() &&
          std::holds_alternative<Rest>(vc.events[idx + 1])) {
        // Compute a genuine larger Duration: next undotted base up (unless
        // the event is already Whole — no larger undotted base exists).
        const NoteValue old_base = old_note_ptr->duration.base();
        if (old_base > NoteValue::kWhole) {
          const NoteValue larger_base =
              static_cast<NoteValue>(static_cast<std::uint8_t>(old_base) - 1);
          const std::optional<Duration> larger_dur =
              Duration::create(larger_base, 0);
          ASSERT_TRUE(larger_dur.has_value());
          const Rational new_dur  = larger_dur->resolved();
          const Rational required = new_dur - old_dur_resolved;

          // Sum consecutive rests after idx.
          Rational consecutive_rest(0);
          for (std::size_t k = idx + 1; k < vc.events.size(); ++k) {
            if (!std::holds_alternative<Rest>(vc.events[k]))
              break;
            consecutive_rest =
                consecutive_rest + event_duration(vc.events[k]).resolved();
          }
          const bool should_succeed =
              (required > Rational(0) && consecutive_rest >= required);

          // Restore pre state.
          fixture.voice() = pre_snapshot;
          ASSERT_EQ(fixture.voice(), pre_snapshot);

          const NotePaletteEntrySpec expand_spec =
              armed(larger_base, NotePaletteEntryKind::kNote);
          auto cmd = make_note_entry_command(
              fixture.project, fixture.node_id, fixture.track(),
              fixture.stave_id(), pos, expand_spec, repl_pitch);
          ASSERT_NE(cmd, nullptr) << vc.description << " expansion idx " << idx;
          const bool ok = cmd->execute(fixture.project).ok();
          EXPECT_EQ(ok, should_succeed)
              << vc.description << " expansion outcome idx " << idx;
          if (ok) {
            EXPECT_EQ(fixture.voice().total_length(), fixture.node_end())
                << vc.description << " expansion tile idx " << idx;
            EXPECT_TRUE(cmd->undo(fixture.project).ok());
            EXPECT_EQ(fixture.voice(), pre_snapshot)
                << vc.description << " expansion undo idx " << idx;
          } else {
            EXPECT_EQ(fixture.voice(), pre_snapshot)
                << vc.description << " expansion atomicity idx " << idx;
          }
        }
      }
    }
  }
}

// ---- Armed palette markings remain unapplied ----

TEST(NoteEntryTest, ArmedPaletteMarkingsNotAppliedOnNoteEntry) {
  Fixture            fixture;
  const SpelledPitch c = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, c);
  fixture.normalize_voice();

  // Build an entry spec carrying every deferred marking so we can prove
  // make_note_entry_command applies none of them -- structural editing is
  // a separate phase.
  NotePaletteState palette = *NotePaletteState::create(
      NoteValue::kHalf, 0, NotePaletteEntryKind::kNote, *Voice::create(1));
  palette = *palette.with_articulation_armed(graphscore::Articulation::kAccent);
  palette = palette.with_dynamic(Dynamic::kF);
  palette = palette.with_hairpin_direction(HairpinDirection::kCrescendo);
  palette = palette.with_tie_to_next_armed(true);
  palette = palette.with_slur_armed(true);
  palette = palette.with_pedal_armed(true);
  palette =
      palette.with_beam_override_kind(graphscore::BeamOverride::Kind::kJoin);
  const NotePaletteEntrySpec spec = palette.next_entry_spec();

  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, c);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  const Note& updated = std::get<Note>(fixture.voice().events().front());
  EXPECT_EQ(updated.duration.base(), NoteValue::kHalf);
  // Palette articulations were not applied.
  EXPECT_TRUE(updated.articulations.empty());
  // Palette tie was not applied.
  EXPECT_FALSE(updated.tied_to_next);

  // No dynamics, hairpins, slurs, or beam overrides were added.
  EXPECT_TRUE(fixture.voice().dynamics().empty());
  EXPECT_TRUE(fixture.voice().hairpins().empty());
  EXPECT_TRUE(fixture.voice().slurs().empty());
  EXPECT_TRUE(fixture.voice().beam_overrides().empty());

  // No pedal span was added for the stave.
  EXPECT_EQ(fixture.project.find_node(fixture.node_id)
                ->lane(fixture.track())
                ->pedal_spans(fixture.stave_id()),
            nullptr);
}

// ---- Explicit voice-stream workflow: entry into a never-touched voice ----

// The composer's first click into an armed but never-touched voice must
// place the note and materialize the rest of the voice as normalized
// rests, in one undoable action -- the empty-voice path returns a
// CommandTransaction, never a bare SetEventCommand.
TEST(NoteEntryTest, FirstClickIntoAnEmptyVoiceCreatesTheStreamAndTheNote) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  ASSERT_TRUE(fixture.voice(2).events().empty());

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_NE(dynamic_cast<CommandTransaction*>(cmd.get()), nullptr);

  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  ASSERT_FALSE(fixture.voice(2).events().empty());
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice(2).events().front()));
  const Note& note = std::get<Note>(fixture.voice(2).events().front());
  EXPECT_EQ(note.pitch, pitch);
  EXPECT_EQ(note.duration.base(), NoteValue::kQuarter);
  EXPECT_EQ(fixture.voice(2).total_length(), fixture.node_end());
}

// One undo removes both the note and the whole voice: the voice returns to
// completely empty, exactly as if the composer had never clicked.
TEST(NoteEntryTest, UndoAfterVoiceCreationReturnsToCompletelyEmpty) {
  Fixture                    fixture;
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());
  ASSERT_FALSE(fixture.voice(2).events().empty());

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  EXPECT_EQ(fixture.voice(2), VoiceContent{});
}

// Redo reproduces the exact same fill and note, id for id.
TEST(NoteEntryTest, RedoAfterVoiceCreationIsIdForIdIdentical) {
  Fixture                    fixture;
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());
  const VoiceContent post_snapshot = fixture.voice(2);

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  ASSERT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(2), post_snapshot);

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  ASSERT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(2), post_snapshot);
}

// CommandHistory, not a bare execute()/undo()/redo() call, is the real
// caller in the app: one execute_new() through it must place the note and
// materialize the stream as a single undo/redo-stack entry, including the
// ordinary redo-stack-clearing interaction a fresh execute_new() triggers.
TEST(NoteEntryTest, VoiceCreationTransactionRoundTripsThroughCommandHistory) {
  Fixture                    fixture;
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(cmd), fixture.project).ok());
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  ASSERT_FALSE(fixture.voice(2).events().empty());
  const VoiceContent post_snapshot = fixture.voice(2);

  EXPECT_TRUE(history.undo(fixture.project).ok());
  EXPECT_FALSE(history.can_undo());
  EXPECT_TRUE(history.can_redo());
  EXPECT_TRUE(fixture.voice(2).events().empty());

  EXPECT_TRUE(history.redo(fixture.project).ok());
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  EXPECT_EQ(fixture.voice(2), post_snapshot);

  // A fresh execute_new() after an undo clears the redo stack, same as any
  // other command through CommandHistory.
  EXPECT_TRUE(history.undo(fixture.project).ok());
  ASSERT_TRUE(history.can_redo());
  auto second_cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(second_cmd, nullptr);
  EXPECT_TRUE(history.execute_new(std::move(second_cmd), fixture.project).ok());
  EXPECT_FALSE(history.can_redo());
  EXPECT_FALSE(fixture.voice(2).events().empty());
}

// Arming a rest duration and clicking an empty voice still materializes
// the stream: the result is a voice of normalized rests, not a no-op.
TEST(NoteEntryTest, RestEntryIntoAnEmptyVoiceStillMaterializesTheStream) {
  Fixture                    fixture;
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kRest, 2);
  auto cmd = make_note_entry_command(fixture.project, fixture.node_id,
                                     fixture.track(), fixture.stave_id(),
                                     Rational(0), spec, std::nullopt);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  ASSERT_FALSE(fixture.voice(2).events().empty());
  ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice(2).events().front()));
  EXPECT_EQ(std::get<Rest>(fixture.voice(2).events().front()).duration.base(),
            NoteValue::kHalf);
  EXPECT_EQ(fixture.voice(2).total_length(), fixture.node_end());
}

// A position that is not an onset of the empty voice's hypothetical fill
// is rejected outright, mutating nothing.
TEST(NoteEntryTest, ReturnsNullptrWhenPositionIsNotAFillOnsetInAnEmptyVoice) {
  Fixture                    fixture;
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  // node_end() == 2 (two 4/4 measures); the hypothetical fill is two whole
  // rests, at onsets 0 and 1 -- position 1/2 is neither.
  auto cmd = make_note_entry_command(
      fixture.project, fixture.node_id, fixture.track(), fixture.stave_id(),
      *Rational::create(1, 2), spec, *SpelledPitch::create(Letter::kC, 4));
  EXPECT_EQ(cmd, nullptr);
  EXPECT_TRUE(fixture.voice(2).events().empty());
}

// The documented "either succeeds completely or leaves the project
// untouched" claim is non-trivial exactly when CreateVoiceStreamCommand
// succeeds but the chained SetEventCommand then fails: node_end() == 2 (two
// 4/4 measures), so the hypothetical fill is two whole rests at onsets 0
// and 1. Arming a dotted whole (3/2) at position 1 (the second whole rest,
// the voice's last event) needs 1/2 beyond node_end() with no later event
// to cover the growth, so SetEventCommand rejects the expansion atomically.
// The whole transaction must roll back to a completely empty voice, never a
// "voice materialized but noteless" half-state.
TEST(NoteEntryTest,
     FailedChainedSetEventLeavesTheVoiceCompletelyEmptyNotHalfMaterialized) {
  Fixture fixture;
  ASSERT_TRUE(fixture.voice(2).events().empty());

  const NotePaletteState state = *NotePaletteState::create(
      NoteValue::kWhole, 1, NotePaletteEntryKind::kNote, *Voice::create(2));
  const NotePaletteEntrySpec spec  = state.next_entry_spec();
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);

  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(1), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  ASSERT_NE(dynamic_cast<CommandTransaction*>(cmd.get()), nullptr);

  EXPECT_FALSE(cmd->execute(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  EXPECT_EQ(fixture.voice(2), VoiceContent{});
}

// The non-empty path's own behavior is unchanged by this feature: it still
// returns a bare SetEventCommand, never a transaction.
TEST(NoteEntryTest, NonEmptyVoicePathStillReturnsABareSetEventCommand) {
  Fixture            fixture;
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, pitch);
  fixture.normalize_voice();
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_NE(dynamic_cast<SetEventCommand*>(cmd.get()), nullptr);
  EXPECT_EQ(dynamic_cast<CommandTransaction*>(cmd.get()), nullptr);
}

// Equivalence: entering into an empty voice must be semantically identical
// to explicitly rest-filling that voice first (via the same shared
// decompose_measure_aligned_rests helper preview_note_entry itself uses)
// and then performing the ordinary non-empty entry. This is what
// guarantees the two paths can never drift apart.
TEST(NoteEntryTest,
     EmptyVoiceEntryIsSemanticallyEquivalentToExplicitFillThenEntry) {
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);

  // Path A: enter directly into the empty voice.
  Fixture fixture_a;

  auto cmd_a = make_note_entry_command(fixture_a.project, fixture_a.node_id,
                                       fixture_a.track(), fixture_a.stave_id(),
                                       Rational(0), spec, pitch);
  ASSERT_NE(cmd_a, nullptr);
  ASSERT_TRUE(cmd_a->execute(fixture_a.project).ok());

  // Path B: explicitly rest-fill the voice first with the same shared
  // helper, then perform the ordinary non-empty entry at the same onset.
  Fixture    fixture_b;
  const auto fill = decompose_measure_aligned_rests(
      *fixture_b.project.find_node(fixture_b.node_id)->timeline());
  ASSERT_TRUE(fill.has_value());
  for (const Rest& rest : *fill) {
    ASSERT_TRUE(fixture_b.voice(2).append(rest).ok());
  }
  auto cmd_b = make_note_entry_command(fixture_b.project, fixture_b.node_id,
                                       fixture_b.track(), fixture_b.stave_id(),
                                       Rational(0), spec, pitch);
  ASSERT_NE(cmd_b, nullptr);
  ASSERT_TRUE(cmd_b->execute(fixture_b.project).ok());

  // Both voices carry the same shape: the same event count, kind and
  // duration at every position. IDs necessarily differ -- each command
  // minted its own fresh ids for both the fill and the note.
  const auto& events_a = fixture_a.voice(2).events();
  const auto& events_b = fixture_b.voice(2).events();
  ASSERT_EQ(events_a.size(), events_b.size());
  for (std::size_t i = 0; i < events_a.size(); ++i) {
    EXPECT_EQ(events_a[i].index(), events_b[i].index()) << "event " << i;
    EXPECT_EQ(event_duration(events_a[i]), event_duration(events_b[i]))
        << "event " << i;
  }
  ASSERT_TRUE(std::holds_alternative<Note>(events_a.front()));
  ASSERT_TRUE(std::holds_alternative<Note>(events_b.front()));
  EXPECT_EQ(std::get<Note>(events_a.front()).pitch,
            std::get<Note>(events_b.front()).pitch);
}

// The equivalence above alone is not meaningful proof of the shared
// measure-aligned tiling: on 4/4 the measure-aligned fill and a naive
// decompose_rest(node_end()) fill are identical (two whole rests), so that
// test only proves the two paths call *a* shared helper. Re-run on three
// 3/4 measures (node_end() == 9/4), where the two fills genuinely differ
// (ThreeThreeQuarterMeasuresRespectEveryBarline, voice_content_test.cpp),
// to prove they still agree once barline alignment actually matters.
TEST(NoteEntryTest,
     EmptyVoiceEntryMatchesExplicitFillThenEntryOnThreeThreeQuarterMeasures) {
  const SpelledPitch         pitch = *SpelledPitch::create(Letter::kC, 4);
  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  const std::vector<Measure> measures = {measure(3, 4), measure(3, 4),
                                         measure(3, 4)};

  Fixture      fixture_a(measures);
  const NodeId node_a = fixture_a.node_id;
  const auto   cmd_a =
      make_note_entry_command(fixture_a.project, node_a, fixture_a.track(),
                              fixture_a.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd_a, nullptr);
  ASSERT_TRUE(cmd_a->execute(fixture_a.project).ok());

  Fixture    fixture_b(measures);
  const auto fill = decompose_measure_aligned_rests(
      *fixture_b.project.find_node(fixture_b.node_id)->timeline());
  ASSERT_TRUE(fill.has_value());
  for (const Rest& rest : *fill) {
    ASSERT_TRUE(fixture_b.voice(2).append(rest).ok());
  }
  auto cmd_b = make_note_entry_command(fixture_b.project, fixture_b.node_id,
                                       fixture_b.track(), fixture_b.stave_id(),
                                       Rational(0), spec, pitch);
  ASSERT_NE(cmd_b, nullptr);
  ASSERT_TRUE(cmd_b->execute(fixture_b.project).ok());

  const auto& events_a = fixture_a.voice(2).events();
  const auto& events_b = fixture_b.voice(2).events();
  ASSERT_EQ(events_a.size(), events_b.size());
  for (std::size_t i = 0; i < events_a.size(); ++i) {
    EXPECT_EQ(events_a[i].index(), events_b[i].index()) << "event " << i;
    EXPECT_EQ(event_duration(events_a[i]), event_duration(events_b[i]))
        << "event " << i;
  }
  EXPECT_EQ(fixture_a.voice(2).total_length(), fixture_a.node_end());
}

// The composer's first click into an empty voice on a meter where the
// measure-aligned fill genuinely differs from a naive whole-span
// decomposition: three 3/4 measures (node_end() == 9/4) tile as three
// dotted-half rests when decomposed per-measure, never the
// dotted-whole-plus-dotted-half a naive decompose_rest(node_end()) call
// would produce, which would straddle the first barline. Proven through the
// full make_note_entry_command path, not just at the
// decompose_measure_aligned_rests unit level
// (ThreeThreeQuarterMeasuresRespectEveryBarline, voice_content_test.cpp).
TEST(NoteEntryTest, FirstClickOnThreeThreeQuarterMeasuresRespectsEveryBarline) {
  Fixture            fixture({measure(3, 4), measure(3, 4), measure(3, 4)});
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);

  // Arm a dotted half (3/4), exactly the first rest's own duration, so the
  // replacement is same-duration and the three-event shape is preserved
  // unchanged -- isolating the barline-alignment property from any
  // additional gap-filling machinery.
  const NotePaletteState state = *NotePaletteState::create(
      NoteValue::kHalf, 1, NotePaletteEntryKind::kNote, *Voice::create(2));
  const NotePaletteEntrySpec spec = state.next_entry_spec();
  auto                       cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, pitch);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  const auto& events = fixture.voice(2).events();
  ASSERT_EQ(events.size(), 3u);
  ASSERT_TRUE(std::holds_alternative<Note>(events[0]));
  EXPECT_EQ(std::get<Note>(events[0]).duration,
            *Duration::create(NoteValue::kHalf, 1));

  // No event crosses a measure boundary: each of the three events covers
  // exactly one 3/4 measure, so cumulative duration lands on every barline.
  Rational cumulative(0);
  for (const auto& ev : events) {
    EXPECT_EQ(event_duration(ev).resolved(), *Rational::create(3, 4));
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_EQ(cumulative, fixture.node_end());
}

// ---- Isolation extended to the voice-creation path ----

// Creating voice 2 leaves voices 1, 3, and 4 -- already-populated or still
// untouched -- byte-for-byte unchanged. Extends OtherVoicesUnaffected to
// the creation path.
TEST(NoteEntryTest, VoiceCreationLeavesOtherVoicesUnaffected) {
  Fixture            fixture;
  const SpelledPitch c               = *SpelledPitch::create(Letter::kC, 4);
  const SpelledPitch d               = *SpelledPitch::create(Letter::kD, 4);
  const Note         voice1_original = append_quarter_note(fixture, c, 1);
  fixture.normalize_voice(1);
  const VoiceContent voice3_before = fixture.voice(3);
  const VoiceContent voice4_before = fixture.voice(4);
  ASSERT_TRUE(fixture.voice(2).events().empty());

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kHalf, NotePaletteEntryKind::kNote, 2);
  auto cmd =
      make_note_entry_command(fixture.project, fixture.node_id, fixture.track(),
                              fixture.stave_id(), Rational(0), spec, d);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice(1).events().front()));
  EXPECT_EQ(std::get<Note>(fixture.voice(1).events().front()), voice1_original);
  EXPECT_EQ(fixture.voice(3), voice3_before);
  EXPECT_EQ(fixture.voice(4), voice4_before);
  EXPECT_FALSE(fixture.voice(2).events().empty());

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice(2).events().empty());
  ASSERT_TRUE(std::holds_alternative<Note>(fixture.voice(1).events().front()));
  EXPECT_EQ(std::get<Note>(fixture.voice(1).events().front()), voice1_original);
  EXPECT_EQ(fixture.voice(3), voice3_before);
  EXPECT_EQ(fixture.voice(4), voice4_before);
}

// The armed voice is the sole write target for the creation path too:
// arming voice 2 while voice 3 also happens to be empty leaves voice 3
// empty -- creating one voice never creates or disturbs a sibling voice
// the composer did not explicitly select. Extends ArmedVoiceIsOnlyChangedVoice.
TEST(NoteEntryTest, VoiceCreationNeverMaterializesASiblingVoice) {
  Fixture fixture;
  ASSERT_TRUE(fixture.voice(2).events().empty());
  ASSERT_TRUE(fixture.voice(3).events().empty());

  const NotePaletteEntrySpec spec =
      armed(NoteValue::kQuarter, NotePaletteEntryKind::kNote, 2);
  auto cmd = make_note_entry_command(
      fixture.project, fixture.node_id, fixture.track(), fixture.stave_id(),
      Rational(0), spec, *SpelledPitch::create(Letter::kC, 4));
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());

  EXPECT_FALSE(fixture.voice(2).events().empty());
  EXPECT_TRUE(fixture.voice(3).events().empty());
}

// ---- make_move_notehead_command (M5-phase-20) ------------------------------

TEST(NoteEntryTest, MoveNoteheadCommandValidSingleNotehead) {
  Fixture            fixture;
  const SpelledPitch c4       = *SpelledPitch::create(Letter::kC, 4);
  const Note         original = append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};

  auto cmd = make_move_notehead_command(fixture.project, item,
                                        NoteheadStepDirection::kUp);
  ASSERT_NE(cmd, nullptr);
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  const Note& moved = std::get<Note>(fixture.voice().events().front());
  EXPECT_EQ(moved.id, original.id);
  EXPECT_EQ(moved.pitch, *SpelledPitch::create(Letter::kD, 4));
}

TEST(NoteEntryTest, MoveNoteheadCommandStaleNoteheadReturnsNull) {
  Fixture            fixture;
  const SpelledPitch c4 = *SpelledPitch::create(Letter::kC, 4);
  append_quarter_note(fixture, c4);
  fixture.normalize_voice();

  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), NotationEntityId::generate()};
  EXPECT_EQ(make_move_notehead_command(fixture.project, item,
                                       NoteheadStepDirection::kUp),
            nullptr);
}

TEST(NoteEntryTest, MoveNoteheadCommandWrongKindReturnsNull) {
  Fixture    fixture;
  const Rest original = append_whole_rest(fixture);
  // A top-level Rest id is not a notehead identity.
  const NoteheadItem item{fixture.node_id, fixture.track(), fixture.stave_id(),
                          *Voice::create(1), original.id};
  EXPECT_EQ(make_move_notehead_command(fixture.project, item,
                                       NoteheadStepDirection::kUp),
            nullptr);
}

}  // namespace
