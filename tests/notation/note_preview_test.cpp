// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using graphscore::Clef;
using graphscore::decompose_rest;
using graphscore::Duration;
using graphscore::GlyphCommand;
using graphscore::GlyphMetrics;
using graphscore::GlyphMetricsValue;
using graphscore::KeySignature;
using graphscore::layout_notation;
using graphscore::Letter;
using graphscore::make_note;
using graphscore::make_rest;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationLayout;
using graphscore::NotationLayoutOptions;
using graphscore::NotationPoint;
using graphscore::NotationPreview;
using graphscore::NotationRect;
using graphscore::Note;
using graphscore::NotePaletteEntryKind;
using graphscore::NotePaletteState;
using graphscore::NoteValue;
using graphscore::preview_note_entry;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::smufl_codepoint;
using graphscore::SmuflGlyph;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::Voice;
using graphscore::VoiceContent;

class FixedMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return GlyphMetricsValue{
        NotationRect{-staff_space * 0.25, -staff_space * 0.5, staff_space * 1.5,
                     staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

[[nodiscard]] Measure measure(std::uint8_t  numerator   = 4,
                              std::uint16_t denominator = 4) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 KeySignature{}};
}

struct Fixture {
  Project project{ProjectId::generate(), "Preview"};
  NodeId  node_id;
  TrackId track_id;

  explicit Fixture(std::size_t measure_count, Clef clef = Clef::kTreble)
      : Fixture(std::vector<Measure>(measure_count, measure()),
                StaffLayout::single_staff(clef)) {}

  Fixture(std::vector<Measure> measures, StaffLayout layout) {
    const auto staves = layout.staves();
    const auto added =
        project.add_track("Track", std::move(layout), *MidiChannel::create(0));
    EXPECT_TRUE(added.has_value());
    track_id   = *added;
    node_id    = project.add_node("Node");
    auto* lane = project.find_node(node_id)->lane(track_id);
    for (const auto& stave : staves) {
      lane->ensure_stave(stave.id);
    }
    auto timeline = NodeTimeline::create(std::move(measures), staves);
    EXPECT_TRUE(timeline.has_value());
    project.find_node(node_id)->set_timeline(std::move(*timeline));
  }

  [[nodiscard]] StaveId stave_id(std::size_t stave_index = 0) const {
    return project.active_tracks()[0].layout().staves()[stave_index].id;
  }

  [[nodiscard]] VoiceContent& voice(std::uint8_t voice_index = 1,
                                    std::size_t  stave_index = 0) {
    return project.find_node(node_id)
        ->lane(track_id)
        ->stave(stave_id(stave_index))
        ->voice(*Voice::create(voice_index));
  }
};

[[nodiscard]] NotationLayout require_layout(
    const graphscore::NotationLayoutResult& result) {
  EXPECT_TRUE(result);
  return *result.layout;
}

[[nodiscard]] NotePaletteState note_state(
    std::uint8_t dots = 0, std::uint8_t voice = 1,
    NoteValue            note_value = NoteValue::kQuarter,
    NotePaletteEntryKind kind       = NotePaletteEntryKind::kNote) {
  const auto state =
      NotePaletteState::create(note_value, dots, kind, *Voice::create(voice));
  return *state;
}

// Finds the notehead glyph command for `note` and returns its origin: the
// exact position layout_notation itself placed that pitch at, used as
// ground truth without reaching into notation_engraving.cpp's internal
// pitch<->y formula.
[[nodiscard]] NotationPoint notehead_origin(const NotationLayout& layout,
                                            const Note&           note) {
  const std::string target = note.id.to_string() + "/notehead";
  const auto        found =
      std::ranges::find_if(layout.commands, [&](const auto& command) {
        const auto* glyph = std::get_if<GlyphCommand>(&command);
        return glyph != nullptr && glyph->id.value == target;
      });
  EXPECT_NE(found, layout.commands.end());
  return std::get<GlyphCommand>(*found).origin;
}

// Finds the rest glyph command for `rest` (in the given voice) and returns
// its origin: the exact position layout_notation itself placed that onset
// at, used the same way notehead_origin() is -- ground truth read out of the
// real layout, never notation_measure_math.cpp's internal x<->time formula
// reproduced here.
[[nodiscard]] NotationPoint rest_origin(const NotationLayout& layout,
                                        const Rest&           rest,
                                        std::uint8_t          voice_index = 1) {
  const std::string target =
      rest.id.to_string() + "/voice/" + std::to_string(voice_index) + "/rest";
  const auto found =
      std::ranges::find_if(layout.commands, [&](const auto& command) {
        const auto* glyph = std::get_if<GlyphCommand>(&command);
        return glyph != nullptr && glyph->id.value == target;
      });
  EXPECT_NE(found, layout.commands.end());
  return std::get<GlyphCommand>(*found).origin;
}

[[nodiscard]] NotationPoint staff_center(const NotationLayout& layout,
                                         std::size_t measure_index = 0) {
  const auto& staff = layout.systems[0].staves[0];
  return NotationPoint{staff.measure_bounds[measure_index].x +
                           staff.measure_bounds[measure_index].width * 0.5,
                       staff.bounds.y + staff.bounds.height * 0.5};
}

// ---- Basic candidate pitch/onset/rest semantics ----

TEST(NotePreviewTest, NotePreviewLandsAtCandidatePitchAndOnlyOnset) {
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(pitch, *Duration::create(NoteValue::kWhole, 0)))
          .ok());
  const Note note = std::get<Note>(fixture.voice().events().back());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note);

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  ASSERT_TRUE(preview->candidate_pitch.has_value());
  EXPECT_EQ(*preview->candidate_pitch, pitch);
  EXPECT_EQ(preview->candidate_onset, Rational(0));
  EXPECT_EQ(preview->entry_kind, NotePaletteEntryKind::kNote);
  EXPECT_EQ(preview->voice, *Voice::create(1));
  EXPECT_EQ(preview->stave_id, fixture.stave_id());
  EXPECT_EQ(preview->track_id, fixture.track_id);
  EXPECT_FALSE(preview->commands.empty());
}

TEST(NotePreviewTest, RestPreviewUsesRestGlyphAndLeavesPitchUnset) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_FALSE(preview->candidate_pitch.has_value());
  EXPECT_EQ(preview->entry_kind, NotePaletteEntryKind::kRest);
  ASSERT_EQ(preview->commands.size(), 1u);
  const auto* glyph = std::get_if<GlyphCommand>(&preview->commands.front());
  ASSERT_NE(glyph, nullptr);
  EXPECT_EQ(glyph->code_point, smufl_codepoint(SmuflGlyph::kRestQuarter));
}

TEST(NotePreviewTest, DotsFromPaletteAppearAsAugmentationDotCommands) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto preview = preview_note_entry(
      fixture.project, layout,
      note_state(2, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  ASSERT_TRUE(preview.has_value());
  std::size_t dot_count = 0;
  for (const auto& command : preview->commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph != nullptr &&
        glyph->code_point == smufl_codepoint(SmuflGlyph::kAugmentationDot)) {
      ++dot_count;
    }
  }
  EXPECT_EQ(dot_count, 2u);
}

// ---- Onset resolution: armed voice, empty voice, whole-measure rest ----

TEST(NotePreviewTest, ArmedVoiceChangesTheSnappedOnsetAtTheSamePoint) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  const Duration whole   = *Duration::create(NoteValue::kWhole, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(fixture.voice(1).append(make_rest(quarter)).ok());
  }
  ASSERT_TRUE(fixture.voice(2).append(make_rest(whole)).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto voice1 = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  const auto voice2 = preview_note_entry(
      fixture.project, layout,
      note_state(0, 2, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  ASSERT_TRUE(voice1.has_value());
  ASSERT_TRUE(voice2.has_value());
  // The measure's leading space (clef/key/time-signature glyphs) occupies
  // the head of the bounds before the rhythmic span begins, so the
  // geometric center of the measure's own bounds maps to an onset earlier
  // than the musical midpoint: nearest to voice 1's onset at 1/4, not 1/2.
  EXPECT_EQ(voice1->candidate_onset, *Rational::create(1, 4));
  EXPECT_EQ(voice2->candidate_onset, Rational(0));
  EXPECT_NE(voice1->candidate_onset, voice2->candidate_onset);
}

// Contract change (explicit voice-stream workflow): an entirely empty voice
// used to yield std::nullopt outright; a composer arming Voice 2 and
// clicking could never get a preview or place a note there. It now
// previews as if the voice had already been filled by
// decompose_measure_aligned_rests -- the composer's first click into a
// never-touched voice must show something, not silently reject.
TEST(NotePreviewTest, EmptyVoiceYieldsPreviewAtTheHypotheticalFillOnset) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  // Voice 2 is left empty.
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 2, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->voice, *Voice::create(2));
  // The single-measure timeline's hypothetical fill for voice 2 is one
  // whole-measure rest, so it offers exactly one onset -- 0 -- regardless
  // of where in the measure the click falls, matching the real-voice
  // contract WholeMeasureRestOffersExactlyOneOnsetRegardlessOfClickPosition
  // proves for a voice that already holds that same whole rest.
  EXPECT_EQ(preview->candidate_onset, Rational(0));
}

// Onset selection across a multi-rest hypothetical fill: a 5/8 measure
// decomposes into more than one rest, so the empty voice's hypothetical
// fill offers more than one onset, and the click nearest each one must
// resolve to that onset specifically -- not always the first.
TEST(NotePreviewTest,
     EmptyVoiceOnsetSelectionAcrossAMultiRestHypotheticalFill) {
  Fixture    fixture({measure(5, 8)}, StaffLayout::single_staff());
  const auto shape = decompose_rest(*Rational::create(5, 8));
  ASSERT_TRUE(shape.has_value());
  ASSERT_GE(shape->size(), 2u) << "5/8 must decompose into more than one rest "
                                  "for this test to be meaningful";

  // Voice 1 gets the identical real rest sequence decompose_rest(5/8)
  // produces, so its rendered glyph positions are ground truth for where
  // each onset actually falls -- the same technique
  // EquidistantOnsetsTieBreakToTheEarlierOnset uses above, rather than
  // reproducing the engraver's own x<->time formula in this test.
  for (const Rest& rest : *shape) {
    ASSERT_TRUE(fixture.voice(1).append(rest).ok());
  }
  // Voice 2 is left empty; its hypothetical fill has the same shape.

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint first_point  = rest_origin(layout, (*shape)[0], 1);
  const NotationPoint second_point = rest_origin(layout, (*shape)[1], 1);

  const auto first = preview_note_entry(
      fixture.project, layout,
      note_state(0, 2, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      first_point);
  const auto second = preview_note_entry(
      fixture.project, layout,
      note_state(0, 2, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      second_point);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->candidate_onset, Rational(0));
  EXPECT_EQ(second->candidate_onset, (*shape)[0].duration.resolved());
  EXPECT_NE(first->candidate_onset, second->candidate_onset);
}

// The empty-voice substitution is narrowly scoped to a voice with zero
// events. A voice that already holds content, but has no event boundary
// within the specific resolved measure, is a different case (a short or
// incomplete voice) and keeps the pre-existing std::nullopt behavior.
TEST(NotePreviewTest, NonEmptyVoiceWithNoOnsetInMeasureStillYieldsNoPreview) {
  Fixture fixture(2);
  ASSERT_TRUE(fixture.voice(2)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kQuarter, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  // Measure 1 (ordinal 1): voice 2's only event is a quarter note at 0,
  // entirely within measure 0, so measure 1 has no event boundary in it.
  const NotationPoint point = staff_center(layout, 1);

  const auto preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 2, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  EXPECT_FALSE(preview.has_value());
}

TEST(NotePreviewTest,
     WholeMeasureRestOffersExactlyOneOnsetRegardlessOfClickPosition) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto& staff  = layout.systems[0].staves[0];
  const auto& bounds = staff.measure_bounds[0];

  const NotationPoint near_start{bounds.x + bounds.width * 0.1,
                                 staff.bounds.y + staff.bounds.height * 0.5};
  const NotationPoint near_end{bounds.x + bounds.width * 0.9,
                               staff.bounds.y + staff.bounds.height * 0.5};

  const auto start_preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      near_start);
  const auto end_preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      near_end);
  ASSERT_TRUE(start_preview.has_value());
  ASSERT_TRUE(end_preview.has_value());
  EXPECT_EQ(start_preview->candidate_onset, Rational(0));
  EXPECT_EQ(end_preview->candidate_onset, Rational(0));
}

TEST(NotePreviewTest, EquidistantOnsetsTieBreakToTheEarlierOnset) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kQuarter, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kHalf, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kQuarter, 0)))
                  .ok());
  // Onsets: 0, 1/4, 3/4. A click exactly midway, in x, between the engraved
  // positions of onsets 1/4 and 3/4 is equidistant between them under the
  // engraver's own x<->time mapping (never the layout-agnostic linear
  // interpolation across the whole measure bounds that a naive midpoint of
  // measure.bounds would assume); the earlier onset wins.
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto&         events = fixture.voice().events();
  const NotationPoint onset_quarter =
      rest_origin(layout, std::get<Rest>(events[1]));
  const NotationPoint onset_three_quarters =
      rest_origin(layout, std::get<Rest>(events[2]));
  const NotationPoint point{(onset_quarter.x + onset_three_quarters.x) * 0.5,
                            onset_quarter.y};

  const auto preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->candidate_onset, *Rational::create(1, 4));
}

// ---- x<->time mapping matches the engraver's own leading-space
// reservation (clef/key/time-signature glyphs), never a naive linear split
// of the whole measure bounds. ----

TEST(NotePreviewTest,
     PreviewMatchesEngravedNoteheadXInMeasureZeroWithFullLeading) {
  Fixture            fixture(1);
  const SpelledPitch pitch   = *SpelledPitch::create(Letter::kE, 4);
  const Duration     quarter = *Duration::create(NoteValue::kQuarter, 0);
  ASSERT_TRUE(fixture.voice().append(make_note(pitch, quarter)).ok());
  ASSERT_TRUE(fixture.voice().append(make_note(pitch, quarter)).ok());
  const Note second_note = std::get<Note>(fixture.voice().events()[1]);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, second_note);

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  ASSERT_FALSE(preview->commands.empty());
  const auto* glyph = std::get_if<GlyphCommand>(&preview->commands.front());
  ASSERT_NE(glyph, nullptr);
  EXPECT_DOUBLE_EQ(glyph->origin.x, point.x);
}

TEST(NotePreviewTest, PreviewMatchesEngravedNoteheadXInALaterMeasure) {
  Fixture            fixture(2);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(pitch, *Duration::create(NoteValue::kQuarter, 0)))
          .ok());
  const Note note = std::get<Note>(fixture.voice().events().back());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems[0].measures.size(), 2u);
  const NotationPoint point = notehead_origin(layout, note);

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  ASSERT_FALSE(preview->commands.empty());
  const auto* glyph = std::get_if<GlyphCommand>(&preview->commands.front());
  ASSERT_NE(glyph, nullptr);
  EXPECT_DOUBLE_EQ(glyph->origin.x, point.x);
}

TEST(NotePreviewTest, PreviewMatchesEngravedNoteheadXUnderANonCKeySignature) {
  const KeySignature key = *KeySignature::create(4);  // E major, four sharps.
  Fixture            fixture({Measure{*TimeSignature::create(4, 4), key}},
                             StaffLayout::single_staff(Clef::kTreble));
  const SpelledPitch pitch   = *SpelledPitch::create(Letter::kE, 4);
  const Duration     quarter = *Duration::create(NoteValue::kQuarter, 0);
  ASSERT_TRUE(fixture.voice().append(make_note(pitch, quarter)).ok());
  ASSERT_TRUE(fixture.voice().append(make_note(pitch, quarter)).ok());
  const Note second_note = std::get<Note>(fixture.voice().events()[1]);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, second_note);

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  ASSERT_FALSE(preview->commands.empty());
  const auto* glyph = std::get_if<GlyphCommand>(&preview->commands.front());
  ASSERT_NE(glyph, nullptr);
  EXPECT_DOUBLE_EQ(glyph->origin.x, point.x);
}

TEST(NotePreviewTest, PreviewResolvesAPointInASecondSystemNotJustTheFirst) {
  Fixture fixture(2);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(pitch, *Duration::create(NoteValue::kWhole, 0)))
          .ok());
  const Note note = std::get<Note>(fixture.voice().events().back());

  NotationLayoutOptions options;
  options.system_width = 200.0;
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_EQ(layout.systems.size(), 2u);
  const NotationPoint point = notehead_origin(layout, note);
  EXPECT_GT(point.y,
            layout.systems[0].bounds.y + layout.systems[0].bounds.height);

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  // The note's onset is absolute (across the whole timeline), not relative
  // to its own measure: the second measure starts at onset 1 (the first
  // measure's whole-note rest occupies [0, 1)).
  EXPECT_EQ(preview->candidate_onset, Rational(1));
  EXPECT_EQ(preview->stave_id, fixture.stave_id());
  EXPECT_EQ(preview->track_id, fixture.track_id);
}

// ---- Clef changes mid-node ----

TEST(NotePreviewTest, ClefChangeMidNodeChangesCandidatePitchAtTheSameY) {
  Fixture fixture(2, Clef::kTreble);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(
      fixture.project.find_node(fixture.node_id)
          ->timeline()
          ->add_clef_change(fixture.stave_id(), Rational(1), Clef::kBass)
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems[0].measures.size(), 2u);
  const NotationPoint measure0_point = staff_center(layout, 0);
  const NotationPoint measure1_point = staff_center(layout, 1);

  const auto treble =
      preview_note_entry(fixture.project, layout, note_state(), measure0_point);
  const auto bass =
      preview_note_entry(fixture.project, layout, note_state(), measure1_point);
  ASSERT_TRUE(treble.has_value());
  ASSERT_TRUE(bass.has_value());
  ASSERT_TRUE(treble->candidate_pitch.has_value());
  ASSERT_TRUE(bass->candidate_pitch.has_value());
  EXPECT_NE(*treble->candidate_pitch, *bass->candidate_pitch);
}

// ---- Bucketing/geometry edge cases ----

TEST(NotePreviewTest, PointOutsideEverySystemYieldsNoPreview) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto preview = preview_note_entry(fixture.project, layout, note_state(),
                                          NotationPoint{50'000.0, 50'000.0});
  EXPECT_FALSE(preview.has_value());
}

// ---- Staff proximity gating: a bounded ledger/marking lane around each
// staff, never an unbounded nearest-staff guess across the far larger
// SystemLayout::bounds a dense multi-staff marking budget reserves. ----

TEST(NotePreviewTest,
     PointInTheLedgerLaneAboveOrBelowTheStaffResolvesForNoteAndRest) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto&  staff = layout.systems[0].staves[0];
  const double space = staff.bounds.height / 4.0;
  const double x     = layout.systems[0].measures[0].bounds.x + 10.0;

  for (const double y : {staff.bounds.y - space * 2.0,
                         staff.bounds.y + staff.bounds.height + space * 2.0}) {
    const NotationPoint point{x, y};
    const auto          note_preview =
        preview_note_entry(fixture.project, layout, note_state(), point);
    const auto rest_preview = preview_note_entry(
        fixture.project, layout,
        note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
        point);
    ASSERT_TRUE(note_preview.has_value());
    ASSERT_TRUE(rest_preview.has_value());
    EXPECT_EQ(note_preview->stave_id, fixture.stave_id());
    EXPECT_EQ(rest_preview->stave_id, fixture.stave_id());
  }
}

TEST(NotePreviewTest,
     PointFarBelowTheStaffButInsideTheSystemYieldsNoPreviewForRest) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto&         system = layout.systems[0];
  const auto&         staff  = system.staves[0];
  const double        space  = staff.bounds.height / 4.0;
  const NotationPoint point{
      system.measures[0].bounds.x + 10.0,
      std::min(staff.bounds.y + staff.bounds.height + space * 30.0,
               system.bounds.y + system.bounds.height - 1.0)};
  ASSERT_TRUE(system.bounds.contains(point));

  const auto preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      point);
  EXPECT_FALSE(preview.has_value());
}

TEST(NotePreviewTest, PointNearOneOfTwoStavesResolvesToThatStaffNotTheOther) {
  Fixture fixture({Measure{*TimeSignature::create(4, 4), KeySignature{}}},
                  StaffLayout::grand_staff());
  ASSERT_TRUE(fixture.voice(1, 0)
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice(1, 1)
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems[0].staves.size(), 2u);
  const auto&  top    = layout.systems[0].staves[0];
  const auto&  bottom = layout.systems[0].staves[1];
  const double x      = layout.systems[0].measures[0].bounds.x + 10.0;

  // grand_staff()'s two staves each own the full per-stave marking budget
  // (system_top_padding/staff_slot_height in notation_layout.cpp), which keeps
  // them geometrically far apart; a genuinely equidistant midpoint is
  // unreachable in this layout. This exercises "resolves to the staff whose own
  // ledger/marking lane the point falls in, not the other stave" across a
  // real multi-staff system.
  const NotationPoint near_top{x, top.bounds.y + top.bounds.height * 0.5};
  const NotationPoint near_bottom{x,
                                  bottom.bounds.y + bottom.bounds.height * 0.5};

  const auto top_preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      near_top);
  const auto bottom_preview = preview_note_entry(
      fixture.project, layout,
      note_state(0, 1, NoteValue::kQuarter, NotePaletteEntryKind::kRest),
      near_bottom);
  ASSERT_TRUE(top_preview.has_value());
  ASSERT_TRUE(bottom_preview.has_value());
  EXPECT_EQ(top_preview->stave_id, top.stave_id);
  EXPECT_EQ(bottom_preview->stave_id, bottom.stave_id);
  EXPECT_NE(top_preview->stave_id, bottom_preview->stave_id);
}

TEST(NotePreviewTest, PitchOutsideValidOctaveRangeYieldsNoPreviewNotAClamp) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto&         system = layout.systems[0];
  const NotationPoint point{
      system.measures[0].bounds.x + system.measures[0].bounds.width * 0.5,
      system.bounds.y + system.bounds.height};

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(preview.has_value());
}

// ---- Non-mutation, determinism, geometry validity ----

TEST(NotePreviewTest, NeverMutatesTheProjectOrTheLayout) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationLayout layout_before = layout;
  const Node          node_before = *fixture.project.find_node(fixture.node_id);
  const NotationPoint point       = staff_center(layout);

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(layout, layout_before);
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), node_before);
}

TEST(NotePreviewTest, PreviewCommandsNeverAppearInLayoutCommandsOrHitRegions) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const std::size_t   command_count_before    = layout.commands.size();
  const std::size_t   hit_region_count_before = layout.hit_regions.size();
  const NotationPoint point                   = staff_center(layout);

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_FALSE(preview->commands.empty());
  EXPECT_EQ(layout.commands.size(), command_count_before);
  EXPECT_EQ(layout.hit_regions.size(), hit_region_count_before);
}

TEST(NotePreviewTest, IdenticalInputsProduceIdenticalResults) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto first =
      preview_note_entry(fixture.project, layout, note_state(), point);
  const auto second =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, *second);
}

TEST(NotePreviewTest, GeometryIsFiniteAndWithinTheMaximumCoordinate) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto preview = preview_note_entry(
      fixture.project, layout, note_state(2, 1, NoteValue::kQuarter), point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_TRUE(preview->geometry_is_finite());
  for (const auto& command : preview->commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    ASSERT_NE(glyph, nullptr);
    EXPECT_LE(std::abs(glyph->origin.x),
              NotationLayoutOptions::kMaximumCoordinate);
    EXPECT_LE(std::abs(glyph->origin.y),
              NotationLayoutOptions::kMaximumCoordinate);
  }
}

// ---- Pitch inversion round-trip (all four clefs, wide step range) ----

[[nodiscard]] std::optional<SpelledPitch> round_trip_pitch(Clef        clef,
                                                           Letter      letter,
                                                           std::int8_t octave) {
  const auto pitch = SpelledPitch::create(letter, octave);
  if (!pitch.has_value())
    return std::nullopt;
  Fixture fixture(1, clef);
  EXPECT_TRUE(
      fixture.voice()
          .append(make_note(*pitch, *Duration::create(NoteValue::kWhole, 0)))
          .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const Note          note  = std::get<Note>(fixture.voice().events().back());
  const NotationPoint point = notehead_origin(layout, note);
  const auto          preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  return preview.has_value() ? preview->candidate_pitch : std::nullopt;
}

// preview_note_entry() resolves the staff itself through a bounded
// ledger/marking lane around it (the staff's own five lines plus a fixed
// allowance matching layout_notation()'s six-staff-space system_top_padding
// -- see the "staff proximity gating" tests above), not just system
// containment or SpelledPitch's own [kMinOctave, kMaxOctave] range. A pitch
// placed further above or below the staff than that lane allows is never
// reachable by a click in the first place (in the real layout it would sit
// outside the marking budget the layout itself reserves). That window is
// centered on the staff, whose own diatonic position differs per clef
// (clef_middle_line), so each clef's reachable octave span is its own,
// verified directly through the full point -> system -> staff -> pitch path
// rather than reproduced from the internal formula here.
struct RoundTripOctaveRange {
  Clef        clef;
  std::int8_t min_octave;
  std::int8_t max_octave;
};

constexpr std::array<RoundTripOctaveRange, 4> kRoundTripOctaveRanges = {
    RoundTripOctaveRange{Clef::kTreble, 3, 6},
    RoundTripOctaveRange{Clef::kBass, 1, 4},
    RoundTripOctaveRange{Clef::kAlto, 2, 5},
    RoundTripOctaveRange{Clef::kTenor, 2, 5},
};

TEST(NotePreviewTest, PitchInversionRoundTripsAcrossAWideStepRangeInEveryClef) {
  constexpr std::array<Letter, 7> kLetters = {
      Letter::kA, Letter::kB, Letter::kC, Letter::kD,
      Letter::kE, Letter::kF, Letter::kG};
  for (const auto& range : kRoundTripOctaveRanges) {
    for (std::int8_t octave = range.min_octave; octave <= range.max_octave;
         ++octave) {
      for (const Letter letter : kLetters) {
        const auto expected = SpelledPitch::create(letter, octave);
        ASSERT_TRUE(expected.has_value());
        const auto actual = round_trip_pitch(range.clef, letter, octave);
        ASSERT_TRUE(actual.has_value())
            << "clef " << static_cast<int>(range.clef) << " letter "
            << static_cast<int>(letter) << " octave "
            << static_cast<int>(octave);
        EXPECT_EQ(*actual, *expected);
      }
    }
  }
}

}  // namespace
