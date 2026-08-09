// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace {

using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Command;
using graphscore::CommandHistory;
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

[[nodiscard]] Measure measure() {
  return Measure{*TimeSignature::create(4, 4), graphscore::KeySignature{}};
}

struct Fixture {
  Project project{ProjectId::generate(), "Entry"};
  NodeId  node_id;
  TrackId track_id;

  Fixture() {
    const auto added = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
    EXPECT_TRUE(added.has_value());
    track_id   = *added;
    node_id    = project.add_node("Node");
    auto* lane = project.find_node(node_id)->lane(track_id);
    lane->ensure_stave(stave_id());
    auto timeline =
        NodeTimeline::create({measure(), measure()},
                             {project.active_tracks()[0].layout().staves()[0]});
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

}  // namespace
