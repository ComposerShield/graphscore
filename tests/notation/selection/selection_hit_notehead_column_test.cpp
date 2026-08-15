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

// ---- A stemless (whole-note) event draws no stem and so emits no stem hit
// region; in its place the engraver emits one kEvent "notehead-column"
// region spanning the bounding box of the event's own noteheads, which is
// what lets a whole-note chord be selected as a whole event at all.
//
// The column occupies a rank of its own on the ladder documented at
// HitRegion::priority (graphscore_notation.hpp): strictly above every
// container region, so a click in the column beats the insertion caret,
// and strictly below every region naming an engraved object, so each
// notehead and each per-notehead accidental or augmentation dot the column
// overlaps keeps selecting its own ChordNote. That rank is deliberately
// *not* the stem region's -- the stem is an engraved object and ranks with
// the other glyphs, above the column. Do not collapse the two. ----

TEST(SelectionResolverTest,
     AWholeNoteChordEmitsNoStemButANoteheadColumnOverItsNoteheadsBoundingBox) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  EXPECT_EQ(find_hit_region(layout, chord.id.to_string() + "/stem/hit"),
            nullptr);

  const HitRegion* lower =
      find_hit_region(layout, notes[0].id.to_string() + "/notehead/hit");
  const HitRegion* upper =
      find_hit_region(layout, notes[1].id.to_string() + "/notehead/hit");
  const HitRegion* column = find_hit_region(layout, column_hit_id(chord.id));
  ASSERT_NE(lower, nullptr);
  ASSERT_NE(upper, nullptr);
  ASSERT_NE(column, nullptr);

  EXPECT_EQ(column->role, HitRole::kEvent);
  EXPECT_EQ(column->semantic_id.value, chord.id.to_string());

  // The rank itself is swept against a layout rich enough to span the
  // ladder by the sibling test below; here only the local relationship.
  EXPECT_EQ(column->priority, 5);
  EXPECT_LT(column->priority, lower->priority);
  EXPECT_LT(column->priority, upper->priority);

  // Exactly the union of the two noteheads' own hit regions.
  const double left   = std::min(lower->bounds.x, upper->bounds.x);
  const double right  = std::max(lower->bounds.x + lower->bounds.width,
                                 upper->bounds.x + upper->bounds.width);
  const double top    = std::min(lower->bounds.y, upper->bounds.y);
  const double bottom = std::max(lower->bounds.y + lower->bounds.height,
                                 upper->bounds.y + upper->bounds.height);
  EXPECT_EQ(column->bounds.x, left);
  EXPECT_EQ(column->bounds.y, top);
  EXPECT_EQ(column->bounds.width, right - left);
  EXPECT_EQ(column->bounds.height, bottom - top);
  EXPECT_GT(column->bounds.height, lower->bounds.height);
}

// Sweeps every region of a layout deliberately built to put a
// representative of each engraved-object rank on the page beside a
// stemless chord's column, so that "the column ranks below every engraved
// object" is checked against the ladder rather than against two noteheads.
// The fixture engraves, in two measures of one voice:
//
//   * a dotted whole chord (E#4 + G4), both notes tied into
//   * a half chord on the same two pitches, and
//   * a slur and a dynamic anchored on the pair,
//
// which between them emit noteheads, an accidental per E#4, an
// augmentation dot per notehead, the half chord's stem, the dynamic's two
// character glyphs, a tie segment per tied pitch, and a slur segment --
// covering kHitPriorityGlyph, kHitPrioritySpanSegment and
// kHitPriorityNotehead. The named look-ups below assert that material is
// really present, so the sweep cannot quietly become trivial again if the
// fixture is later simplified.
TEST(SelectionResolverTest,
     TheNoteheadColumnRanksAboveEveryContainerAndBelowEveryEngravedObject) {
  Fixture                      fixture(2);
  const std::vector<ChordNote> tied = {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, graphscore::Accidental::kSharp),
       true},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       true},
  };
  const Chord first = make_chord(*Duration::create(NoteValue::kWhole, 1), tied);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const std::vector<ChordNote> target = {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, graphscore::Accidental::kSharp),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       false},
  };
  const Chord second =
      make_chord(*Duration::create(NoteValue::kHalf, 0), target);
  ASSERT_TRUE(fixture.voice().append(second).ok());
  const Slur slur = make_slur(first.id, second.id);
  ASSERT_TRUE(fixture.voice().add_slur(slur).ok());
  const DynamicMarking dynamic = make_dynamic_marking(first.id, Dynamic::kMf);
  ASSERT_TRUE(fixture.voice().add_dynamic(dynamic).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const HitRegion* column = find_hit_region(layout, column_hit_id(first.id));
  ASSERT_NE(column, nullptr);

  for (const std::string& required : {
           tied[0].id.to_string() + "/notehead/hit",
           tied[0].id.to_string() + "/accidental/column-0/hit",
           tied[0].id.to_string() + "/dot/0/hit",
           tied[1].id.to_string() + "/dot/0/hit",
           second.id.to_string() + "/stem/hit",
           dynamic.id.to_string() + "/glyph/0/hit",
           tied[0].id.to_string() + "/tie/segment/system-0/sub/4/hit",
           slur.id.to_string() + "/slur/segment/system-0/hit",
       }) {
    EXPECT_NE(find_hit_region(layout, required), nullptr) << required;
  }

  std::size_t containers = 0;
  std::size_t columns    = 0;
  std::size_t objects    = 0;
  for (const HitRegion& region : layout.hit_regions) {
    const bool container =
        region.role == HitRole::kSystem || region.role == HitRole::kMeasure ||
        region.role == HitRole::kStaff || region.role == HitRole::kVoice ||
        region.role == HitRole::kStaffMeasure;
    if (container) {
      ++containers;
      EXPECT_LT(region.priority, column->priority) << region.id.value;
    } else if (region.id.value.ends_with("/notehead-column/hit")) {
      // The rank holds notehead-column regions and nothing else; a layout
      // may of course contain several of them.
      ++columns;
      EXPECT_EQ(region.priority, column->priority) << region.id.value;
    } else {
      ++objects;
      EXPECT_GT(region.priority, column->priority) << region.id.value;
    }
  }
  // Today: 10 containers (system, two measures, staff, four voices, two
  // staff-measure regions), the one column, and 14 engraved-object regions
  // -- 7 at kHitPriorityGlyph (two
  // accidentals, two augmentation dots, the half chord's stem, the
  // dynamic's two character glyphs), 3 at kHitPrioritySpanSegment (two tie
  // segments, one slur segment) and 4 at kHitPriorityNotehead. The floor is
  // a backstop for the named look-ups above: it catches the fixture losing
  // material, while still tolerating the engraver gaining a region.
  EXPECT_GT(containers, 0u);
  EXPECT_EQ(columns, 1u);
  EXPECT_GE(objects, 14u);
}

TEST(SelectionResolverTest,
     ClickingBetweenAWholeNoteChordsNoteheadsSelectsTheWholeChord) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      notehead_gap_point(layout, notes[0].id, notes[1].id);

  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->id.value, column_hit_id(chord.id));
  EXPECT_EQ(hit->role, HitRole::kEvent);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  const ChordItem& item = chord_set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  EXPECT_EQ(item.entity, chord.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     ANoteheadOfAWholeNoteChordStillOutranksTheNoteheadColumn) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  for (const ChordNote& note : notes) {
    const NotationPoint point = notehead_origin(layout, note.id);
    const auto          hit   = layout.hit_test(point);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->role, HitRole::kNotehead);

    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(), point);
    ASSERT_TRUE(selection.has_value());
    const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
    ASSERT_NE(notehead_set, nullptr);
    ASSERT_EQ(notehead_set->items().size(), 1u);
    EXPECT_EQ(notehead_set->items().front().entity, note.id);
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}

// The accidental and augmentation-dot regions add_glyph emits for a chord
// note carry that note's own semantic id and outrank the column, so a click
// on one selects that ChordNote however the two overlap. Neither the
// smaller-area tie-break nor the engraver's placement offsets would deliver
// that: the tie-break's outcome depends on the font's glyph metrics, and
// the offsets are measured from the clicked note's own head_x while the
// column spans every notehead's -- the clustered-seconds regressions below
// are exactly the cases where those two arguments fail.
TEST(SelectionResolverTest,
     AWholeNoteChordNotesAccidentalStillOutranksTheNoteheadColumn) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes =
      two_chord_notes(graphscore::Accidental::kSharp);
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_NE(find_hit_region(layout, column_hit_id(chord.id)), nullptr);
  const NotationPoint point =
      glyph_origin(layout, notes[0].id.to_string() + "/accidental/column-0");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, notes[0].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     ADottedWholeNoteChordsDotStillOutranksTheNoteheadColumn) {
  // Two measures: a dotted whole is 3/2, which does not fit in one 4/4
  // measure's own length.
  Fixture                      fixture(2);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 1), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_NE(find_hit_region(layout, column_hit_id(chord.id)), nullptr);
  const NotationPoint point =
      glyph_origin(layout, notes[1].id.to_string() + "/dot/0");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, notes[1].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Scope containment: a stemmed chord already has a kEvent region (its stem),
// so it gets no column region, and a click in the gap between its noteheads
// keeps resolving exactly as it does without this feature -- through a
// container region (today, the staff-measure region, which outranks the
// coarser system/measure/staff/voice containers it overlaps), to an
// insertion caret.
TEST(SelectionResolverTest, AStemmedChordEmitsNoNoteheadColumnRegion) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_EQ(find_hit_region(layout, column_hit_id(chord.id)), nullptr);
  EXPECT_NE(find_hit_region(layout, chord.id.to_string() + "/stem/hit"),
            nullptr);

  const NotationPoint point =
      notehead_gap_point(layout, notes[0].id, notes[1].id);
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kStaffMeasure);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  ASSERT_EQ(caret_set->items().size(), 1u);
  EXPECT_EQ(caret_set->items().front().position, Rational(0));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}
}  // namespace
