// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_selection.hpp>

#include "hit_resolution_internal.hpp"
#include "measure_math.hpp"
#include "notation_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

// The project-wide score order resolve_range_selection's staff-range rule
// uses: Project::active_tracks() order, then each track's own
// StaffLayout::staves() order -- the identical order layout_notation itself
// assigns to every system's own StaffSystemLayout list (see its own
// "std::size_t stave_ordinal" loop above), so every system carries this
// exact same ordered staff set project-wide. Named "_pairs" because the
// public score_ordered_staves (graphscore_notation.hpp) exposes this same
// traversal to callers outside this file as a vector of MeasureScope
// instead; that function delegates to this one rather than copying the
// rule. (extend_measure_selection, below, restates this order in its own
// in-place filter loop rather than calling this helper -- it pre-dates
// this helper and is the one remaining sibling not yet routed through
// it.)
[[nodiscard]] std::vector<std::pair<TrackId, StaveId>>
score_ordered_stave_pairs(const Project& project) {
  std::vector<std::pair<TrackId, StaveId>> order;
  for (const Track& track : project.active_tracks()) {
    for (const StaveDefinition& stave_definition : track.layout().staves()) {
      order.emplace_back(track.id(), stave_definition.id);
    }
  }
  return order;
}

// The grid resolve_range_selection quantizes a raw pixel-derived musical
// time to -- see that function's own contract comment
// (graphscore/notation/graphscore_notation.hpp) for the rationale and this
// grid's limits.
constexpr std::int64_t kRangeSelectionGridDenominator = 192;

[[nodiscard]] Rational quantize_range_time(double time) noexcept {
  const double scaled =
      time * static_cast<double>(kRangeSelectionGridDenominator);
  const auto numerator = static_cast<std::int64_t>(std::llround(scaled));
  return Rational(numerator) / Rational(kRangeSelectionGridDenominator);
}

// Whether `content` has any event whose own [onset, onset + duration)
// extent overlaps the half-open `span` -- resolve_range_selection's
// content-driven substitute for voice geometry, which VoiceLayout does not
// carry (see its own comment in the public header).
[[nodiscard]] bool voice_overlaps_span(const VoiceContent& content,
                                       const MusicalSpan&  span) {
  Rational onset;
  for (const VoiceEvent& event : content.events()) {
    const Rational event_end = onset + event_duration(event).resolved();
    if (onset < span.end && event_end > span.start) {
      return true;
    }
    onset = event_end;
  }
  return false;
}

// The shared tail every ArbitraryRangeSet-producing entry point in this
// file reaches once it has a resolved span and a pair of score-order staff
// endpoints: resolve_range_selection's own pointer-drag path (see its own
// contract comment in graphscore_notation.hpp), resolve_range_selection_spec,
// extend_range_selection, and extend_range_selection_staff_scope. Applies
// the staff-range rule (every staff between `first_staff` and `last_staff`,
// inclusive, in `project`'s own score order, order-insensitive via
// std::minmax -- see score_ordered_stave_pairs above), the voice-inclusion rule
// (voice_overlaps_span, one item per staff/voice pair with overlapping
// content), and the lane/voices-missing skip behavior described in
// resolve_range_selection's own original comment. Does not itself run
// validate_selection -- each public caller decides whether and how to
// validate, so this helper's own behavior stays identical regardless of
// that choice: resolve_range_selection itself never validates (unchanged
// from before this helper existed), while resolve_range_selection_spec,
// extend_range_selection, and extend_range_selection_staff_scope all do --
// see each one's own contract comment in graphscore_notation.hpp for its
// own validation return-value contract.
//
// Returns std::nullopt when span.start is not strictly less than span.end,
// when either staff endpoint cannot be found in the project's own score-
// ordered staff list, or when no voice anywhere in the resolved staff
// range has any content overlapping `span` (ArbitraryRangeSet::create
// itself rejects an empty item vector).
[[nodiscard]] std::optional<Selection> build_arbitrary_range_selection(
    const Project& project, const Node& node, NodeId node_id,
    const MusicalSpan& span, std::pair<TrackId, StaveId> first_staff,
    std::pair<TrackId, StaveId> last_staff) {
  if (!(span.start < span.end)) {
    return std::nullopt;
  }

  const std::vector<std::pair<TrackId, StaveId>> score_order =
      score_ordered_stave_pairs(project);
  const auto first_position = std::ranges::find(score_order, first_staff);
  const auto last_position  = std::ranges::find(score_order, last_staff);
  if (first_position == score_order.end() ||
      last_position == score_order.end()) {
    return std::nullopt;
  }
  const auto [lower, upper] = std::minmax(first_position, last_position);

  std::vector<ArbitraryRangeItem> items;
  for (auto it = lower; it <= upper; ++it) {
    const auto [track_id, stave_id] = *it;
    const TrackLane* lane           = node.lane(track_id);
    // Unlike resolve_insertion_site's identical two checks, a staff in the
    // resolved range whose own TrackLane/StaveVoices cannot be found is
    // skipped rather than failing the whole query: a lane==nullptr staff is
    // unreachable for any layout satisfying resolve_range_selection's own
    // contract (layout_notation hard-fails with
    // NotationLayoutError::kLaneMissing before producing such a layout),
    // and voices==nullptr is semantically empty (layout_notation treats a
    // missing StaveVoices as kEmptyVoices), so skipping this one staff is
    // equivalent to scanning its four voices and finding no overlapping
    // content in any of them. This reasoning is pointer-drag-specific
    // (layout_notation's own guarantee), but every caller of this helper
    // -- resolve_range_selection_spec, extend_range_selection, and
    // extend_range_selection_staff_scope included -- ultimately operates on
    // a Project/Node pair layout_notation would also accept, so the same
    // guarantee applies uniformly.
    if (lane == nullptr) {
      continue;
    }
    const StaveVoices* voices = lane->stave(stave_id);
    if (voices == nullptr) {
      continue;
    }
    for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
         ++voice_index) {
      // Voice::create fails only when index < kMin || index > kMax
      // (graphscore/core/voice.hpp), and this loop's own bounds keep
      // voice_index within [kMin, kMax] throughout, so this can never fail.
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
      const Voice         voice   = *Voice::create(voice_index);
      const VoiceContent& content = voices->voice(voice);
      if (!voice_overlaps_span(content, span)) {
        continue;
      }
      items.push_back(
          ArbitraryRangeItem{node_id, track_id, stave_id, voice, span});
    }
  }

  std::optional<ArbitraryRangeSet> set =
      ArbitraryRangeSet::create(std::move(items));
  if (!set.has_value()) {
    return std::nullopt;
  }
  return Selection{*std::move(set)};
}

// Derives the score-order staff extent of an existing ArbitraryRangeSet's
// own items -- the lowest and highest score-order position among all of
// `items`'s own (track, stave) pairs -- for extend_range_selection's own
// "hold the current staff range fixed" need; that is this function's only
// caller. extend_range_selection_staff_scope never needs this
// reconstruction: it takes both new staff endpoints explicitly instead of
// holding one implicitly fixed (see its own contract comment in
// graphscore_notation.hpp for why). See extend_range_selection's own
// contract comment (graphscore_notation.hpp) for the documented limitation
// this reconstruction carries.
//
// Returns std::nullopt when `items` is empty, or when any item names a
// (track, stave) pair absent from `score_order` (a stale/fabricated set).
[[nodiscard]] std::optional<
    std::pair<std::pair<TrackId, StaveId>, std::pair<TrackId, StaveId>>>
existing_range_staff_extent(
    const std::vector<ArbitraryRangeItem>&          items,
    const std::vector<std::pair<TrackId, StaveId>>& score_order) {
  std::optional<std::size_t> lower_index;
  std::optional<std::size_t> upper_index;
  for (const ArbitraryRangeItem& item : items) {
    const auto position =
        std::ranges::find(score_order, std::pair{item.track, item.stave});
    if (position == score_order.end()) {
      return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(position - score_order.begin());
    if (!lower_index.has_value() || index < *lower_index) {
      lower_index = index;
    }
    if (!upper_index.has_value() || index > *upper_index) {
      upper_index = index;
    }
  }
  if (!lower_index.has_value() || !upper_index.has_value()) {
    return std::nullopt;
  }
  return std::pair{score_order[*lower_index], score_order[*upper_index]};
}

// Verifies every item in `items` shares one NodeId and one MusicalSpan --
// the alignment precondition extend_range_selection and
// extend_range_selection_staff_scope both require of `existing`, mirroring
// extend_measure_selection's own alignment check on FullMeasureSet.
[[nodiscard]] bool arbitrary_range_set_is_aligned(
    const std::vector<ArbitraryRangeItem>& items) {
  if (items.empty()) {
    return false;
  }
  const NodeId      anchor_node = items.front().node;
  const MusicalSpan anchor_span = items.front().span;
  return std::ranges::all_of(items, [&](const ArbitraryRangeItem& item) {
    return item.node == anchor_node && item.span == anchor_span;
  });
}

}  // namespace

std::optional<Selection> resolve_range_selection(const Project&        project,
                                                 const NotationLayout& layout,
                                                 NotationPoint         anchor,
                                                 NotationPoint         focus) {
  if (!finite_point(anchor) || !finite_point(focus)) {
    return std::nullopt;
  }

  const std::optional<ResolvedStaffSite> anchor_site =
      resolve_staff_at(layout, anchor);
  const std::optional<ResolvedStaffSite> focus_site =
      resolve_staff_at(layout, focus);
  if (!anchor_site.has_value() || !focus_site.has_value()) {
    return std::nullopt;
  }

  const MeasureLayout* anchor_measure =
      resolve_measure_at(*anchor_site->system, anchor);
  const MeasureLayout* focus_measure =
      resolve_measure_at(*focus_site->system, focus);
  if (anchor_measure == nullptr || focus_measure == nullptr) {
    return std::nullopt;
  }

  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return std::nullopt;
  }
  const MeasureMap& measures = timeline->measures();
  if (anchor_measure->ordinal >= measures.measure_count() ||
      focus_measure->ordinal >= measures.measure_count()) {
    return std::nullopt;
  }

  const double anchor_staff_space = anchor_site->staff->bounds.height / 4.0;
  const double focus_staff_space  = focus_site->staff->bounds.height / 4.0;
  const double anchor_time        = time_at_x(
      measures, anchor_measure->ordinal, anchor.x, anchor_measure->bounds.x,
      anchor_measure->bounds.width, anchor_staff_space);
  const double focus_time = time_at_x(
      measures, focus_measure->ordinal, focus.x, focus_measure->bounds.x,
      focus_measure->bounds.width, focus_staff_space);

  const Rational start = quantize_range_time(std::min(anchor_time, focus_time));
  const Rational end   = quantize_range_time(std::max(anchor_time, focus_time));
  const MusicalSpan span{start, end};

  return build_arbitrary_range_selection(
      project, *node, layout.node_id, span,
      {anchor_site->staff->track_id, anchor_site->staff->stave_id},
      {focus_site->staff->track_id, focus_site->staff->stave_id});
}

std::optional<Selection> resolve_range_selection_spec(
    const Project& project, const RangeSelectionSpec& spec) {
  const Node* node = project.find_node(spec.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return std::nullopt;
  }
  if (spec.span.start < Rational(0) ||
      spec.span.end > timeline->measures().total_length()) {
    return std::nullopt;
  }

  std::optional<Selection> selection = build_arbitrary_range_selection(
      project, *node, spec.node_id, spec.span,
      {spec.first_staff.track_id, spec.first_staff.stave_id},
      {spec.last_staff.track_id, spec.last_staff.stave_id});
  if (!selection.has_value()) {
    return std::nullopt;
  }
  if (!validate_selection(project, *selection).empty()) {
    return std::nullopt;
  }
  return selection;
}

std::optional<Selection> extend_range_selection(
    const Project& project, const ArbitraryRangeSet& existing, RangeEdge edge,
    Rational time) {
  if (!arbitrary_range_set_is_aligned(existing.items())) {
    return std::nullopt;
  }
  const NodeId      node_id      = existing.items().front().node;
  const MusicalSpan current_span = existing.items().front().span;

  const Node* node = project.find_node(node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return std::nullopt;
  }

  Rational new_start = current_span.start;
  Rational new_end   = current_span.end;
  if (edge == RangeEdge::kStart) {
    new_start = time;
  } else {
    new_end = time;
  }
  if (new_start > new_end) {
    std::swap(new_start, new_end);
  }
  if (new_start < Rational(0) ||
      new_end > timeline->measures().total_length()) {
    return std::nullopt;
  }
  const MusicalSpan new_span{new_start, new_end};

  const std::vector<std::pair<TrackId, StaveId>> score_order =
      score_ordered_stave_pairs(project);
  const auto extent =
      existing_range_staff_extent(existing.items(), score_order);
  if (!extent.has_value()) {
    return std::nullopt;
  }

  std::optional<Selection> selection = build_arbitrary_range_selection(
      project, *node, node_id, new_span, extent->first, extent->second);
  if (!selection.has_value()) {
    return std::nullopt;
  }
  if (!validate_selection(project, *selection).empty()) {
    return std::nullopt;
  }
  return selection;
}

std::optional<Selection> extend_range_selection_staff_scope(
    const Project& project, const ArbitraryRangeSet& existing,
    MeasureScope first_staff, MeasureScope last_staff) {
  if (!arbitrary_range_set_is_aligned(existing.items())) {
    return std::nullopt;
  }
  const NodeId      node_id = existing.items().front().node;
  const MusicalSpan span    = existing.items().front().span;

  const Node* node = project.find_node(node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  if (node->timeline() == nullptr) {
    return std::nullopt;
  }

  std::optional<Selection> selection = build_arbitrary_range_selection(
      project, *node, node_id, span,
      {first_staff.track_id, first_staff.stave_id},
      {last_staff.track_id, last_staff.stave_id});
  if (!selection.has_value()) {
    return std::nullopt;
  }
  if (!validate_selection(project, *selection).empty()) {
    return std::nullopt;
  }
  return selection;
}

std::vector<MeasureScope> score_ordered_staves(const Project& project) {
  const std::vector<std::pair<TrackId, StaveId>> pairs =
      score_ordered_stave_pairs(project);
  std::vector<MeasureScope> order;
  order.reserve(pairs.size());
  for (const auto& [track_id, stave_id] : pairs) {
    order.push_back(MeasureScope{track_id, stave_id});
  }
  return order;
}

bool SelectionDragState::begin(ActiveTool tool, NotationPoint anchor) noexcept {
  // Cancel any in-progress drag before validating — a rejected begin()
  // must leave no stale drag state.  committed_selection_ is preserved.
  cancel();
  if (tool != ActiveTool::kSelection) {
    return false;
  }
  // A non-finite anchor produces a nullopt from update() rather than
  // silently entering a drag that can never resolve.
  if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y)) {
    return false;
  }
  active_tool_ = tool;
  anchor_      = anchor;
  dragging_    = true;
  return true;
}

std::optional<Selection> SelectionDragState::update(
    const Project& project, const NotationLayout& layout, NotationPoint focus) {
  if (!dragging_) {
    return std::nullopt;
  }
  live_extent_ = resolve_range_selection(project, layout, anchor_, focus);
  return live_extent_;
}

std::optional<Selection> SelectionDragState::commit() noexcept {
  if (!dragging_) {
    return std::nullopt;
  }
  committed_selection_ = std::move(live_extent_);
  live_extent_.reset();
  dragging_ = false;
  return committed_selection_;
}

void SelectionDragState::cancel() noexcept {
  dragging_ = false;
  anchor_   = {};
  live_extent_.reset();
  // committed_selection_ is untouched: its previous value persists until the
  // next commit() or set_committed_selection().
}

static_assert(std::is_nothrow_move_constructible_v<std::optional<Selection>>);
static_assert(std::is_nothrow_move_assignable_v<std::optional<Selection>>);

void SelectionDragState::set_committed_selection(
    std::optional<Selection> selection) noexcept {
  // Cancel any in-progress drag before storing: if a drag stayed live, a
  // later commit() would overwrite this keyboard-set selection with a
  // stale live extent, so the two selection sources -- pointer drag and
  // keyboard/accessible range extension -- could disagree about what is
  // selected. Cancelling here means a subsequent commit() finds no drag in
  // progress at the moment of this call and returns nullopt rather than
  // clobbering `selection`. A begin() that starts a new drag afterward owns
  // committed_selection() again like any other drag -- including clearing
  // it via commit() if that new drag's own update() never resolves.
  cancel();
  committed_selection_ = std::move(selection);
}

// ---- build_range_highlight_rects -------------------------------------------

std::vector<NotationRect> build_range_highlight_rects(
    const Selection& selection, const Project& project,
    const NotationLayout& layout) {
  const auto* range_set = std::get_if<ArbitraryRangeSet>(&selection);
  if (range_set == nullptr || range_set->items().empty()) {
    return {};
  }

  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return {};
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return {};
  }
  const MeasureMap& measures = timeline->measures();

  // Accumulate span intervals per (system, staff, measure).  Multiple items
  // (e.g. two voices on the same staff, both selected) project through the
  // same measure.  Intervals that are identical, overlapping, or touching
  // are coalesced to avoid duplicate translucent geometry; disjoint intervals
  // produce separate rectangles so gaps are not filled.
  using Key = std::tuple<std::size_t, std::size_t, std::size_t>;
  std::map<Key, std::vector<std::pair<double, double>>> intervals;

  for (std::size_t sys_idx = 0; sys_idx < layout.systems.size(); ++sys_idx) {
    const SystemLayout& system = layout.systems[sys_idx];
    for (std::size_t staff_idx = 0; staff_idx < system.staves.size();
         ++staff_idx) {
      const StaffSystemLayout& staff = system.staves[staff_idx];
      for (const ArbitraryRangeItem& item : range_set->items()) {
        if (item.node != layout.node_id) {
          continue;
        }
        if (item.track != staff.track_id || item.stave != staff.stave_id) {
          continue;
        }

        const MusicalSpan& span        = item.span;
        const double       staff_space = staff.bounds.height / 4.0;
        for (const MeasureLayout& measure : system.measures) {
          if (measure.ordinal >= measures.measure_count()) {
            continue;
          }
          const Rational measure_start =
              measures.measure_start(measure.ordinal);
          const Rational measure_end =
              measure_start + measures.measure_length(measure.ordinal);
          if (!(measure_start < span.end) || !(span.start < measure_end)) {
            continue;
          }

          const Rational clamped_start = std::max(span.start, measure_start);
          const Rational clamped_end   = std::min(span.end, measure_end);
          const double   start_x =
              position_x(measures, measure.ordinal, measure.bounds.width,
                         clamped_start, measure.bounds.x, staff_space);
          const double end_x =
              position_x(measures, measure.ordinal, measure.bounds.width,
                         clamped_end, measure.bounds.x, staff_space);

          const Key key{sys_idx, staff_idx, measure.ordinal};
          intervals[key].emplace_back(start_x, end_x);
        }
      }
    }
  }

  // Coalesce overlapping or touching intervals per key into disjoint rects.
  std::vector<NotationRect> rects;
  for (auto& [key, raw] : intervals) {
    // Sort by start_x, then end_x for determinism.
    std::ranges::sort(raw);

    // Coalesce: merge intervals that overlap or touch.
    std::vector<std::pair<double, double>> coalesced;
    for (const auto& [start_x, end_x] : raw) {
      if (coalesced.empty()) {
        coalesced.emplace_back(start_x, end_x);
      } else {
        auto& last = coalesced.back();
        if (start_x <= last.second) {
          // Overlapping or touching: extend the last interval.
          last.second = std::max(last.second, end_x);
        } else {
          // Disjoint: start a new interval.
          coalesced.emplace_back(start_x, end_x);
        }
      }
    }

    const std::size_t        sys_idx   = std::get<0>(key);
    const std::size_t        staff_idx = std::get<1>(key);
    const StaffSystemLayout& staff = layout.systems[sys_idx].staves[staff_idx];
    for (const auto& [start_x, end_x] : coalesced) {
      rects.push_back(NotationRect{start_x, staff.bounds.y, end_x - start_x,
                                   staff.bounds.height});
    }
  }
  return rects;
}

}  // namespace graphscore
