// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/selection.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

namespace graphscore {

namespace {

// Returns true iff every element is distinct (no adjacent equal pair
// after sorting would suffice, but we keep it simple for small sets).
template <typename T>
bool all_distinct(const std::vector<T>& items) {
  for (std::size_t i = 0; i < items.size(); ++i) {
    for (std::size_t j = i + 1; j < items.size(); ++j) {
      if (items[i] == items[j])
        return false;
    }
  }
  return true;
}

const Track* find_active(const Project& project, TrackId track_id) {
  return project.find_active_track(track_id);
}

bool is_archived(const Project& project, TrackId track_id) {
  return project.find_archived_track(track_id) != nullptr;
}

bool track_exists(const Project& project, TrackId track_id) {
  return find_active(project, track_id) != nullptr ||
         is_archived(project, track_id);
}

bool track_has_stave(const Track& track, StaveId stave_id) {
  for (const auto& def : track.layout().staves()) {
    if (def.id == stave_id)
      return true;
  }
  return false;
}

enum class EntityKind : std::uint8_t {
  kNone,
  kNote,
  kChord,
  kRest,
  kChordNote,
  kGraceNote,
};

EntityKind classify_entity(const VoiceContent& voice, NotationEntityId id) {
  for (const auto& event : voice.events()) {
    if (event_id(event) == id) {
      if (std::holds_alternative<Note>(event))
        return EntityKind::kNote;
      if (std::holds_alternative<Chord>(event))
        return EntityKind::kChord;
      if (std::holds_alternative<Rest>(event))
        return EntityKind::kRest;
    }
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const auto& cn : chord->notes) {
        if (cn.id == id)
          return EntityKind::kChordNote;
      }
    }
  }
  for (const auto& g : voice.grace_groups()) {
    for (const auto& gn : g.notes) {
      if (gn.id == id)
        return EntityKind::kGraceNote;
    }
  }
  return EntityKind::kNone;
}

// Shared helper: validates that node/track/stave/lane exist and track
// is active for items that carry (node, track, stave) scope.  Returns
// a diagnostic on first failure, or nullopt if the chain is intact.
// Does NOT validate voice or entity-kind: callers add those checks.
std::optional<SelectionDiagnostic> validate_track_scope_chain(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    std::size_t item_index) {
  const Node* node = project.find_node(node_id);
  if (!node)
    return SelectionDiagnostic{
        item_index, SelectionDiagnosticCode::kNodeNotFound, "node not found"};

  if (!track_exists(project, track_id))
    return SelectionDiagnostic{
        item_index, SelectionDiagnosticCode::kTrackNotFound, "track not found"};
  if (is_archived(project, track_id))
    return SelectionDiagnostic{item_index,
                               SelectionDiagnosticCode::kTrackArchived,
                               "track is archived"};
  const Track* track = find_active(project, track_id);
  if (!track_has_stave(*track, stave_id))
    return SelectionDiagnostic{item_index,
                               SelectionDiagnosticCode::kStaveNotFound,
                               "stave not in track layout"};

  if (!node->has_lane(track_id))
    return SelectionDiagnostic{
        item_index, SelectionDiagnosticCode::kLaneNotFound, "lane not found"};
  const TrackLane* lane = node->lane(track_id);
  if (!lane->has_stave(stave_id))
    return SelectionDiagnostic{item_index,
                               SelectionDiagnosticCode::kLaneNotFound,
                               "stave not in lane"};
  return std::nullopt;
}

// ---- per-arm validators ----

std::vector<SelectionDiagnostic> validate_notehead_set(const Project& project,
                                                       const NoteheadSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    const auto& item = set.items()[i];

    auto scope_diag = validate_track_scope_chain(project, item.node, item.track,
                                                 item.stave, i);
    if (scope_diag.has_value()) {
      diags.push_back(*scope_diag);
      continue;
    }

    const Node*         node = project.find_node(item.node);
    const TrackLane*    lane = node->lane(item.track);
    const StaveVoices*  sv   = lane->stave(item.stave);
    const VoiceContent& vc   = sv->voice(item.voice);

    const EntityKind kind = classify_entity(vc, item.entity);
    switch (kind) {
      case EntityKind::kNote:
      case EntityKind::kChordNote:
      case EntityKind::kGraceNote:
        break;
      case EntityKind::kNone:
        diags.push_back(
            SelectionDiagnostic{i, SelectionDiagnosticCode::kEntityNotFound,
                                "entity not found in voice"});
        break;
      default:
        diags.push_back(
            SelectionDiagnostic{i, SelectionDiagnosticCode::kWrongEntityKind,
                                "entity is not a notehead"});
        break;
    }
  }
  return diags;
}

std::vector<SelectionDiagnostic> validate_chord_set(const Project&  project,
                                                    const ChordSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    const auto& item = set.items()[i];

    auto scope_diag = validate_track_scope_chain(project, item.node, item.track,
                                                 item.stave, i);
    if (scope_diag.has_value()) {
      diags.push_back(*scope_diag);
      continue;
    }

    const Node*         node = project.find_node(item.node);
    const TrackLane*    lane = node->lane(item.track);
    const StaveVoices*  sv   = lane->stave(item.stave);
    const VoiceContent& vc   = sv->voice(item.voice);

    const EntityKind kind = classify_entity(vc, item.entity);
    if (kind == EntityKind::kNone) {
      diags.push_back(
          SelectionDiagnostic{i, SelectionDiagnosticCode::kEntityNotFound,
                              "entity not found in voice"});
    } else if (kind != EntityKind::kChord) {
      diags.push_back(
          SelectionDiagnostic{i, SelectionDiagnosticCode::kWrongEntityKind,
                              "entity is not a top-level chord"});
    }
  }
  return diags;
}

std::vector<SelectionDiagnostic> validate_full_measure_set(
    const Project& project, const FullMeasureSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    const auto& item = set.items()[i];

    auto scope_diag = validate_track_scope_chain(project, item.node, item.track,
                                                 item.stave, i);
    if (scope_diag.has_value()) {
      diags.push_back(*scope_diag);
      continue;
    }

    const Node*         node     = project.find_node(item.node);
    const NodeTimeline* timeline = node->timeline();
    if (!timeline) {
      diags.push_back(SelectionDiagnostic{
          i, SelectionDiagnosticCode::kNoTimeline, "node has no timeline"});
      continue;
    }

    if (item.measure_index >= timeline->measures().measure_count()) {
      diags.push_back(SelectionDiagnostic{
          i, SelectionDiagnosticCode::kMeasureIndexOutOfRange,
          "measure index out of range"});
      continue;
    }

    // Verify the measure is in the main region, not pickdown.
    const Rational measure_start =
        timeline->measures().measure_start(item.measure_index);
    if (timeline->classify(measure_start) == TimelineRegion::kPickdown) {
      diags.push_back(SelectionDiagnostic{
          i, SelectionDiagnosticCode::kMeasureIndexInPickdown,
          "measure index names pickdown material"});
    }
  }
  return diags;
}

std::vector<SelectionDiagnostic> validate_arbitrary_range_set(
    const Project& project, const ArbitraryRangeSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    const auto& item = set.items()[i];

    auto scope_diag = validate_track_scope_chain(project, item.node, item.track,
                                                 item.stave, i);
    if (scope_diag.has_value()) {
      diags.push_back(*scope_diag);
      continue;
    }

    const Node*         node     = project.find_node(item.node);
    const NodeTimeline* timeline = node->timeline();
    if (!timeline) {
      diags.push_back(SelectionDiagnostic{
          i, SelectionDiagnosticCode::kNoTimeline,
          "node has no timeline, range classification unavailable"});
      continue;
    }

    // classify() handles any valid MusicalSpan (main, pickdown, or
    // crossing); the result is not used for validation — invoking it
    // ensures classification is possible and the span is reachable.
    (void)timeline->classify(item.span);
  }
  return diags;
}

std::vector<SelectionDiagnostic> validate_node_set(const Project& project,
                                                   const NodeSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    if (!project.find_node(set.items()[i].node)) {
      diags.push_back(SelectionDiagnostic{
          i, SelectionDiagnosticCode::kNodeNotFound, "node not found"});
    }
  }
  return diags;
}

std::vector<SelectionDiagnostic> validate_connector_set(
    const Project& project, const ConnectorSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    const auto& item = set.items()[i];
    const Node* node = project.find_node(item.node);
    if (!node) {
      diags.push_back(SelectionDiagnostic{
          i, SelectionDiagnosticCode::kNodeNotFound, "node not found"});
      continue;
    }
    if (!node->find_input(item.connector) &&
        !node->find_output(item.connector)) {
      diags.push_back(
          SelectionDiagnostic{i, SelectionDiagnosticCode::kConnectorNotFound,
                              "connector not found on node"});
    }
  }
  return diags;
}

std::vector<SelectionDiagnostic> validate_insertion_caret_set(
    const Project& project, const InsertionCaretSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    const auto& item = set.items()[i];

    auto scope_diag = validate_track_scope_chain(project, item.node, item.track,
                                                 item.stave, i);
    if (scope_diag.has_value()) {
      diags.push_back(*scope_diag);
      continue;
    }

    const Node*         node = project.find_node(item.node);
    const TrackLane*    lane = node->lane(item.track);
    const StaveVoices*  sv   = lane->stave(item.stave);
    const VoiceContent& vc   = sv->voice(item.voice);

    // Position 0 is always a valid caret position — it is the
    // boundary before the first event even when find_event_index_at
    // returns nullopt (empty voice) and the lane extent is nonzero.
    if (item.position == Rational(0))
      continue;

    // Legal caret positions are event boundaries ∪
    // TrackLane::total_length().  The lane extent may differ from the
    // selected voice's own total_length(), e.g. when another stave or
    // voice extends further.
    const Rational lane_end = lane->total_length();
    if (item.position == lane_end)
      continue;

    if (vc.find_event_index_at(item.position).has_value())
      continue;

    diags.push_back(SelectionDiagnostic{
        i, SelectionDiagnosticCode::kCaretPositionNotBoundary,
        "caret position is not an event boundary or lane total_length()"});
  }
  return diags;
}

struct SelectionValidator {
  const Project& project;

  std::vector<SelectionDiagnostic> operator()(const NoteheadSet& set) const {
    return validate_notehead_set(project, set);
  }

  std::vector<SelectionDiagnostic> operator()(const ChordSet& set) const {
    return validate_chord_set(project, set);
  }

  std::vector<SelectionDiagnostic> operator()(const FullMeasureSet& set) const {
    return validate_full_measure_set(project, set);
  }

  std::vector<SelectionDiagnostic> operator()(
      const ArbitraryRangeSet& set) const {
    return validate_arbitrary_range_set(project, set);
  }

  std::vector<SelectionDiagnostic> operator()(const NodeSet& set) const {
    return validate_node_set(project, set);
  }

  std::vector<SelectionDiagnostic> operator()(const ConnectorSet& set) const {
    return validate_connector_set(project, set);
  }

  std::vector<SelectionDiagnostic> operator()(
      const InsertionCaretSet& set) const {
    return validate_insertion_caret_set(project, set);
  }
};

}  // namespace

// ---- set factories ----

std::optional<NoteheadSet> NoteheadSet::create(
    std::vector<NoteheadItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  return NoteheadSet(std::move(items));
}

std::optional<ChordSet> ChordSet::create(std::vector<ChordItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  return ChordSet(std::move(items));
}

std::optional<FullMeasureSet> FullMeasureSet::create(
    std::vector<FullMeasureItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  return FullMeasureSet(std::move(items));
}

std::optional<ArbitraryRangeSet> ArbitraryRangeSet::create(
    std::vector<ArbitraryRangeItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  for (const auto& item : items) {
    if (!(item.span.start <= item.span.end))
      return std::nullopt;
  }
  return ArbitraryRangeSet(std::move(items));
}

std::optional<NodeSet> NodeSet::create(std::vector<NodeItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  return NodeSet(std::move(items));
}

std::optional<ConnectorSet> ConnectorSet::create(
    std::vector<ConnectorItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  return ConnectorSet(std::move(items));
}

std::optional<InsertionCaretSet> InsertionCaretSet::create(
    std::vector<InsertionCaretItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  return InsertionCaretSet(std::move(items));
}

// ---- validate_selection ----

std::vector<SelectionDiagnostic> validate_selection(
    const Project& project, const Selection& selection) {
  return std::visit(SelectionValidator{project}, selection);
}

}  // namespace graphscore
