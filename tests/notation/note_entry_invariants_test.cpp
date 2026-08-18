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

}  // namespace
