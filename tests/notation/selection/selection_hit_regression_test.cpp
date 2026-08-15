// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- HIGH-3 regression: a tuplet run spanning a system break normalizes
// to the run's true first event, not merely the event the per-system
// engraver happened to anchor the digit against (see
// docs/plan/05-notation-editor.md's cross-system tuplet hazard). ----

TEST(SelectionResolverTest,
     TupletDigitAcrossASystemBreakNormalizesToTheRunsTrueFirstEvent) {
  Fixture            fixture(2);
  const auto         ratio   = *TupletRatio::create(3, 2);
  const Duration     triplet = *Duration::create(NoteValue::kEighth, 0, ratio);
  const SpelledPitch pitch   = *SpelledPitch::create(Letter::kE, 4);
  std::vector<Note>  notes;
  // 12 eighth-note triplets exactly fill measure 0 (12 * 1/12 whole note ==
  // 1 whole note); 3 more open measure 1 with the same ratio, so the true
  // run (15 events, a whole multiple of 3) spans the barline. The
  // engraver's own per-system fragment for measure 1 prepends only measure
  // 0's own last event (notes[11]) as lookback context, so its local scan
  // anchors measure 1's digit at notes[11] -- a mid-run event, not the
  // run's true first event, notes[0].
  for (int index = 0; index < 15; ++index) {
    notes.push_back(make_note(pitch, triplet));
    ASSERT_TRUE(fixture.voice().append(notes.back()).ok());
  }

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_EQ(layout.systems.size(), 2u);
  ASSERT_EQ(layout.systems[1].first_measure, 1u);

  const NotationPoint point =
      glyph_origin(layout, notes[11].id.to_string() + "/tuplet/digit/0");
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kMarking);
  EXPECT_EQ(layout.hit_test(point)->semantic_id.value,
            notes[11].id.to_string());

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kTuplet);
  EXPECT_EQ(item.anchor, notes[0].id);
  EXPECT_NE(item.anchor, notes[11].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- M5-phase-16i regression: a malformed (incomplete) tuplet run spanning
// a system break suppresses its digit on every system it occupies, keyed to
// the run's true global first event -- not merely the event the per-system
// engraver's local fragment happens to anchor against. The domain keys
// kIncompleteTupletGroup to the true global run start, so the second
// system's local fragment (whose lookback context is a mid-run event) must
// still recognize the diagnostic and omit the digit. ----

TEST(SelectionResolverTest,
     AnIncompleteTupletRunAcrossASystemBreakSuppressesItsDigitOnBothSystems) {
  Fixture            fixture(2);
  const auto         ratio   = *TupletRatio::create(3, 2);
  const Duration     triplet = *Duration::create(NoteValue::kEighth, 0, ratio);
  const SpelledPitch pitch   = *SpelledPitch::create(Letter::kE, 4);
  std::vector<Note>  notes;
  // 12 eighth-note triplets exactly fill measure 0 (12 * 1/12 == 1 whole
  // note); 2 more open measure 1 with the same ratio. The true run (14
  // events) is not a whole multiple of 3, so the domain flags
  // kIncompleteTupletGroup against notes[0]. The engraver's per-system
  // fragment for measure 1 prepends measure 0's own last event (notes[11])
  // as lookback context, so its local scan anchors measure 1's would-be
  // digit at notes[11] -- a mid-run event. Suppression must still fire.
  for (int index = 0; index < 14; ++index) {
    notes.push_back(make_note(pitch, triplet));
    ASSERT_TRUE(fixture.voice().append(notes.back()).ok());
  }

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_EQ(layout.systems.size(), 2u);
  ASSERT_EQ(layout.systems[1].first_measure, 1u);

  // Neither system emits a tuplet digit (and therefore no digit hit region):
  // system 0's digit would anchor at the true run start and system 1's at the
  // mid-run lookback context, and both must suppress against the same
  // incomplete-run diagnostic.
  for (const NotationEntityId& anchor : {notes[0].id, notes[11].id}) {
    EXPECT_FALSE(std::ranges::any_of(layout.commands, [&](const auto& command) {
      const auto* glyph = std::get_if<GlyphCommand>(&command);
      return glyph != nullptr &&
             glyph->id.value == anchor.to_string() + "/tuplet/digit/0";
    })) << anchor.to_string();
    EXPECT_EQ(
        find_hit_region(layout, anchor.to_string() + "/tuplet/digit/0/hit"),
        nullptr)
        << anchor.to_string();
  }

  // The incomplete-run diagnostic is emitted once, deterministically keyed to
  // the true global run start.
  ASSERT_EQ(layout.diagnostics.size(), 1u);
  EXPECT_EQ(layout.diagnostics[0].entity_id, notes[0].id);
  EXPECT_EQ(
      layout.diagnostics[0].policy,
      "omitted-invalid-reference:" +
          std::to_string(static_cast<int>(
              graphscore::NotationDiagnosticCode::kIncompleteTupletGroup)));
}

// ---- Stale-layout guards: a kMarking hit whose named marking can no
// longer be found, or no longer carries the shape its kind requires,
// yields std::nullopt rather than a Selection validate_selection would
// reject. ----

TEST(SelectionResolverTest,
     ADynamicMarkingRemovedAfterLayoutBuiltYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());
  const DynamicMarking dynamic = make_dynamic_marking(note.id, Dynamic::kMf);
  ASSERT_TRUE(fixture.voice().add_dynamic(dynamic).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, dynamic.id.to_string() + "/glyph/0");

  ASSERT_TRUE(fixture.voice().remove_dynamic(dynamic.id).ok());
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest,
     AnArticulationIndexBeyondTheEventsCurrentCountYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0), false,
                              {Articulation::kAccent});
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const std::string target = note.id.to_string() + "/articulation/0/hit";
  const auto        found  = std::ranges::find_if(
      layout.hit_regions,
      [&](const HitRegion& region) { return region.id.value == target; });
  ASSERT_NE(found, layout.hit_regions.end());
  const NotationPoint point{found->bounds.x + found->bounds.width * 0.5,
                            found->bounds.y + found->bounds.height * 0.5};
  found->id = NotationId{note.id.to_string() + "/articulation/9/hit"};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest, ATieHitRegionOnAnUntiedNoteYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);
  // Fabricate a "tie segment" hit region over the note's own position,
  // naming the note as its semantic entity -- something no real emitter
  // does for an untied note (add_span_segment's tie branch only ever fires
  // when tied_to_next is set), simulating a future engraver defect.
  // The id uses the current subdivided-segment format to match the actual
  // emitter.
  layout.hit_regions.push_back(HitRegion{
      NotationId{note.id.to_string() + "/tie/segment/system-0/sub/0/hit"},
      NotationId{note.id.to_string()}, HitRole::kMarking,
      NotationRect{point.x - 1.0, point.y - 1.0, 2.0, 2.0}, 100, std::nullopt,
      std::nullopt});

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

// ---- Blank-area / no-hit fallback -> InsertionCaretSet ----

TEST(SelectionResolverTest,
     ClickingBlankStaffAreaYieldsAnInsertionCaretAtTheNearestOnset) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(fixture.voice().append(make_rest(quarter)).ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);
  ASSERT_TRUE(layout.hit_test(point).has_value());
  const HitRole blank_role = layout.hit_test(point)->role;
  // staff_center lands inside this measure's own staff-measure region,
  // which outranks the coarser system/measure/staff/voice containers it
  // overlaps (HitRegion::priority), so the role is deterministically
  // kStaffMeasure -- but resolve_selection_at's own fall-through treats
  // every one of the five container roles alike, so the resulting
  // Selection below is unaffected either way.
  EXPECT_EQ(blank_role, HitRole::kStaffMeasure);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  ASSERT_EQ(caret_set->items().size(), 1u);
  const InsertionCaretItem& item = caret_set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  // Caret legality (selection.hpp): must be an event boundary or
  // TrackLane::total_length(). A caret snapped to a raw x<->time value
  // rather than an existing onset would fail this.
  EXPECT_TRUE(fixture.voice().find_event_index_at(item.position).has_value() ||
              item.position == Rational(0));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     NoHitAtAllStillYieldsAnInsertionCaretWhenTheStaffResolves) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(fixture.voice().append(make_rest(quarter)).ok());
  }

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);
  layout.hit_regions.clear();
  EXPECT_FALSE(layout.hit_test(point).has_value());

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, PointOutsideEverySystemYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{-10'000.0, -10'000.0};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest, NonFinitePointYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{std::numeric_limits<double>::quiet_NaN(), 0.0};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

// ---- Typed-id recovery: correct track/stave/voice without a UUID parser ----

TEST(SelectionResolverTest, MultiStaffResolutionPicksTheCorrectTrackAndStave) {
  Fixture    fixture({StaffLayout::single_staff(Clef::kTreble),
                      StaffLayout::single_staff(Clef::kBass)},
                     1);
  const Note top    = make_note(*SpelledPitch::create(Letter::kC, 5),
                                *Duration::create(NoteValue::kQuarter, 0));
  const Note bottom = make_note(*SpelledPitch::create(Letter::kC, 3),
                                *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(1, 0).append(top).ok());
  ASSERT_TRUE(fixture.voice(1, 1).append(bottom).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto top_selection = resolve_selection_at(
      fixture.project, layout, note_state(), notehead_origin(layout, top.id));
  ASSERT_TRUE(top_selection.has_value());
  const auto* top_set = std::get_if<NoteheadSet>(&*top_selection);
  ASSERT_NE(top_set, nullptr);
  EXPECT_EQ(top_set->items().front().track, fixture.track_ids[0]);
  EXPECT_EQ(top_set->items().front().stave, fixture.stave_id(0));

  const auto bottom_selection =
      resolve_selection_at(fixture.project, layout, note_state(),
                           notehead_origin(layout, bottom.id));
  ASSERT_TRUE(bottom_selection.has_value());
  const auto* bottom_set = std::get_if<NoteheadSet>(&*bottom_selection);
  ASSERT_NE(bottom_set, nullptr);
  EXPECT_EQ(bottom_set->items().front().track, fixture.track_ids[1]);
  EXPECT_EQ(bottom_set->items().front().stave, fixture.stave_id(1));
  EXPECT_TRUE(validate_selection(fixture.project, *top_selection).empty());
  EXPECT_TRUE(validate_selection(fixture.project, *bottom_selection).empty());
}

TEST(SelectionResolverTest,
     MultiVoiceResolutionPicksTheOwningVoiceNotTheArmedOne) {
  Fixture    fixture(1);
  const Note voice_two_note =
      make_note(*SpelledPitch::create(Letter::kC, 4),
                *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(voice_two_note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  // Voice 1 is armed, but the click lands on a Voice 2 note.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1),
                           notehead_origin(layout, voice_two_note.id));
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Shared suffix constants: a hit whose id disagrees with its resolved
// entity kind is rejected rather than trusted (defends the "silent drift"
// concern the shared kHitSuffix* constants exist to prevent). ----

TEST(SelectionResolverTest,
     ANoteheadRoleHitWithAnUnrecognizedIdSuffixYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);
  const auto          found =
      std::ranges::find_if(layout.hit_regions, [&](const HitRegion& region) {
        return region.id.value == note.id.to_string() + "/notehead/hit";
      });
  ASSERT_NE(found, layout.hit_regions.end());
  found->id = NotationId{note.id.to_string() + "/bogus/hit"};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest, AStemSuffixedHitPointingAtARestYieldsNoSelection) {
  Fixture    fixture(1);
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(rest).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = rest_origin(layout, rest);
  // Fabricate a "stem" hit region over the rest glyph's own position,
  // naming the rest as its semantic entity -- something no real emitter
  // does (rests never grow stems), simulating a future engraver defect
  // that reuses the "stem" suffix incorrectly.
  layout.hit_regions.push_back(
      HitRegion{NotationId{rest.id.to_string() + "/stem/hit"},
                NotationId{rest.id.to_string()}, HitRole::kEvent,
                NotationRect{point.x - 1.0, point.y - 1.0, 2.0, 2.0}, 100,
                std::nullopt, std::nullopt});

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

// ---- HIGH-1 regression: an insertion caret is never built at an onset the
// domain itself would reject (validate_insertion_caret_set,
// graphscore/domain/selection.cpp). ----

TEST(SelectionResolverTest,
     EmptyArmedVoiceOverMultipleMeasuresNeverYieldsAnIllegalCaret) {
  // Two measures, nothing in any voice at all: TrackLane::total_length() ==
  // 0, so the only legal caret position is 0. The hypothetical
  // measure-aligned rest fill preview_note_entry/resolve_insertion_site
  // read for an empty voice puts an onset at the start of measure 1 (== 1),
  // which is neither position 0 nor total_length() nor an existing event
  // boundary -- exactly the shape HIGH-1 found unguarded.
  Fixture fixture(2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 0, 1);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  if (selection.has_value()) {
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Reachability proof: staff 0's second measure is not itself unresolvable
  // geometry (resolve_staff_at/resolve_measure_at rejecting this x/y) --
  // the same click into that same measure, once the armed voice actually
  // carries content there, resolves to a legal caret. That pins the
  // contract check above to the caret-legality guard this test exists for,
  // not to the click never reaching resolve_insertion_site's onset scan at
  // all.
  Fixture        reachable(2);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 8; ++index) {
    ASSERT_TRUE(reachable.voice().append(make_rest(quarter)).ok());
  }
  const NotationLayout reachable_layout = require_layout(
      layout_notation(reachable.project, reachable.node_id, metrics));
  const NotationPoint reachable_point = staff_center(reachable_layout, 0, 1);
  const auto          reachable_selection = resolve_selection_at(
      reachable.project, reachable_layout, note_state(), reachable_point);
  ASSERT_TRUE(reachable_selection.has_value());
  EXPECT_TRUE(
      validate_selection(reachable.project, *reachable_selection).empty());
}

TEST(SelectionResolverTest,
     EmptyArmedVoiceAlongsideAFullSecondVoiceNeverYieldsAnIllegalCaret) {
  // Voice 1 (armed) stays entirely empty; voice 2 carries eight quarters
  // across both measures, so TrackLane::total_length() == 2 -- the
  // "ordinary start a new voice on an existing score" shape the reviewer
  // called out, not a corner case. Legality is still evaluated against the
  // armed voice's own content, not voice 2's.
  Fixture        fixture(2);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 8; ++index) {
    ASSERT_TRUE(fixture.voice(2).append(make_rest(quarter)).ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 0, 1);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  if (selection.has_value()) {
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Reachability proof: the identical click point resolves to a legal
  // caret when voice 2 -- the voice that actually has content there -- is
  // armed instead, so the nullopt above is the caret-legality guard
  // rejecting voice 1's empty content at this onset, not this point going
  // unresolved by resolve_staff_at/resolve_measure_at.
  const auto voice2_selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(voice2_selection.has_value());
  EXPECT_TRUE(validate_selection(fixture.project, *voice2_selection).empty());
}

TEST(SelectionResolverTest,
     EmptyArmedVoiceStillYieldsACaretAtTheVeryFirstMeasure) {
  // The fix must not overcorrect: position 0 is always legal
  // (validate_insertion_caret_set), so the very first click into a
  // never-touched voice must still produce a usable caret.
  Fixture fixture(2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 0, 0);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  EXPECT_EQ(caret_set->items().front().position, Rational(0));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- HIGH-2 regression: a hit's owning staff comes from resolve_hit_entity
// scanning the layout, not from re-deriving a staff from the click point --
// a ledger-line notehead legitimately falls outside resolve_staff_at's own
// bounded proximity window. ----

TEST(SelectionResolverTest,
     ALedgerLineNoteheadBeyondTheStaffProximityWindowStillResolves) {
  Fixture    fixture(1, Clef::kBass);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 6),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kNotehead);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, note.id);
  EXPECT_EQ(notehead_set->items().front().track, fixture.track_ids[0]);
  EXPECT_EQ(notehead_set->items().front().stave, fixture.stave_id());
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}
}  // namespace
