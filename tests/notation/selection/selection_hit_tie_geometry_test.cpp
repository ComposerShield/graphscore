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

// ---- Clustered seconds: the cases that rule out separating the column
// from the per-notehead glyph regions by geometry. Two engraver rules
// displace a notehead horizontally -- the seconds rule (space * 0.75) and
// the voice-collision rule (space * 0.22) -- and the column spans every
// displaced head_x while an accidental/dot offset is measured from its own
// note's head_x. Either rule can therefore carry the column out past the
// glyph, putting the glyph's exact origin inside the column. Each of these
// asserts that containment before asserting the selection, so the case
// cannot silently stop being a regression test if placement ever changes.
// ----

// An adjacent-second dyad. The second-listed pitch is displaced by the
// seconds rule, away from the stem: right in a stem-down voice, left in a
// stem-up one.
[[nodiscard]] std::vector<ChordNote> clustered_chord_notes(
    Letter                 upper_letter,
    graphscore::Accidental lower_accidental = graphscore::Accidental::kNatural,
    graphscore::Accidental upper_accidental =
        graphscore::Accidental::kNatural) {
  return {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, lower_accidental), false},
      {NotationEntityId::generate(),
       *SpelledPitch::create(upper_letter, 4, upper_accidental), false},
  };
}

void expect_selects_only(const Fixture& fixture, const NotationLayout& layout,
                         const HitRegion* column, NotationPoint point,
                         const NotationEntityId& expected) {
  ASSERT_NE(column, nullptr);
  EXPECT_TRUE(column->bounds.contains(point));

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, expected);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     AStemDownClusteredChordsDisplacedAccidentalInsideTheColumnStillWins) {
  Fixture fixture(1);
  // Voice 2 is a stem-down voice, so the seconds rule displaces F#4 to the
  // right and its accidental lands near the column's own centre.
  const std::vector<ChordNote> notes =
      clustered_chord_notes(Letter::kF, graphscore::Accidental::kNatural,
                            graphscore::Accidental::kSharp);
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice(2).append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[1].id.to_string() + "/accidental/column-0");
  expect_selects_only(fixture, layout,
                      find_hit_region(layout, column_hit_id(chord.id)), point,
                      notes[1].id);
}

TEST(SelectionResolverTest,
     AStemUpClusteredChordsAccidentalInsideTheColumnStillWins) {
  Fixture fixture(1);
  // Voice 1 is stem-up, so the seconds rule displaces F4 to the *left*,
  // widening the column past E#4's own accidental instead.
  const std::vector<ChordNote> notes =
      clustered_chord_notes(Letter::kF, graphscore::Accidental::kSharp);
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[0].id.to_string() + "/accidental/column-0");
  expect_selects_only(fixture, layout,
                      find_hit_region(layout, column_hit_id(chord.id)), point,
                      notes[0].id);
}

TEST(SelectionResolverTest, AClusteredDottedChordsDotInsideTheColumnStillWins) {
  // Two measures: a dotted whole is 3/2, which does not fit in one 4/4
  // measure's own length.
  Fixture                      fixture(2);
  const std::vector<ChordNote> notes = clustered_chord_notes(Letter::kF);
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 1), notes);
  ASSERT_TRUE(fixture.voice(2).append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[0].id.to_string() + "/dot/0");
  expect_selects_only(fixture, layout,
                      find_hit_region(layout, column_hit_id(chord.id)), point,
                      notes[0].id);
}

// The tie hit region is now subdivided into 8 segments, each bound to
// the local curve extent rather than a single rectangle spanning the
// whole envelope (add_span_segment, kHitRoleTie branch in
// src/notation/notation_engraving.cpp).  With both notes tied in a close-voiced
// stemless chord (E4 + G4, a third apart on adjacent staff lines), each
// tie's sub-segment rects near the endpoints stay close to the lane --
// below the notehead y + space where the tie is drawn -- so the gap
// between the two tie bands at the column centre remains clear.
// A direct click on the tie curve itself still selects the tie (tested
// separately below).
TEST(SelectionResolverTest,
     ATiedWholeNoteChordsGapClickReachesTheChordAwayFromTheTieCurve) {
  Fixture fixture(2);
  // E4 (bottom line) + G4 (second line in treble clef) -- a third apart,
  // both tied -- the original close-voiced case the single-rectangle tie
  // region used to shadow.
  const std::vector<ChordNote> tied = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       true},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       true},
  };
  const Chord first = make_chord(*Duration::create(NoteValue::kWhole, 0), tied);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const Chord second =
      make_chord(*Duration::create(NoteValue::kWhole, 0), two_chord_notes());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const HitRegion* column = find_hit_region(layout, column_hit_id(first.id));
  ASSERT_NE(column, nullptr);

  // Click at the column's own centre -- the gap between the two noteheads
  // (E4 at staff line, G4 one line above).  The subdivided tie regions are
  // tight to the actual cubic curves: near the endpoints (t ≈ 0 and t ≈ 1),
  // the curve y is close to lane = pitch_y + space, so the per-segment
  // rects for the sub/0 and sub/7 segments extend only ~0.2*space below
  // lane, nowhere near the column centre.  The column therefore wins the
  // hit at priority 5, and even though the tie regions outrank it
  // (priority 7), none of them cover this point.
  const NotationPoint point{column->bounds.x + column->bounds.width * 0.5,
                            column->bounds.y + column->bounds.height * 0.5};
  EXPECT_TRUE(column->bounds.contains(point));

  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items().front().entity, first.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// A stemless single Note emits a column too, coinciding exactly with its
// sole notehead's region and fully shadowed by it -- the header states this;
// nothing tested it.
TEST(SelectionResolverTest,
     AStemlessSingleNotesColumnCoincidesWithAndLosesToItsNotehead) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kE, 4),
                              *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const HitRegion* column = find_hit_region(layout, column_hit_id(note.id));
  const HitRegion* head =
      find_hit_region(layout, note.id.to_string() + "/notehead/hit");
  ASSERT_NE(column, nullptr);
  ASSERT_NE(head, nullptr);
  EXPECT_EQ(column->bounds, head->bounds);
  EXPECT_LT(column->priority, head->priority);

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

// ---- Defect Family 1 (M5-phase-16h): a tie's tightened hit region no
// longer shadows articulation glyphs on the same chord away from the
// actually drawn tie curve. ----

TEST(SelectionResolverTest,
     AnArticulationOnATiedChordOutranksTheTieRegionAwayFromTheCurve) {
  // Two measures: a dotted whole is 3/2 > 4/4.
  Fixture            fixture(2);
  const SpelledPitch tied_pitch = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(tied_pitch, *Duration::create(NoteValue::kWhole, 1), true,
                {Articulation::kAccent});
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const Note second =
      make_note(tied_pitch, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint articulation_point =
      glyph_origin(layout, first.id.to_string() + "/articulation/0");

  // The articulation glyph sits above the notehead; the tightened tie band
  // no longer reaches it.
  ASSERT_TRUE(layout.hit_test(articulation_point).has_value());
  EXPECT_EQ(layout.hit_test(articulation_point)->role, HitRole::kMarking);

  const auto selection = resolve_selection_at(fixture.project, layout,
                                              note_state(), articulation_point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().kind, MarkingKind::kArticulation);
  ASSERT_TRUE(set->items().front().articulation.has_value());
  EXPECT_EQ(*set->items().front().articulation, Articulation::kAccent);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- The tightened tie region still selects the tie itself when the
// click lands directly on the drawn tie curve. ----

TEST(SelectionResolverTest, AClickOnTheTieCurveItselfStillSelectsTheTie) {
  Fixture            fixture(2);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  const Note second =
      make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // The subdivided tie's middle sub-segment (sub/4) covers the curve apex,
  // the deepest point of the arch -- well away from both noteheads where
  // the curve is closest to the lane.  Clicking at the centre of that
  // sub-segment's hit region is a click on the actual drawn curve.
  const std::string tie_hit_id =
      first.id.to_string() + "/tie/segment/system-0/sub/4/hit";
  const NotationPoint point = hit_region_center(layout, tie_hit_id);
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kTie);
  EXPECT_EQ(item.anchor, first.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Clicking a point inside the single-segment old-rectangle formula but
// away from every actual subdivided tie sub-segment does not select the tie
// -- the subdivided curve rects are tight to the actual bezier, so the old
// universal band is reachable through the column or a container.
TEST(SelectionResolverTest,
     APointInsideGlobalTieEnvelopeButAwayFromTheLocalCurveDoesNotSelectTheTie) {
  Fixture            fixture(2);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  const Note second =
      make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint notehead = notehead_origin(layout, first.id);

  // A point near the start notehead's x, at the full arch depth
  // (the apex is only reached at the midpoint, not near the endpoints).
  const double        space = 10.0;  // default staff_space in FixedMetrics
  const NotationPoint point{notehead.x + space * 0.5, notehead.y - space * 0.8};

  // 1. Prove the point is inside the old single-rectangle envelope formula.
  // The old non-subdivided tie hit region was a single rectangle:
  //   x = min(from.x, to.x)
  //   y = lane - 2 * space   where lane = pitch_y + space
  //   width = abs(to.x - from.x)
  //   height = 4 * space
  // For C4 in treble clef with space=10: lane ≈ 40 + 10 = 50, so the
  // rectangle extends y ∈ [30, 70].
  const double       from_x     = notehead_origin(layout, first.id).x;
  const double       to_x       = notehead_origin(layout, second.id).x;
  const double       old_left   = std::min(from_x, to_x);
  const double       old_top    = notehead.y + space - space * 2.0;
  const double       old_width  = std::abs(to_x - from_x);
  const double       old_height = space * 4.0;
  const NotationRect old_envelope{old_left, old_top, old_width, old_height};
  EXPECT_TRUE(old_envelope.contains(point));

  // 2. Prove the point is outside every subdivided tie sub-segment hit
  // region (it is near the start x, far vertical from the local curve).
  for (const HitRegion& region : layout.hit_regions) {
    if (region.role == HitRole::kMarking &&
        region.id.value.starts_with(first.id.to_string() + "/tie/segment/")) {
      EXPECT_FALSE(region.bounds.contains(point))
          << "point inside tie sub-region " << region.id.value;
    }
  }

  // 3. The point must not resolve to a tie selection.
  const auto hit = layout.hit_test(point);
  if (hit.has_value()) {
    EXPECT_NE(hit->role, HitRole::kMarking);
  }
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  if (selection.has_value()) {
    const auto* set = std::get_if<MarkingSet>(&*selection);
    if (set != nullptr) {
      ASSERT_FALSE(set->items().empty());
      EXPECT_NE(set->items().front().kind, MarkingKind::kTie);
    }
  }
}

// ---- Defect Family 3 (M5-phase-16h) tie geometry: short tie segment
// (small dx) subdivision, and per-system clipping assertions for ties
// spanning a system break.  The subdivided curve rects must be strictly
// within the owning system's bounds. ----

TEST(SelectionResolverTest, ShortTieSubSegmentsAllClipToSystemBounds) {
  // Two notes very close together (small dx): the tie's cubic arch is
  // shallow, so each sub-segment rect is short and near the lane.  Every
  // sub-segment must be strictly within the system bounds.
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kEighth, 0), true);
  const Note second =
      make_note(pitch, *Duration::create(NoteValue::kEighth, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  const NotationRect sys_bounds = layout.systems[0].bounds;

  // Count and verify every tie sub-segment hit region.
  std::size_t sub_count = 0;
  for (const HitRegion& region : layout.hit_regions) {
    if (!region.id.value.starts_with(first.id.to_string() +
                                     "/tie/segment/system-0/sub/")) {
      continue;
    }
    ++sub_count;
    // Each sub-segment rect must be finite and within system bounds.
    EXPECT_GE(region.bounds.x, sys_bounds.x) << "sub " << sub_count - 1;
    EXPECT_GE(region.bounds.y, sys_bounds.y) << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.x + region.bounds.width,
              sys_bounds.x + sys_bounds.width)
        << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.y + region.bounds.height,
              sys_bounds.y + sys_bounds.height)
        << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.width, 0.0) << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.height, 0.0) << "sub " << sub_count - 1;
  }
  // Exactly 8 sub-segments for the subdivided curve.
  EXPECT_EQ(sub_count, 8u);

  // The tie must be selectable: click the middle sub-segment's centre.
  const NotationPoint point = hit_region_center(
      layout, first.id.to_string() + "/tie/segment/system-0/sub/4/hit");
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, LongTieSubSegmentsAllClipToSystemBounds) {
  // Two whole notes across a wide span (nearly the full system width):
  // the tie is long so each sub-segment covers a distinct x range.  The
  // global envelope extends far from the actual curve at the endpoints;
  // the subdivided rects must be visibly narrower near the endpoints.
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  const Note second = make_note(pitch, *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  const NotationRect sys_bounds = layout.systems[0].bounds;

  // Count tie sub-segments and verify each is within system bounds.
  std::size_t sub_count = 0;
  for (const HitRegion& region : layout.hit_regions) {
    if (!region.id.value.starts_with(first.id.to_string() +
                                     "/tie/segment/system-0/sub/")) {
      continue;
    }
    ++sub_count;
    // Each sub-segment rect must be within all four system bounds and
    // have positive dimensions.
    EXPECT_GE(region.bounds.x, sys_bounds.x) << "sub " << sub_count - 1;
    EXPECT_GE(region.bounds.y, sys_bounds.y) << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.x + region.bounds.width,
              sys_bounds.x + sys_bounds.width)
        << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.y + region.bounds.height,
              sys_bounds.y + sys_bounds.height)
        << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.width, 0.0) << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.height, 0.0) << "sub " << sub_count - 1;
  }
  EXPECT_EQ(sub_count, 8u);

  // The tie is selectable at the middle of the curve.  Assert exact
  // MarkingKind and anchor, not merely validate_selection success.
  const NotationPoint point = hit_region_center(
      layout, first.id.to_string() + "/tie/segment/system-0/sub/4/hit");
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
  EXPECT_EQ(set->items().front().anchor, first.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     CrossSystemTieHitRegionsAreClippedToEachOwnedSystem) {
  // A tie crossing a system break: the engraver emits separate tie
  // segments on each system, each clipped to its own system's bounds.
  // The first system's end segment must not extend past its right edge,
  // and the second system's start segment must not start before its
  // left edge.
  Fixture            fixture(2);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  // A whole note in measure 0, tied into a half note in measure 1.
  const Note first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const Note second = make_note(pitch, *Duration::create(NoteValue::kHalf, 0));
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_EQ(layout.systems.size(), 2u);

  // Verify tie segments exist on both systems and are fully clipped.
  const NotationRect sys0_bounds = layout.systems[0].bounds;
  const NotationRect sys1_bounds = layout.systems[1].bounds;
  std::size_t        sys0_segs   = 0;
  std::size_t        sys1_segs   = 0;
  for (const HitRegion& region : layout.hit_regions) {
    if (region.id.value.find("/tie/segment/system-0/sub/") !=
        std::string::npos) {
      ++sys0_segs;
      EXPECT_GE(region.bounds.x, sys0_bounds.x);
      EXPECT_GE(region.bounds.y, sys0_bounds.y);
      EXPECT_LE(region.bounds.x + region.bounds.width,
                sys0_bounds.x + sys0_bounds.width);
      EXPECT_LE(region.bounds.y + region.bounds.height,
                sys0_bounds.y + sys0_bounds.height);
      EXPECT_GT(region.bounds.width, 0.0);
      EXPECT_GT(region.bounds.height, 0.0);
    }
    if (region.id.value.find("/tie/segment/system-1/sub/") !=
        std::string::npos) {
      ++sys1_segs;
      EXPECT_GE(region.bounds.x, sys1_bounds.x);
      EXPECT_GE(region.bounds.y, sys1_bounds.y);
      EXPECT_LE(region.bounds.x + region.bounds.width,
                sys1_bounds.x + sys1_bounds.width);
      EXPECT_LE(region.bounds.y + region.bounds.height,
                sys1_bounds.y + sys1_bounds.height);
      EXPECT_GT(region.bounds.width, 0.0);
      EXPECT_GT(region.bounds.height, 0.0);
    }
  }
  EXPECT_EQ(sys0_segs, 8u);
  EXPECT_EQ(sys1_segs, 8u);

  // The tie is selectable on a representative sub-segment in each system.
  {
    const NotationPoint sys0_point = hit_region_center(
        layout, first.id.to_string() + "/tie/segment/system-0/sub/4/hit");
    const auto hit = layout.hit_test(sys0_point);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->role, HitRole::kMarking);

    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(), sys0_point);
    ASSERT_TRUE(selection.has_value());
    const auto* set = std::get_if<MarkingSet>(&*selection);
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->items().size(), 1u);
    EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
    EXPECT_EQ(set->items().front().anchor, first.id);
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
  {
    const NotationPoint sys1_point = hit_region_center(
        layout, first.id.to_string() + "/tie/segment/system-1/sub/3/hit");
    const auto hit = layout.hit_test(sys1_point);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->role, HitRole::kMarking);

    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(), sys1_point);
    ASSERT_TRUE(selection.has_value());
    const auto* set = std::get_if<MarkingSet>(&*selection);
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->items().size(), 1u);
    EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
    EXPECT_EQ(set->items().front().anchor, first.id);
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}
}  // namespace
