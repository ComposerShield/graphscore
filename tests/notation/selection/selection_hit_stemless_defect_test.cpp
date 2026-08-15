// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- Defect Family 3 (M5-phase-16h): two stemless chords in different
// voices at the same onset emit overlapping equal-area notehead-column
// regions.  hit_test's semantic_id tie-break depends on UUID ordering, so
// the result is not deterministic across IDs.  resolve_selection_at uses
// the palette's armed voice as a preference: when the armed voice owns one
// of the coincident columns, that voice's chord is selected regardless of
// which column hit_test returned.  Direct notehead/glyph hits are
// unaffected and always resolve to their actual owning voice. ----

// Helper: appends a whole-note chord with the given notes to a voice and
// returns the chord's id.
[[nodiscard]] NotationEntityId append_stemless_chord(
    Fixture& fixture, std::vector<ChordNote> notes,
    std::uint8_t voice_index = 1) {
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), std::move(notes));
  const NotationEntityId id = chord.id;
  EXPECT_TRUE(fixture.voice(voice_index).append(chord).ok());
  return id;
}

TEST(SelectionResolverTest,
     StemlessChordColumnPrefersArmedVoiceWhenColumnsCoincide) {
  // Two voices, each with a stemless whole-note chord at the same onset on
  // the same two pitches (E4 + G4).  Both emit notehead-column regions
  // whose bounds have the same area (same pitch span and same width, with
  // voice-collision horizontal displacement that preserves the column
  // dimensions).  A click in the overlap region can hit either column;
  // hit_test breaks the tie by area first, and when area is equal, falls
  // to semantic_id (UUID) ordering, which is not deterministic across
  // IDs.  resolve_selection_at uses the palette's armed voice as a
  // preference only among equal-priority, equal-area candidates at the
  // same staff and onset.
  Fixture fixture(1);

  const std::vector<ChordNote> v1_notes = two_chord_notes();
  const std::vector<ChordNote> v2_notes = two_chord_notes();

  const NotationEntityId voice1_id =
      append_stemless_chord(fixture, v1_notes, 1);
  const NotationEntityId voice2_id =
      append_stemless_chord(fixture, v2_notes, 2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* col1 = find_hit_region(layout, column_hit_id(voice1_id));
  const HitRegion* col2 = find_hit_region(layout, column_hit_id(voice2_id));
  ASSERT_NE(col1, nullptr);
  ASSERT_NE(col2, nullptr);
  EXPECT_EQ(col1->priority, col2->priority);
  // Columns have equal area because they span the same pitches and have
  // the same width (voice-collision offset only moves them horizontally).
  const double area1 = col1->bounds.width * col1->bounds.height;
  const double area2 = col2->bounds.width * col2->bounds.height;
  EXPECT_DOUBLE_EQ(area1, area2);
  EXPECT_GT(col1->bounds.width, 0.0);
  EXPECT_GT(col2->bounds.width, 0.0);

  // Compute a point inside both columns: the midpoint of their
  // intersection in x, at the common vertical centre.
  const double overlap_left  = std::max(col1->bounds.x, col2->bounds.x);
  const double overlap_right = std::min(col1->bounds.x + col1->bounds.width,
                                        col2->bounds.x + col2->bounds.width);
  ASSERT_LT(overlap_left, overlap_right);
  const double        overlap_x = (overlap_left + overlap_right) * 0.5;
  const double        overlap_y = col1->bounds.y + col1->bounds.height * 0.5;
  const NotationPoint point{overlap_x, overlap_y};

  ASSERT_TRUE(col1->bounds.contains(point));
  ASSERT_TRUE(col2->bounds.contains(point));

  // Both chords are at musical onset 0: whole notes at measure start.
  // The armed-voice override verifies equal onset before swapping, so this
  // verifies the override fires only for genuinely simultaneous chords.

  // When Voice 1 is armed, the Voice 1 chord is selected -- regardless of
  // which column hit_test would return on its own UUID-based tie-break.
  {
    const auto voice1_selection =
        resolve_selection_at(fixture.project, layout, note_state(1), point);
    ASSERT_TRUE(voice1_selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*voice1_selection);
    ASSERT_NE(chord_set, nullptr);
    ASSERT_EQ(chord_set->items().size(), 1u);
    EXPECT_EQ(chord_set->items().front().entity, voice1_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(1));
    EXPECT_TRUE(validate_selection(fixture.project, *voice1_selection).empty());
  }

  // When Voice 2 is armed, the Voice 2 chord is selected -- the same
  // assertion, verifying the armed-voice preference and not mere UUID
  // ordering.
  {
    const auto voice2_selection =
        resolve_selection_at(fixture.project, layout, note_state(2), point);
    ASSERT_TRUE(voice2_selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*voice2_selection);
    ASSERT_NE(chord_set, nullptr);
    ASSERT_EQ(chord_set->items().size(), 1u);
    EXPECT_EQ(chord_set->items().front().entity, voice2_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
    EXPECT_TRUE(validate_selection(fixture.project, *voice2_selection).empty());
  }
}

TEST(SelectionResolverTest,
     DirectNoteheadHitOnMultivoiceChordResolvesToTheOwningVoice) {
  // Direct notehead hit must still resolve to the actual owning voice, not
  // the armed voice -- even when another voice also has a coincident
  // column at the same onset.
  Fixture fixture(1);

  const Note voice1_note = make_note(*SpelledPitch::create(Letter::kC, 5),
                                     *Duration::create(NoteValue::kWhole, 0));
  const Note voice2_note = make_note(*SpelledPitch::create(Letter::kE, 4),
                                     *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice(1).append(voice1_note).ok());
  ASSERT_TRUE(fixture.voice(2).append(voice2_note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Click on voice 2's notehead while voice 1 is armed -- still gets
  // voice 2's notehead (direct notehead hit, not a column hit).
  {
    const NotationPoint point = notehead_origin(layout, voice2_note.id);
    const auto          selection =
        resolve_selection_at(fixture.project, layout, note_state(1), point);
    ASSERT_TRUE(selection.has_value());
    const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
    ASSERT_NE(notehead_set, nullptr);
    ASSERT_EQ(notehead_set->items().size(), 1u);
    EXPECT_EQ(notehead_set->items().front().entity, voice2_note.id);
    EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(2));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Click on voice 1's notehead while voice 2 is armed -- still gets
  // voice 1's notehead.
  {
    const NotationPoint point = notehead_origin(layout, voice1_note.id);
    const auto          selection =
        resolve_selection_at(fixture.project, layout, note_state(2), point);
    ASSERT_TRUE(selection.has_value());
    const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
    ASSERT_NE(notehead_set, nullptr);
    ASSERT_EQ(notehead_set->items().size(), 1u);
    EXPECT_EQ(notehead_set->items().front().entity, voice1_note.id);
    EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(1));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}

// ---- Defect Family 2 (M5-phase-16h): armed-voice column preference is
// correctly scoped to genuinely tied candidates only. ----

// Two overlapping columns with different areas (different pitch spans):
// the smaller-area column wins geometrically regardless of armed voice.
TEST(SelectionResolverTest,
     UnequalAreaColumnWinsEvenWhenArmedVoiceHasLargerColumn) {
  Fixture fixture(1);

  // Voice 1: wide-spaced chord (C5 + G5, a fifth) -- larger column area.
  const std::vector<ChordNote> wide_notes = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kC, 5),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 5),
       false},
  };
  const NotationEntityId wide_id =
      append_stemless_chord(fixture, wide_notes, 1);

  // Voice 2: close-spaced chord (C5 + E5, a third) -- smaller column area.
  const std::vector<ChordNote> narrow_notes = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kC, 5),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 5),
       false},
  };
  const NotationEntityId narrow_id =
      append_stemless_chord(fixture, narrow_notes, 2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* wide_col = find_hit_region(layout, column_hit_id(wide_id));
  const HitRegion* narrow_col =
      find_hit_region(layout, column_hit_id(narrow_id));
  ASSERT_NE(wide_col, nullptr);
  ASSERT_NE(narrow_col, nullptr);

  // Narrow column has strictly smaller area.
  const double wide_area = wide_col->bounds.width * wide_col->bounds.height;
  const double narrow_area =
      narrow_col->bounds.width * narrow_col->bounds.height;
  ASSERT_LT(narrow_area, wide_area);

  // Click inside the overlap region.
  const double overlap_left =
      std::max(wide_col->bounds.x, narrow_col->bounds.x);
  const double overlap_right =
      std::min(wide_col->bounds.x + wide_col->bounds.width,
               narrow_col->bounds.x + narrow_col->bounds.width);
  ASSERT_LT(overlap_left, overlap_right);
  const NotationPoint point{
      (overlap_left + overlap_right) * 0.5,
      narrow_col->bounds.y + narrow_col->bounds.height * 0.5,
  };

  // hit_test picks the smaller-area column (Voice 2), not the Voice 1 one.
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->id.value, narrow_col->id.value);

  // Voice 1 armed -- the unequal-area override is suppressed; the geometric
  // winner (Voice 2) is preserved.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items().front().entity, narrow_id);
  EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// An adjacent-onset column cannot cross-select: the armed voice's column at
// a different onset should not override the hit_test winner.
TEST(SelectionResolverTest, AdjacentOnsetColumnDoesNotOverrideTheWinner) {
  Fixture fixture(2);  // two measures so we can offset onsets

  // Voice 1: whole-note chord at measure start (onset 0).
  const Note v1_note = make_note(*SpelledPitch::create(Letter::kE, 4),
                                 *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice(1).append(v1_note).ok());

  // Voice 2: quarter rest + dotted half chord, so the chord starts at
  // onset = quarter (not measure start).
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(rest).ok());
  const Note v2_note =
      make_note(*SpelledPitch::create(Letter::kG, 4),
                *Duration::create(NoteValue::kHalf, 1));  // dotted half
  ASSERT_TRUE(fixture.voice(2).append(v2_note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Click on Voice 1's single-note stemless column (coincident with its
  // notehead).  Voice 2's note is at a different onset and has a stem
  // (half note), so it emits no column.
  const NotationPoint point = notehead_origin(layout, v1_note.id);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kNotehead);

  // Voice 2 armed.  The Voice 1 notehead is a direct kNotehead hit, not a
  // column, so the armed-voice column override path is never entered.  The
  // result is Voice 1's notehead regardless.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  EXPECT_EQ(notehead_set->items().front().entity, v1_note.id);
  EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// A column on a different staff/system cannot override the winner -- the
// staff pointer comparison in the armed-voice scan prevents cross-staff
// selection.
TEST(SelectionResolverTest, OtherStaffColumnDoesNotOverrideTheWinner) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);

  // Upper staff: a stemless chord.
  const NotationEntityId upper_id =
      append_stemless_chord(fixture, two_chord_notes(), 1);
  // Lower staff: another stemless chord (Voice 1 on the lower staff).
  auto&       lower_voice = fixture.voice(1, 0, 1);
  const Chord lower_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), two_chord_notes());
  const auto append_result = lower_voice.append(lower_chord);
  ASSERT_TRUE(append_result.ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Click on the upper staff's column centre.
  const HitRegion* upper_col = find_hit_region(layout, column_hit_id(upper_id));
  const HitRegion* lower_col =
      find_hit_region(layout, column_hit_id(lower_chord.id));
  ASSERT_NE(upper_col, nullptr);
  ASSERT_NE(lower_col, nullptr);
  const NotationPoint point{
      upper_col->bounds.x + upper_col->bounds.width * 0.5,
      upper_col->bounds.y + upper_col->bounds.height * 0.5};
  ASSERT_TRUE(upper_col->bounds.contains(point));
  // The lower staff's column does not contain this point -- the y is far
  // from the lower staff's own vertical span, so column override is not
  // reachable regardless of guard state.
  EXPECT_FALSE(lower_col->bounds.contains(point));

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  // The result must be the upper staff's chord -- never the lower staff's.
  EXPECT_EQ(chord_set->items().front().entity, upper_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Defect Family 3 (M5-phase-16h): adjacent-onset column override
// guard.  Two stemless chords at different onsets in different voices
// each emit a notehead-column region.  The click must resolve to the
// column that actually covers the click point, not to the armed voice's
// column at a different onset (which has the same priority/area
// coincidentally).  Unlike the existing
// AdjacentOnsetColumnDoesNotOverrideTheWinner test (which clicks a
// kNotehead region), this test clicks a column overlap point so the
// column-override path is actually entered. ----

TEST(SelectionResolverTest, AdjacentOnsetColumnsDoNotCrossOverride) {
  // Two measures, two voices, both at the same onset so that columns
  // have overlapping bounds.  Voice 1 gets a stemless chord at onset 0;
  // voice 2 gets a stemless chord shifted to a later onset (different
  // measure position), so the columns have different x positions and
  // do not both contain the midpoint click on voice 1's column.
  Fixture fixture(1);

  // Voice 1: two stemless chords at onset 0 (same pitches for both voices
  // so columns have equal area).
  const std::vector<ChordNote> notes1 = two_chord_notes();
  const NotationEntityId v1_id = append_stemless_chord(fixture, notes1, 1);

  // Voice 2: a quarter rest then a stemless chord at onset = quarter.
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(rest).ok());
  const std::vector<ChordNote> notes2 = two_chord_notes();
  const Chord                  v2_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes2);
  ASSERT_TRUE(fixture.voice(2).append(v2_chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* col1 = find_hit_region(layout, column_hit_id(v1_id));
  const HitRegion* col2 = find_hit_region(layout, column_hit_id(v2_chord.id));
  ASSERT_NE(col1, nullptr);
  ASSERT_NE(col2, nullptr);
  // The columns have equal area (same pitches, same width) and equal
  // priority, but different x positions (different onsets).
  EXPECT_EQ(col1->priority, col2->priority);
  EXPECT_DOUBLE_EQ(col1->bounds.width * col1->bounds.height,
                   col2->bounds.width * col2->bounds.height);
  EXPECT_NE(col1->bounds.x, col2->bounds.x);

  // Click at voice 1's column centre.  Voice 2's column is at a
  // different x and does not contain this point.
  const NotationPoint point{col1->bounds.x + col1->bounds.width * 0.5,
                            col1->bounds.y + col1->bounds.height * 0.5};
  ASSERT_TRUE(col1->bounds.contains(point));
  EXPECT_FALSE(col2->bounds.contains(point));

  // Voice 2 armed.  The column override scan checks point containment:
  // voice 2's column does not contain this click point, so it cannot
  // be an alternative.  The result is voice 1's chord (the geometric
  // winner), which may differ from the armed voice.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, v1_id);
  EXPECT_NE(chord_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// genuinely tied equal-area columns.
TEST(SelectionResolverTest,
     ThreeWayColumnTieChoosesArmedVoiceAmongTiedCandidates) {
  Fixture fixture(1);

  const std::vector<ChordNote> notes = two_chord_notes();
  const NotationEntityId       v1_id = append_stemless_chord(fixture, notes, 1);
  const NotationEntityId       v2_id = append_stemless_chord(fixture, notes, 2);
  const NotationEntityId       v3_id = append_stemless_chord(fixture, notes, 3);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* col1 = find_hit_region(layout, column_hit_id(v1_id));
  const HitRegion* col2 = find_hit_region(layout, column_hit_id(v2_id));
  const HitRegion* col3 = find_hit_region(layout, column_hit_id(v3_id));
  ASSERT_NE(col1, nullptr);
  ASSERT_NE(col2, nullptr);
  ASSERT_NE(col3, nullptr);

  // Point inside all three columns.
  const double overlap_left =
      std::max({col1->bounds.x, col2->bounds.x, col3->bounds.x});
  const double overlap_right = std::min({col1->bounds.x + col1->bounds.width,
                                         col2->bounds.x + col2->bounds.width,
                                         col3->bounds.x + col3->bounds.width});
  ASSERT_LT(overlap_left, overlap_right);
  const NotationPoint point{
      (overlap_left + overlap_right) * 0.5,
      col1->bounds.y + col1->bounds.height * 0.5,
  };

  // Voice 3 armed -- its chord is selected among the three tied columns.
  {
    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(3), point);
    ASSERT_TRUE(selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*selection);
    ASSERT_NE(chord_set, nullptr);
    EXPECT_EQ(chord_set->items().front().entity, v3_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(3));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Voice 2 armed -- its chord is selected.
  {
    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(2), point);
    ASSERT_TRUE(selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*selection);
    ASSERT_NE(chord_set, nullptr);
    EXPECT_EQ(chord_set->items().front().entity, v2_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}

// A stale alternative whose semantic entity cannot be resolved is skipped
// rather than crashing or selecting the wrong chord.
TEST(SelectionResolverTest,
     StaleAlternativeColumnIsSkippedInArmedVoiceOverride) {
  Fixture fixture(1);

  const NotationEntityId v1_id =
      append_stemless_chord(fixture, two_chord_notes(), 1);
  const NotationEntityId v2_id =
      append_stemless_chord(fixture, two_chord_notes(), 2);

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Inject a fake column region with a semantic_id that does not name any
  // real domain entity -- simulating a stale layout.
  const HitRegion* col1 = find_hit_region(layout, column_hit_id(v1_id));
  ASSERT_NE(col1, nullptr);

  // Copy bounds and priority before push_back, so no pointer into
  // layout.hit_regions is retained across vector mutation.
  const NotationRect col1_bounds   = col1->bounds;
  const int          col1_priority = col1->priority;

  const HitRegion stale_col{NotationId{"stale/notehead-column/hit"},
                            // Keep this deterministically an alternative, not
                            // hit_test's lexicographically smallest winner.
                            NotationId{"zzzz-stale-entity"}, HitRole::kEvent,
                            col1_bounds, col1_priority, std::nullopt,
                            std::nullopt};
  layout.hit_regions.push_back(stale_col);

  const NotationPoint point{
      col1_bounds.x + col1_bounds.width * 0.5,
      col1_bounds.y + col1_bounds.height * 0.5,
  };
  ASSERT_TRUE(col1_bounds.contains(point));
  ASSERT_TRUE(stale_col.bounds.contains(point));

  // Voice 2 armed.  The stale column coincidentally covers the point and
  // has equal area/priority with the real columns, but its semantic entity
  // cannot be resolved, so it is skipped.  The Voice 2 chord is selected
  // through the real column.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items().front().entity, v2_id);
  EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}
}  // namespace
