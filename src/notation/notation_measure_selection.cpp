// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_selection.hpp>

#include "notation_geometry.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace graphscore {

namespace {

// The (staff, measure ordinal) pair a HitRole::kStaffMeasure HitResult's own
// semantic_id names, recovered by rebuilding the same id
// staff_measure_semantic_id built at emission for every (staff, measure)
// pair this layout actually carries and comparing each for exact string
// equality against the hit's own semantic_id -- never by parsing the hit id
// itself, for the same reason find_entity_in_voice above cannot: there is
// no way to parse a NodeId/TrackId/StaveId/ordinal back out of a
// NotationId's own string. The scan is O(systems x staves x measures per
// system), the same order of magnitude resolve_marking_record above
// already scans; both are a per-pointer-event query this notation target
// runs on the writer's own click handling, entirely outside the ADR 0003
// §3.1 runtime closure and off any path graphscore_runtime_impl's process
// call reaches, so neither is subject to the realtime rules in AGENTS.md.
// The inner staff_measure_semantic_id call still allocates two fresh
// std::strings per candidate before the comparison; the starts_with check
// below skips straight past every staff other than the hit's own without
// paying that cost, since staff_measure_semantic_id(staff.id, ordinal)'s
// value is always staff.id.value with a "/staff-measure/<ordinal>" suffix
// appended (make_id's own "root/role" concatenation), so a semantic_id
// naming this staff's own measure always carries staff.id.value as a
// literal prefix.
struct ResolvedStaffMeasure {
  const StaffSystemLayout* staff   = nullptr;
  std::size_t              ordinal = 0;
};

[[nodiscard]] std::optional<ResolvedStaffMeasure> resolve_staff_measure_hit(
    const NotationLayout& layout, const NotationId& semantic_id) {
  for (const SystemLayout& system : layout.systems) {
    for (const StaffSystemLayout& staff : system.staves) {
      if (!semantic_id.value.starts_with(staff.id.value)) {
        continue;
      }
      for (const MeasureLayout& measure : system.measures) {
        if (staff_measure_semantic_id(staff.id, measure.ordinal).value ==
            semantic_id.value) {
          return ResolvedStaffMeasure{&staff, measure.ordinal};
        }
      }
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<Selection> resolve_measure_selection_at(
    const Project& project, const NotationLayout& layout, NotationPoint point) {
  if (!finite_point(point)) {
    return std::nullopt;
  }
  const std::optional<HitResult> hit = layout.hit_test(point);
  if (!hit.has_value() || hit->role != HitRole::kStaffMeasure) {
    return std::nullopt;
  }
  const std::optional<ResolvedStaffMeasure> resolved =
      resolve_staff_measure_hit(layout, hit->semantic_id);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  std::optional<FullMeasureSet> set = FullMeasureSet::create(
      {FullMeasureItem{layout.node_id, resolved->staff->track_id,
                       resolved->staff->stave_id, resolved->ordinal}});
  if (!set.has_value()) {
    return std::nullopt;
  }
  Selection selection{*std::move(set)};
  if (!validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  return selection;
}

std::optional<Selection> resolve_measure_range_selection(
    const Project& project, const NotationLayout& layout, NotationPoint anchor,
    NotationPoint focus) {
  if (!finite_point(anchor) || !finite_point(focus)) {
    return std::nullopt;
  }
  const std::optional<HitResult> anchor_hit = layout.hit_test(anchor);
  const std::optional<HitResult> focus_hit  = layout.hit_test(focus);
  if (!anchor_hit.has_value() || !focus_hit.has_value() ||
      anchor_hit->role != HitRole::kStaffMeasure ||
      focus_hit->role != HitRole::kStaffMeasure) {
    return std::nullopt;
  }
  const std::optional<ResolvedStaffMeasure> anchor_measure =
      resolve_staff_measure_hit(layout, anchor_hit->semantic_id);
  const std::optional<ResolvedStaffMeasure> focus_measure =
      resolve_staff_measure_hit(layout, focus_hit->semantic_id);
  if (!anchor_measure.has_value() || !focus_measure.has_value() ||
      anchor_measure->staff->track_id != focus_measure->staff->track_id ||
      anchor_measure->staff->stave_id != focus_measure->staff->stave_id) {
    return std::nullopt;
  }
  const std::size_t first =
      std::min(anchor_measure->ordinal, focus_measure->ordinal);
  const std::size_t last =
      std::max(anchor_measure->ordinal, focus_measure->ordinal);
  std::optional<FullMeasureSet> set = FullMeasureSet::create({FullMeasureItem{
      layout.node_id, anchor_measure->staff->track_id,
      anchor_measure->staff->stave_id, first, last - first + 1}});
  if (!set.has_value()) {
    return std::nullopt;
  }
  Selection selection{*std::move(set)};
  if (!validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  return selection;
}

std::optional<Selection> extend_measure_selection(
    const Project& project, const FullMeasureSet& existing,
    const std::vector<MeasureScope>& additional) {
  if (existing.items().empty()) {
    return std::nullopt;
  }

  // Verify alignment: every item must share one node and one measure_index.
  const NodeId      anchor_node    = existing.items().front().node;
  const std::size_t anchor_measure = existing.items().front().measure_index;
  const std::size_t measure_count  = existing.items().front().measure_count;
  for (const FullMeasureItem& item : existing.items()) {
    if (item.node != anchor_node || item.measure_index != anchor_measure ||
        item.measure_count != measure_count) {
      return std::nullopt;
    }
  }

  const Node* node = project.find_node(anchor_node);
  if (node == nullptr) {
    return std::nullopt;
  }

  // Validates one (track, stave) scope against project + anchor node.
  // Returns false for archived/unknown track, stave not in that track's
  // layout, or missing lane/stave in the node.  This is the single gate
  // both existing and additional scopes pass through; no scope is ever
  // silently skipped.
  const auto scope_is_valid = [&](TrackId track_id, StaveId stave_id) -> bool {
    const Track* track = project.find_active_track(track_id);
    if (track == nullptr)
      return false;
    for (const StaveDefinition& sd : track->layout().staves()) {
      if (sd.id == stave_id) {
        const TrackLane* lane = node->lane(track_id);
        return lane != nullptr && lane->has_stave(stave_id);
      }
    }
    return false;
  };

  // Validate and collect: existing items first, then additional scopes.
  // Every scope is validated before being accepted; any invalid scope
  // fails the whole call.
  std::vector<std::pair<TrackId, StaveId>> scopes;

  const auto add_scope = [&](TrackId track_id, StaveId stave_id) -> bool {
    if (!scope_is_valid(track_id, stave_id))
      return false;
    for (const auto& scope : scopes) {
      if (scope.first == track_id && scope.second == stave_id) {
        return true;  // duplicate, idempotent
      }
    }
    scopes.push_back({track_id, stave_id});
    return true;
  };

  for (const FullMeasureItem& item : existing.items()) {
    if (!add_scope(item.track, item.stave))
      return std::nullopt;
  }
  for (const MeasureScope& scope : additional) {
    if (!add_scope(scope.track_id, scope.stave_id))
      return std::nullopt;
  }

  // Produce items in deterministic score order: active_tracks() order,
  // then each track's own StaffLayout::staves() order.
  std::vector<FullMeasureItem> items;
  for (const Track& track : project.active_tracks()) {
    for (const StaveDefinition& sd : track.layout().staves()) {
      for (const auto& scope : scopes) {
        if (scope.first == track.id() && scope.second == sd.id) {
          items.push_back(FullMeasureItem{anchor_node, track.id(), sd.id,
                                          anchor_measure, measure_count});
          break;
        }
      }
    }
  }

  std::optional<FullMeasureSet> set = FullMeasureSet::create(std::move(items));
  if (!set.has_value()) {
    return std::nullopt;
  }
  Selection selection{*std::move(set)};
  if (!validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  return selection;
}

}  // namespace graphscore
