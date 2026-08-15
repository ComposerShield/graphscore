// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- Defect Family 2 (M5-phase-16h): controlled isolation tests that
// prove each target guard (onset, staff) individually prevents an
// otherwise-eligible armed-voice override.  Each test constructs a scenario
// where every precondition except the guard under test passes -- same
// priority, equal area, containing point, resolvable chord, armed
// alternative present -- then asserts the exact winner.  The test must be
// designed so that removing the guard would change the outcome.

// Onset guard: the intended winner (lexically smaller id) at onset 0, the
// armed alternative at onset quarter.  Inject the alternative's column with
// exact equal bounds at the winner's position.  All other guards pass.
TEST(SelectionResolverTest, OnsetGuardPreventsArmedOverride) {
  Fixture     fixture(1);
  const auto  notes = two_chord_notes();
  const Chord a = make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  const Chord b = make_chord(*Duration::create(NoteValue::kWhole, 0),
                             two_chord_notes(graphscore::Accidental::kNatural));
  const bool  a_wins             = a.id.to_string() < b.id.to_string();
  const NotationEntityId win_id  = a_wins ? a.id : b.id;
  const NotationEntityId lose_id = a_wins ? b.id : a.id;

  // Winner at onset 0 (voice 1, unarmed in the default note state).
  ASSERT_TRUE(fixture.voice(1).append(a_wins ? a : b).ok());
  // Loser at onset quarter in voice 2 (after a quarter rest).
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(rest).ok());
  ASSERT_TRUE(fixture.voice(2).append(a_wins ? b : a).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* win_col  = find_hit_region(layout, column_hit_id(win_id));
  const HitRegion* lose_col = find_hit_region(layout, column_hit_id(lose_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_NE(lose_col, nullptr);
  EXPECT_EQ(win_col->priority, lose_col->priority);
  EXPECT_DOUBLE_EQ(win_col->bounds.width * win_col->bounds.height,
                   lose_col->bounds.width * lose_col->bounds.height);
  EXPECT_EQ(win_col->owner_system_id, lose_col->owner_system_id);
  EXPECT_EQ(win_col->owner_staff_id, lose_col->owner_staff_id);

  const NotationPoint point{win_col->bounds.x + win_col->bounds.width * 0.5,
                            win_col->bounds.y + win_col->bounds.height * 0.5};
  ASSERT_TRUE(win_col->bounds.contains(point));

  // Verify deterministic winner: smaller semantic id column wins hit_test.
  const auto pre_hit = layout.hit_test(point);
  ASSERT_TRUE(pre_hit.has_value());
  EXPECT_EQ(pre_hit->id, win_col->id);

  // Inject a synthetic at the winner's position with the loser's semantic
  // and ownership.  Same system, staff, priority, exact equal area, point
  // contained — every guard except onset passes.
  HitRegion synthetic{NotationId{"syn/onset-guard/notehead-column/hit"},
                      lose_col->semantic_id,
                      HitRole::kEvent,
                      win_col->bounds,
                      win_col->priority,
                      lose_col->owner_system_id,
                      lose_col->owner_staff_id};
  layout.hit_regions.push_back(synthetic);

  // Voice 2 armed.  Without the onset guard this would override; the
  // onset guard keeps the winner.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Staff guard: the intended winner (lexically smaller id) on upper staff,
// the armed alternative on lower staff.  Inject the alternative's column
// with exact equal bounds at the winner's position while retaining distinct
// owner_staff_id.  All other guards pass.
TEST(SelectionResolverTest, StaffGuardPreventsArmedOverride) {
  Fixture     fixture({StaffLayout::grand_staff()}, 1);
  const auto  notes = two_chord_notes();
  const Chord a = make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  const Chord b = make_chord(*Duration::create(NoteValue::kWhole, 0),
                             two_chord_notes(graphscore::Accidental::kNatural));
  const bool  a_wins             = a.id.to_string() < b.id.to_string();
  const NotationEntityId win_id  = a_wins ? a.id : b.id;
  const NotationEntityId lose_id = a_wins ? b.id : a.id;

  // Winner on upper staff (voice 1, stave 0).
  ASSERT_TRUE(fixture.voice(1).append(a_wins ? a : b).ok());
  // Loser on lower staff (voice 2, stave 1).
  auto& lower_voice = fixture.voice(2, 0, 1);
  ASSERT_TRUE(lower_voice.append(a_wins ? b : a).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* win_col  = find_hit_region(layout, column_hit_id(win_id));
  const HitRegion* lose_col = find_hit_region(layout, column_hit_id(lose_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_NE(lose_col, nullptr);
  ASSERT_TRUE(win_col->owner_staff_id.has_value());
  ASSERT_TRUE(lose_col->owner_staff_id.has_value());
  EXPECT_NE(*win_col->owner_staff_id, *lose_col->owner_staff_id);
  EXPECT_EQ(*win_col->owner_system_id, *lose_col->owner_system_id);
  EXPECT_EQ(win_col->priority, lose_col->priority);
  EXPECT_DOUBLE_EQ(win_col->bounds.width * win_col->bounds.height,
                   lose_col->bounds.width * lose_col->bounds.height);

  const NotationPoint point{win_col->bounds.x + win_col->bounds.width * 0.5,
                            win_col->bounds.y + win_col->bounds.height * 0.5};
  ASSERT_TRUE(win_col->bounds.contains(point));

  const auto pre_hit = layout.hit_test(point);
  ASSERT_TRUE(pre_hit.has_value());
  EXPECT_EQ(pre_hit->id, win_col->id);

  // Inject a synthetic at the winner's position with the loser's semantic
  // and lower-staff ownership.  Same system, priority, exact equal area,
  // point contained.  Only the staff guard differs.
  HitRegion synthetic{NotationId{"syn/staff-guard/notehead-column/hit"},
                      lose_col->semantic_id,
                      HitRole::kEvent,
                      win_col->bounds,
                      win_col->priority,
                      lose_col->owner_system_id,
                      lose_col->owner_staff_id};
  layout.hit_regions.push_back(synthetic);

  // Voice 2 armed (lower staff).  The staff guard must reject.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// A stemless chord column genuinely emitted in a later system carries
// that later SystemLayout's id and resolves correctly.  This is a direct
// integration check, not a synthetic guard-isolation test: the layout is
// produced by the real engraver with enough content to force a system
// break, and the column's owner_system_id is read from the emitted
// HitRegion rather than hand-built.
TEST(SelectionResolverTest, ColumnInLaterSystemCarriesThatSystemsId) {
  Fixture        fixture(3);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  // Fill measure 0 with enough rests to push a stemless chord to a
  // later measure, where it lands in a later system.
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(fixture.voice(1).append(make_rest(quarter)).ok());
  }

  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice(1).append(chord).ok());

  NotationLayoutOptions options;
  options.system_width = 60.0;
  options.left_margin  = 1.0;
  options.right_margin = 1.0;
  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_GE(layout.systems.size(), 2u);

  const HitRegion* col = find_hit_region(layout, column_hit_id(chord.id));
  ASSERT_NE(col, nullptr);
  ASSERT_TRUE(col->owner_system_id.has_value());

  // The column was emitted in a later system -- verify the
  // owner_system_id matches that system's actual id and does not point
  // to system 0.
  const SystemLayout* owner_system = nullptr;
  for (const SystemLayout& sys : layout.systems) {
    if (sys.id == *col->owner_system_id) {
      owner_system = &sys;
      break;
    }
  }
  ASSERT_NE(owner_system, nullptr);
  EXPECT_NE(*col->owner_system_id, layout.systems[0].id);
  EXPECT_GE(owner_system->first_measure, 1u);

  // Click on the column and verify correct resolution.
  const NotationPoint point{col->bounds.x + col->bounds.width * 0.5,
                            col->bounds.y + col->bounds.height * 0.5};
  ASSERT_TRUE(col->bounds.contains(point));

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, chord.id);
  EXPECT_EQ(chord_set->items().front().track, fixture.track_ids[0]);
  EXPECT_EQ(chord_set->items().front().voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Forged/stale-owner guard: a synthetic column that copies the winner's
// owner_system_id/owner_staff_id but carries a semantic_id from a chord
// that actually lives on a different staff/system.  The genuine winner's
// semantic id is deterministically arranged to be lexically smaller so
// that hit_test always returns the genuine region after injection,
// removing any dependence on UUID ordering.  Owner-constrained resolution
// on a forged-winner synthetic (tested via fail-closed) is the separate
// backstop.
TEST(SelectionResolverTest, ForgedOwnerMetadataBlockedByConstrainedResolution) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff()},
                  1);

  // Construct chords first so the lexically smaller semantic id can be
  // deterministically assigned to the genuine winner.
  const std::vector<ChordNote> upper_notes = two_chord_notes();
  const std::vector<ChordNote> other_notes =
      two_chord_notes(graphscore::Accidental::kSharp);
  const Chord upper_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), upper_notes);
  const Chord other_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), other_notes);

  // Deterministic assignment: lexically smaller semantic id to the
  // genuine winner (unarmed voice 1 on track 0).
  const bool upper_wins =
      upper_chord.id.to_string() < other_chord.id.to_string();
  const NotationEntityId win_id  = upper_wins ? upper_chord.id : other_chord.id;
  const NotationEntityId lose_id = upper_wins ? other_chord.id : upper_chord.id;

  // Winner on track 0 (voice 1, unarmed in this test).
  ASSERT_TRUE(
      fixture.voice(1, 0).append(upper_wins ? upper_chord : other_chord).ok());
  // Armed chord on track 1 (voice 2).
  ASSERT_TRUE(
      fixture.voice(2, 1).append(upper_wins ? other_chord : upper_chord).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);

  const HitRegion* win_col = find_hit_region(layout, column_hit_id(win_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_TRUE(win_col->owner_system_id.has_value());
  ASSERT_TRUE(win_col->owner_staff_id.has_value());

  const NotationPoint point{win_col->bounds.x + win_col->bounds.width * 0.5,
                            win_col->bounds.y + win_col->bounds.height * 0.5};
  ASSERT_TRUE(win_col->bounds.contains(point));

  // Copy every needed winner property before push_back, so no pointer
  // into layout.hit_regions is retained across vector mutation.
  const NotationId                win_id_copy   = win_col->id;
  const NotationRect              win_bounds    = win_col->bounds;
  const int                       win_priority  = win_col->priority;
  const std::optional<NotationId> win_owner_sys = win_col->owner_system_id;
  const std::optional<NotationId> win_owner_stf = win_col->owner_staff_id;

  // Synthetic: copies the winner's owner IDs (track 0's staff) but names
  // the losing chord as its semantic entity.  The synthetic's id prefix
  // 'z' sorts after every hex digit and letter, guaranteeing the genuine
  // winner's own column id always wins the lexicographic tie-break.
  HitRegion synthetic{NotationId{"zzz/forged-owner/notehead-column/hit"},
                      NotationId{{lose_id.to_string()}},
                      HitRole::kEvent,
                      win_bounds,
                      win_priority,
                      win_owner_sys,
                      win_owner_stf};
  layout.hit_regions.push_back(synthetic);

  // Post-injection: the genuine column wins hit_test deterministically
  // because its semantic_id is the lexically smaller of the two chord
  // ids (assigned above) and its column id sorts before 'z'.
  const auto post_hit = layout.hit_test(point);
  ASSERT_TRUE(post_hit.has_value());
  EXPECT_EQ(post_hit->id, win_id_copy);

  // Voice 2 armed (track 1).  Resolver selects the genuine winner.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Area guard: same staff/system/onset, the intended geometric winner
// (lexically smaller id) has equal area with the armed alternative.
// Inject the alternative's column at the winner's position with width
// perturbed by std::nextafter so the area differs by < 1e-9.
// The exact-area guard preserves the winner.
TEST(SelectionResolverTest, TinyAreaDeltaPreservesWinner) {
  Fixture     fixture(1);
  const auto  notes = two_chord_notes();
  const Chord a = make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  const Chord b = make_chord(*Duration::create(NoteValue::kWhole, 0),
                             two_chord_notes(graphscore::Accidental::kNatural));
  const bool  a_wins             = a.id.to_string() < b.id.to_string();
  const NotationEntityId win_id  = a_wins ? a.id : b.id;
  const NotationEntityId lose_id = a_wins ? b.id : a.id;
  ASSERT_TRUE(fixture.voice(1).append(a_wins ? a : b).ok());
  ASSERT_TRUE(fixture.voice(2).append(a_wins ? b : a).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* win_col  = find_hit_region(layout, column_hit_id(win_id));
  const HitRegion* lose_col = find_hit_region(layout, column_hit_id(lose_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_NE(lose_col, nullptr);
  EXPECT_EQ(win_col->priority, lose_col->priority);
  const double win_area = win_col->bounds.width * win_col->bounds.height;
  EXPECT_DOUBLE_EQ(win_area, lose_col->bounds.width * lose_col->bounds.height);
  EXPECT_EQ(win_col->owner_system_id, lose_col->owner_system_id);
  EXPECT_EQ(win_col->owner_staff_id, lose_col->owner_staff_id);

  const double overlap_left = std::max(win_col->bounds.x, lose_col->bounds.x);
  const double overlap_right =
      std::min(win_col->bounds.x + win_col->bounds.width,
               lose_col->bounds.x + lose_col->bounds.width);
  ASSERT_LT(overlap_left, overlap_right);
  const NotationPoint point{
      (overlap_left + overlap_right) * 0.5,
      win_col->bounds.y + win_col->bounds.height * 0.5,
  };
  ASSERT_TRUE(win_col->bounds.contains(point));
  ASSERT_TRUE(lose_col->bounds.contains(point));

  const auto pre_hit = layout.hit_test(point);
  ASSERT_TRUE(pre_hit.has_value());
  EXPECT_EQ(pre_hit->id, win_col->id);

  // Copy every needed winner property before any erase/push_back, so
  // no pointer/reference/iterator into layout.hit_regions is retained
  // across vector mutation.
  const NotationId                win_id_copy    = win_col->id;
  const NotationRect              win_bounds     = win_col->bounds;
  const int                       win_priority   = win_col->priority;
  const NotationId                lose_semantic  = lose_col->semantic_id;
  const std::optional<NotationId> lose_owner_sys = lose_col->owner_system_id;
  const std::optional<NotationId> lose_owner_stf = lose_col->owner_staff_id;
  const auto                      lose_pos       = std::ranges::find_if(
      layout.hit_regions,
      [&](const HitRegion& r) { return r.id == lose_col->id; });
  ASSERT_NE(lose_pos, layout.hit_regions.end());
  layout.hit_regions.erase(lose_pos);

  // Replace it with a synthetic that has the same semantic/ownership but
  // a width perturbed upwards by the fewest ULPs that move the area at
  // all, so the area differs by > 0 but < 1e-9.  One ULP of the width is
  // only a fraction of an ULP of the product, so whether it survives the
  // multiplication depends on mantissa alignment; step until it does
  // rather than assume a single step suffices.
  NotationRect alt_bounds = win_bounds;
  do {
    alt_bounds.width = std::nextafter(alt_bounds.width,
                                      std::numeric_limits<double>::infinity());
  } while (alt_bounds.width * alt_bounds.height == win_area);
  HitRegion synthetic{NotationId{"syn/tiny-area/notehead-column/hit"},
                      lose_semantic,
                      HitRole::kEvent,
                      alt_bounds,
                      win_priority,
                      lose_owner_sys,
                      lose_owner_stf};
  layout.hit_regions.push_back(synthetic);

  const double synth_area = alt_bounds.width * alt_bounds.height;
  EXPECT_NE(synth_area, win_area);
  EXPECT_GT(synth_area, win_area);
  const double delta = synth_area - win_area;
  EXPECT_GT(delta, 0.0);
  EXPECT_LT(delta, 1e-9);

  // Post-mutation: hit_test still returns the smaller-area winner,
  // asserted against the pre-mutation copy.
  const auto post_hit = layout.hit_test(point);
  ASSERT_TRUE(post_hit.has_value());
  EXPECT_EQ(post_hit->id, win_id_copy);

  // Voice 2 armed.  The exact-area guard rejects the synthetic; winner
  // remains unchanged.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}
}  // namespace
