// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/delete_measure_command.hpp>
#include <graphscore/domain/insert_measure_command.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_editing.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

// Resolves `selection` to its FullMeasureSet, requiring every item to share
// one anchor NodeId and one measure_index (the same alignment precondition
// extend_measure_selection enforces) and to pass validate_selection.
// Returns nullptr for every other Selection arm, a misaligned set, or a set
// that fails validation.
[[nodiscard]] const FullMeasureSet* resolve_aligned_full_measure_set(
    const Project& project, const Selection& selection) {
  const auto* set = std::get_if<FullMeasureSet>(&selection);
  if (set == nullptr)
    return nullptr;

  const NodeId      anchor_node    = set->items().front().node;
  const std::size_t anchor_measure = set->items().front().measure_index;
  if (set->items().front().measure_count != 1u)
    return nullptr;
  for (const FullMeasureItem& item : set->items()) {
    if (item.node != anchor_node || item.measure_index != anchor_measure ||
        item.measure_count != 1u)
      return nullptr;
  }

  if (!validate_selection(project, selection).empty())
    return nullptr;

  return set;
}

// Builds a FullMeasureSet naming the identical (track, stave) scopes
// `source` names, at `new_measure_index`, in deterministic score order --
// Project::active_tracks() order, then each track's own
// StaffLayout::staves() order, the same ordering rule extend_measure_selection
// applies. Deliberately performs no validate_selection check against
// `project`: `new_measure_index` always refers to the project state
// immediately AFTER the timeline edit that motivated the call, which the
// caller has not yet applied, so a bounds check against the CURRENT
// (pre-edit) timeline would incorrectly reject a legitimate post-edit
// index (e.g. the freshly appended final measure).
[[nodiscard]] std::optional<Selection> remap_full_measure_selection(
    const Project& project, const FullMeasureSet& source,
    std::size_t new_measure_index) {
  const NodeId node_id = source.items().front().node;

  std::vector<FullMeasureItem> reordered;
  for (const Track& track : project.active_tracks()) {
    for (const StaveDefinition& sd : track.layout().staves()) {
      for (const FullMeasureItem& item : source.items()) {
        if (item.track == track.id() && item.stave == sd.id) {
          reordered.push_back(
              FullMeasureItem{node_id, track.id(), sd.id, new_measure_index});
          break;
        }
      }
    }
  }

  std::optional<FullMeasureSet> set =
      FullMeasureSet::create(std::move(reordered));
  if (!set.has_value())
    return std::nullopt;
  return Selection{*std::move(set)};
}

}  // namespace

std::unique_ptr<Command> make_insert_measure_command(const Project&   project,
                                                     const Selection& selection,
                                                     MeasureInsertMode mode) {
  const FullMeasureSet* set =
      resolve_aligned_full_measure_set(project, selection);
  if (set == nullptr)
    return nullptr;

  const NodeId      node_id       = set->items().front().node;
  const Node*       node          = project.find_node(node_id);
  const std::size_t measure_index = set->items().front().measure_index;
  std::size_t       index         = measure_index;
  if (mode == MeasureInsertMode::kAppend)
    index = node->timeline()->measures().measure_count();
  return std::make_unique<InsertMeasureCommand>(node_id, index);
}

std::optional<Selection> selection_after_insert_measure(
    const Project& project, const Selection& selection,
    MeasureInsertMode mode) {
  const FullMeasureSet* set =
      resolve_aligned_full_measure_set(project, selection);
  if (set == nullptr)
    return std::nullopt;

  const NodeId      node_id       = set->items().front().node;
  const Node*       node          = project.find_node(node_id);
  const std::size_t measure_index = set->items().front().measure_index;
  const std::size_t pre_count = node->timeline()->measures().measure_count();
  const std::size_t new_index =
      mode == MeasureInsertMode::kBefore ? measure_index + 1u : pre_count;
  return remap_full_measure_selection(project, *set, new_index);
}

std::unique_ptr<Command> make_delete_measure_command(
    const Project& project, const Selection& selection) {
  const FullMeasureSet* set =
      resolve_aligned_full_measure_set(project, selection);
  if (set == nullptr)
    return nullptr;

  const NodeId      node_id       = set->items().front().node;
  const Node*       node          = project.find_node(node_id);
  const std::size_t measure_index = set->items().front().measure_index;
  if (node->timeline()->measures().measure_count() <= 1u)
    return nullptr;
  return std::make_unique<DeleteMeasureCommand>(node_id, measure_index);
}

std::optional<Selection> selection_after_delete_measure(
    const Project& project, const Selection& selection) {
  const FullMeasureSet* set =
      resolve_aligned_full_measure_set(project, selection);
  if (set == nullptr)
    return std::nullopt;

  const NodeId      node_id       = set->items().front().node;
  const Node*       node          = project.find_node(node_id);
  const std::size_t measure_index = set->items().front().measure_index;
  const std::size_t pre_count = node->timeline()->measures().measure_count();
  if (pre_count <= 1u)
    return std::nullopt;

  const std::size_t new_count = pre_count - 1u;
  const std::size_t new_index =
      measure_index < new_count ? measure_index : new_count - 1u;
  return remap_full_measure_selection(project, *set, new_index);
}

}  // namespace graphscore
