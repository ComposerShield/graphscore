// SPDX-License-Identifier: Apache-2.0

#include "selection_tool_handler.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {

// Moves one edge of the committed selection's shared span to `time`,
// holding the other edge fixed and swapping if the moved edge crosses
// it -- graphscore::extend_range_selection's own documented behavior.
//
// This is built on resolve_range_selection_spec rather than on
// extend_range_selection directly, and passes first_staff_/last_staff_
// explicitly, so an edge-only extension can still recover a staff that
// extend_range_selection's own item-derived fixed-range reconstruction
// would lose (see extend_range_selection's own doc comment on
// graphscore_notation.hpp, and first_staff_/last_staff_'s own comment
// below). The span-move-and-swap arithmetic here is bookkeeping over
// which edge moves, not a musical resolution rule; the actual staff
// walk, voice-overlap check, and bounds validation are all
// resolve_range_selection_spec's. Because this bypasses
// extend_range_selection, the move-and-swap-and-reject-zero-length rule
// implemented below duplicates extend_range_selection's own copy
// (src/notation/notation_range_selection.cpp) instead of reusing it -- if
// that function's crossing semantics ever change, this copy must change with
// it too.
bool SelectionToolHandler::extend_range_edge(graphscore::RangeEdge edge,
                                             graphscore::Rational  time) {
  const auto* existing = current_range_set();
  if (existing == nullptr || existing->items().empty()) {
    return false;
  }
  if (!first_staff_.has_value() || !last_staff_.has_value()) {
    return false;
  }
  const graphscore::MusicalSpan& current_span = existing->items().front().span;
  graphscore::Rational           start        = current_span.start;
  graphscore::Rational           end          = current_span.end;
  if (edge == graphscore::RangeEdge::kStart) {
    start = time;
  } else {
    end = time;
  }
  if (start > end) {
    std::swap(start, end);
  }
  if (start == end) {
    return false;
  }
  const graphscore::NodeId node_id  = existing->items().front().node;
  auto                     resolved = graphscore::resolve_range_selection_spec(
      project_, graphscore::RangeSelectionSpec{
                    node_id, graphscore::MusicalSpan{start, end}, *first_staff_,
                    *last_staff_});
  if (!resolved.has_value()) {
    return false;
  }
  const auto* resolved_set =
      std::get_if<graphscore::ArbitraryRangeSet>(&*resolved);
  if (resolved_set == nullptr || resolved_set->items().empty()) {
    return false;
  }
  // extend_range_selection's own contract: the edge that now carries
  // `time` is not necessarily `edge` itself once a crossing swap has
  // happened, so recompute focus_edge_ from where `time` actually
  // landed in the resolved span rather than assuming it is unchanged.
  const graphscore::MusicalSpan& resolved_span =
      resolved_set->items().front().span;
  focus_edge_ = (resolved_span.start == time) ? graphscore::RangeEdge::kStart
                                              : graphscore::RangeEdge::kEnd;
  drag_.set_committed_selection(std::move(resolved));
  update_highlight();
  return true;
}

// Replaces the committed selection's staff scope with the score-order
// range spanning `first_staff` and `last_staff`, holding the shared span
// fixed. `first_staff`/`last_staff` become the new first_staff_/
// last_staff_ exactly as passed -- not reconstructed from the resulting
// items -- so a staff between them carrying no content overlapping the
// current span is still tracked as part of the scope.
bool SelectionToolHandler::extend_range_staff_scope(
    graphscore::MeasureScope first_staff, graphscore::MeasureScope last_staff) {
  const auto* existing = current_range_set();
  if (existing == nullptr) {
    return false;
  }
  auto extended = graphscore::extend_range_selection_staff_scope(
      project_, *existing, first_staff, last_staff);
  if (!extended.has_value()) {
    return false;
  }
  drag_.set_committed_selection(std::move(extended));
  first_staff_ = first_staff;
  last_staff_  = last_staff;
  update_highlight();
  return true;
}

// The start edge at Rational(0).
bool SelectionToolHandler::select_to_node_start() {
  return extend_range_edge(graphscore::RangeEdge::kStart,
                           graphscore::Rational(0));
}

// The end edge at the committed selection's own node's timeline total
// length. resolve_range_selection_spec (reached through
// extend_range_edge) rejects an out-of-bounds span rather than clamping
// it, so the exact node end must be looked up rather than guessed.
bool SelectionToolHandler::select_to_node_end() {
  const auto* existing = current_range_set();
  if (existing == nullptr || existing->items().empty()) {
    return false;
  }
  const graphscore::Node* node =
      project_.find_node(existing->items().front().node);
  if (node == nullptr) {
    return false;
  }
  const graphscore::NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return false;
  }
  return extend_range_edge(graphscore::RangeEdge::kEnd,
                           timeline->measures().total_length());
}

// Widens the staff scope by one staff in score order: direction < 0
// moves first_staff_ one position earlier (Shift+Up), direction > 0
// moves last_staff_ one position later (Shift+Down). Running off either
// end of score_ordered_staves is a no-op returning false, not a wrap.
bool SelectionToolHandler::step_staff_scope(int direction) {
  if (!first_staff_.has_value() || !last_staff_.has_value()) {
    return false;
  }
  const std::vector<graphscore::MeasureScope> order =
      graphscore::score_ordered_staves(project_);
  const auto first_it = std::find(order.begin(), order.end(), *first_staff_);
  const auto last_it  = std::find(order.begin(), order.end(), *last_staff_);
  if (first_it == order.end() || last_it == order.end()) {
    return false;
  }
  auto first_index =
      static_cast<std::size_t>(std::distance(order.begin(), first_it));
  auto last_index =
      static_cast<std::size_t>(std::distance(order.begin(), last_it));
  if (direction < 0) {
    if (first_index == 0) {
      return false;
    }
    --first_index;
  } else {
    if (last_index + 1 >= order.size()) {
      return false;
    }
    ++last_index;
  }
  return extend_range_staff_scope(order[first_index], order[last_index]);
}

// Derives first_staff_/last_staff_ from the committed selection's own
// items, in score order: the lowest and highest score-order position
// among them. Called only after a pointer-drag commit, where there is no
// caller-supplied staff scope to track directly. A staff at the extreme
// of the drag whose own voices carried no overlapping content at commit
// time contributes no item, so it is unrecoverable here -- the same
// limitation extend_range_selection's own doc comment describes for
// reconstructing a held-fixed staff range from items. That limitation is
// inherent to deriving endpoints from items and is not fixed by this
// sub-phase; extend_range_staff_scope's own explicit first_staff/
// last_staff parameters are what let a later edge-only extension
// preserve a staff this function itself could lose.
void SelectionToolHandler::sync_staff_endpoints_from_committed() {
  const auto* set = current_range_set();
  if (set == nullptr || set->items().empty()) {
    first_staff_.reset();
    last_staff_.reset();
    return;
  }
  const std::vector<graphscore::MeasureScope> order =
      graphscore::score_ordered_staves(project_);
  std::optional<std::size_t> lower;
  std::optional<std::size_t> upper;
  for (const auto& item : set->items()) {
    const graphscore::MeasureScope scope{item.track, item.stave};
    const auto it = std::find(order.begin(), order.end(), scope);
    if (it == order.end()) {
      continue;
    }
    const auto index =
        static_cast<std::size_t>(std::distance(order.begin(), it));
    if (!lower.has_value() || index < *lower) {
      lower = index;
    }
    if (!upper.has_value() || index > *upper) {
      upper = index;
    }
  }
  if (lower.has_value() && upper.has_value()) {
    first_staff_ = order[*lower];
    last_staff_  = order[*upper];
  } else {
    first_staff_.reset();
    last_staff_.reset();
  }
}

// The committed selection's own ArbitraryRangeSet, or nullptr when there
// is no committed selection or it is not that arm. Every accessible
// range control above requires this to be non-null before doing
// anything.
const graphscore::ArbitraryRangeSet* SelectionToolHandler::current_range_set()
    const noexcept {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
}

// The committed selection's own NoteheadSet, or nullptr when there is no
// committed selection or it is not that arm. move_selected_notehead above
// requires exactly this arm with exactly one item.
const graphscore::NoteheadSet* SelectionToolHandler::current_notehead_set()
    const noexcept {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::NoteheadSet>(&*committed);
}

// The committed selection's own ChordSet, or nullptr when there is no
// committed selection or it is not that arm. convert_selection_to_rest
// below accepts this arm alongside NoteheadSet, since "R" converts a
// ChordSet-selected chord to a rest exactly as it converts a
// ChordNote-selected one.
const graphscore::ChordSet* SelectionToolHandler::current_chord_set()
    const noexcept {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::ChordSet>(&*committed);
}

std::optional<graphscore::MeasureScope> SelectionToolHandler::first_staff()
    const noexcept {
  return first_staff_;
}

std::optional<graphscore::MeasureScope> SelectionToolHandler::last_staff()
    const noexcept {
  return last_staff_;
}

}  // namespace graphscore::writer_app
