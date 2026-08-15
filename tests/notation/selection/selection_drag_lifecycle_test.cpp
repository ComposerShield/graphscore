// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- SelectionDragState lifecycle -------------------------------------

TEST(SelectionDragLifecycleTest, BeginWithKSelectionEntersDraggingState) {
  SelectionDragState  drag;
  const NotationPoint anchor{100.0, 200.0};
  EXPECT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  EXPECT_TRUE(drag.is_dragging());
  EXPECT_EQ(drag.anchor(), anchor);
  EXPECT_EQ(drag.active_tool(), ActiveTool::kSelection);
}

TEST(SelectionDragLifecycleTest, BeginWithNonFiniteAnchorIsRejected) {
  SelectionDragState  drag;
  const NotationPoint nan_anchor{std::numeric_limits<double>::quiet_NaN(), 0.0};
  EXPECT_FALSE(drag.begin(ActiveTool::kSelection, nan_anchor));
  EXPECT_FALSE(drag.is_dragging());
}

TEST(SelectionDragLifecycleTest, BeginWithNonSelectionToolIsRejected) {
  SelectionDragState  drag;
  const NotationPoint anchor{100.0, 200.0};
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  EXPECT_FALSE(drag.is_dragging());
}

TEST(SelectionDragLifecycleTest, UpdateResolvesLiveExtentAndReturnsIt) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, focus);
  ASSERT_TRUE(live.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*live);
  ASSERT_NE(set, nullptr);
  EXPECT_EQ(set->items().size(), 1u);

  // live_extent() returns the same resolved Selection.
  ASSERT_TRUE(drag.live_extent().has_value());
  EXPECT_EQ(*drag.live_extent(), *live);

  // validate the result through the domain.
  EXPECT_TRUE(validate_selection(fixture.project, *live).empty());
}

TEST(SelectionDragLifecycleTest, UpdateWithoutBeginReturnsNullopt) {
  SelectionDragState drag;
  // No fixture needed -- update() returns nullopt before touching
  // project or layout.
  Fixture              fixture(1);
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint focus{100.0, 200.0};
  EXPECT_FALSE(drag.update(fixture.project, layout, focus).has_value());
}

TEST(SelectionDragLifecycleTest, CommitReturnsLastLiveExtentAndClearsDragging) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, focus);
  ASSERT_TRUE(live.has_value());

  const std::optional<Selection> committed = drag.commit();
  EXPECT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, *live);
  EXPECT_FALSE(drag.is_dragging());

  // committed_selection() survives the clear.
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *live);

  // live_extent() is cleared.
  EXPECT_FALSE(drag.live_extent().has_value());
}

TEST(SelectionDragLifecycleTest, CommitWithoutSuccessfulUpdateReturnsNullopt) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  SelectionDragState drag;
  ASSERT_TRUE(
      drag.begin(ActiveTool::kSelection, NotationPoint{-10'000.0, -10'000.0}));
  // update() returns nullopt because the anchor is off-stave.
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, NotationPoint{-10'001.0, -10'001.0});
  EXPECT_FALSE(live.has_value());

  const std::optional<Selection> committed = drag.commit();
  EXPECT_FALSE(committed.has_value());
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest,
     CancelClearsDragButPreservesCommittedSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  const std::optional<Selection> committed = drag.commit();
  ASSERT_TRUE(committed.has_value());

  // Start a second drag and cancel it.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  drag.cancel();
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());

  // The first commit's result is still there.
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *committed);
}

TEST(SelectionDragLifecycleTest,
     BeginWhileDraggingCancelsPreviousAndStartsNew) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor1 = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint anchor2{anchor1.x + 50.0, anchor1.y};

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor1));
  ASSERT_TRUE(drag.is_dragging());

  // A second begin() cancels the first and starts a new drag.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor2));
  EXPECT_TRUE(drag.is_dragging());
  EXPECT_EQ(drag.anchor(), anchor2);
  // The first drag's state is gone.
  EXPECT_FALSE(drag.live_extent().has_value());
}

TEST(SelectionDragLifecycleTest,
     ReleaseWithoutUpdateDoesNotLeakAStaleSelection) {
  // If the user presses, never moves (update() never called), and releases:
  // commit() returns nullopt and no selection leaks.
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> committed = drag.commit();
  EXPECT_FALSE(committed.has_value());
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest, NulloptUpdatePreservesDragState) {
  // A failing update() still leaves the drag in progress so the user can
  // move back to a valid position.
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));

  // Move to an off-stave point -- resolution fails.
  EXPECT_FALSE(
      drag.update(fixture.project, layout, NotationPoint{-10'000.0, -10'000.0})
          .has_value());
  // But the drag is still in progress.
  EXPECT_TRUE(drag.is_dragging());

  // A subsequent valid update succeeds.
  const NotationPoint            focus = measure_right_edge(layout, 0, 0, 0);
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, focus);
  ASSERT_TRUE(live.has_value());
  EXPECT_TRUE(drag.is_dragging());
}

TEST(SelectionDragLifecycleTest, LiveExtentPreservesResolvedSpanOrdering) {
  // A forward drag (left → right) and a backward drag (right → left) over
  // the same two endpoints produce identical selections: both resolve to
  // the same start→end span because resolve_range_selection orders the
  // span by musical time, not by spatial drag direction.
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint left  = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint right = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState forward_drag;
  ASSERT_TRUE(forward_drag.begin(ActiveTool::kSelection, left));
  const auto forward = forward_drag.update(fixture.project, layout, right);
  ASSERT_TRUE(forward.has_value());

  SelectionDragState backward_drag;
  ASSERT_TRUE(backward_drag.begin(ActiveTool::kSelection, right));
  const auto backward = backward_drag.update(fixture.project, layout, left);
  ASSERT_TRUE(backward.has_value());

  EXPECT_EQ(*forward, *backward);

  // Both directions resolve to the exact same span: the full measure
  // [0, 1).  Assert the exact values, not only equality.
  const MusicalSpan kExpected{Rational(0), Rational(1)};
  {
    const auto* fwd_set = std::get_if<ArbitraryRangeSet>(&*forward);
    ASSERT_NE(fwd_set, nullptr);
    ASSERT_EQ(fwd_set->items().size(), 1u);
    EXPECT_EQ(fwd_set->items().front().span, kExpected);
  }
  {
    const auto* bwd_set = std::get_if<ArbitraryRangeSet>(&*backward);
    ASSERT_NE(bwd_set, nullptr);
    ASSERT_EQ(bwd_set->items().size(), 1u);
    EXPECT_EQ(bwd_set->items().front().span, kExpected);
  }
}

TEST(SelectionDragLifecycleTest, CommittedSelectionPersistsAcrossToolChanges) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  const std::optional<Selection> first = drag.commit();
  ASSERT_TRUE(first.has_value());

  // After commit, the committed selection is available even across
  // subsequent tool operations.
  drag.cancel();
  ASSERT_TRUE(drag.committed_selection().has_value());

  // Starting a new drag with a non-selection tool does not overwrite
  // committed_selection().
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *first);
}

TEST(SelectionDragLifecycleTest,
     BeginWithNonSelectionToolWhileDraggingCancelsDrag) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.is_dragging());

  // A begin() with a non-Selection tool cancels the drag and returns false.
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());
  // committed_selection_ was never set — remains nullopt.
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest,
     BeginWithNonFiniteAnchorWhileDraggingCancelsDrag) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor     = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint nan_anchor = {std::numeric_limits<double>::quiet_NaN(),
                                    0.0};

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.is_dragging());

  // A begin() with a non-finite anchor cancels the drag and returns false.
  EXPECT_FALSE(drag.begin(ActiveTool::kSelection, nan_anchor));
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());
  // committed_selection_ was never set — remains nullopt.
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest,
     RejectedBeginWhileDraggingPreservesCommittedSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  // First drag: begin, update, commit — produces a committed selection.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  const std::optional<Selection> committed = drag.commit();
  ASSERT_TRUE(committed.has_value());

  // Second drag: begin.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.is_dragging());

  // Rejected begin() with a non-Selection tool cancels the drag and
  // preserves the first drag's committed selection.
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  EXPECT_FALSE(drag.is_dragging());
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *committed);
}

// ---- SelectionDragState::set_committed_selection -----------------------

TEST(SelectionDragLifecycleTest, SetCommittedSelectionWithNoDragInProgress) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(selection.has_value());

  SelectionDragState drag;
  drag.set_committed_selection(selection);
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *selection);
}

TEST(SelectionDragLifecycleTest,
     SetCommittedSelectionWithNulloptClearsAPreviousSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  ASSERT_TRUE(drag.commit().has_value());
  ASSERT_TRUE(drag.committed_selection().has_value());

  drag.set_committed_selection(std::nullopt);
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest, SetCommittedSelectionMidDragCancelsTheDrag) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  ASSERT_TRUE(drag.is_dragging());

  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         keyboard_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff, staff});
  ASSERT_TRUE(keyboard_selection.has_value());

  drag.set_committed_selection(keyboard_selection);
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *keyboard_selection);
}

// If set_committed_selection did not cancel the in-progress drag first, this
// commit() would resolve to the drag's own live extent (span [0, 1), from
// the pointer drag over measure 0 below) and overwrite the keyboard-set
// selection with it. keyboard_selection deliberately names a different span
// ([1, 2), measure 1) so that overwrite -- were the cancel-first invariant
// removed -- would be visible in committed_selection() rather than
// accidentally matching by coincidence.
TEST(SelectionDragLifecycleTest,
     SetCommittedSelectionMidDragPreventsALaterCommitFromOverwritingIt) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> live_extent =
      drag.update(fixture.project, layout, focus);
  ASSERT_TRUE(live_extent.has_value());

  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         keyboard_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(1), Rational(2)},
                         staff, staff});
  ASSERT_TRUE(keyboard_selection.has_value());
  ASSERT_NE(*keyboard_selection, *live_extent);
  drag.set_committed_selection(keyboard_selection);

  const std::optional<Selection> committed = drag.commit();
  EXPECT_FALSE(committed.has_value());
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *keyboard_selection);
}

TEST(SelectionDragLifecycleTest, SetCommittedSelectionSurvivesALaterCancel) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(selection.has_value());

  SelectionDragState drag;
  drag.set_committed_selection(selection);
  drag.cancel();
  EXPECT_FALSE(drag.is_dragging());
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *selection);
}

// set_committed_selection()'s cancel-first guarantee covers only the drag in
// progress at the time of the call. A begin()/update()/commit() cycle that
// starts afterward owns committed_selection() again like any other drag,
// including the case where its own update() never resolves a valid extent --
// commit() then clears committed_selection() to std::nullopt exactly as it
// would for a selection commit() itself produced, silently discarding the
// keyboard-set selection.
TEST(SelectionDragLifecycleTest,
     SetCommittedSelectionThenFailedDragCommitClearsIt) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);

  const MeasureScope staff{fixture.track_ids[0], fixture.stave_id()};
  const auto         keyboard_selection = resolve_range_selection_spec(
      fixture.project,
      RangeSelectionSpec{fixture.node_id, MusicalSpan{Rational(0), Rational(1)},
                         staff, staff});
  ASSERT_TRUE(keyboard_selection.has_value());

  SelectionDragState drag;
  drag.set_committed_selection(keyboard_selection);
  ASSERT_TRUE(drag.committed_selection().has_value());

  // A later drag begins, but its update() never resolves an extent (the
  // focus point is off-stave) -- confirm that genuinely fails before relying
  // on it below.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, NotationPoint{-10'000.0, -10'000.0});
  ASSERT_FALSE(live.has_value());

  // commit() still resolves the drag lifecycle -- and clears the
  // keyboard-set selection, even though this drag never produced one of its
  // own.
  const std::optional<Selection> committed = drag.commit();
  EXPECT_FALSE(committed.has_value());
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.committed_selection().has_value());
}
}  // namespace
