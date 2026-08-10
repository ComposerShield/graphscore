// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/selection.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
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

std::vector<SelectionDiagnostic> validate_rest_set(const Project& project,
                                                   const RestSet& set) {
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
    } else if (kind != EntityKind::kRest) {
      diags.push_back(
          SelectionDiagnostic{i, SelectionDiagnosticCode::kWrongEntityKind,
                              "entity is not a top-level rest"});
    }
  }
  return diags;
}

// ---- marking arm helpers ----

// True if `id` names one of this voice's own records: an event, an
// embedded ChordNote or GraceNote, or any marking/grace record.  Excludes
// stave-scoped PedalSpans, which live on TrackLane rather than VoiceContent
// — so a PedalSpan id used as a voice-scoped anchor (kDynamic, kHairpin,
// kSlur, kTie, kTuplet, kArticulation) is reported kEntityNotFound rather
// than kWrongEntityKind, even though it does name something in the
// addressed stave.  Used only to choose between kEntityNotFound and
// kWrongEntityKind.
bool voice_contains_entity(const VoiceContent& voice, NotationEntityId id) {
  if (classify_entity(voice, id) != EntityKind::kNone)
    return true;
  for (const auto& dynamic : voice.dynamics()) {
    if (dynamic.id == id)
      return true;
  }
  for (const auto& hairpin : voice.hairpins()) {
    if (hairpin.id == id)
      return true;
  }
  for (const auto& slur : voice.slurs()) {
    if (slur.id == id)
      return true;
  }
  for (const auto& beam : voice.beam_overrides()) {
    if (beam.id == id)
      return true;
  }
  for (const auto& group : voice.grace_groups()) {
    if (group.id == id)
      return true;
  }
  return false;
}

// The diagnostic for an anchor that is not the record the kind requires:
// kWrongEntityKind when the id names something else in scope, otherwise
// kEntityNotFound.
SelectionDiagnostic anchor_mismatch(bool               exists_in_scope,
                                    std::size_t        item_index,
                                    const std::string& expected) {
  if (exists_in_scope)
    return SelectionDiagnostic{
        item_index, SelectionDiagnosticCode::kWrongEntityKind, expected};
  return SelectionDiagnostic{item_index,
                             SelectionDiagnosticCode::kEntityNotFound,
                             "marking anchor not found in the addressed scope"};
}

std::optional<std::size_t> find_event_index(const VoiceContent& voice,
                                            NotationEntityId    id) {
  for (std::size_t i = 0; i < voice.events().size(); ++i) {
    if (event_id(voice.events()[i]) == id)
      return i;
  }
  return std::nullopt;
}

// The tied_to_next flag of the Note or ChordNote `id` names, or nullopt if
// `id` names neither.
std::optional<bool> tie_origin_state(const VoiceContent& voice,
                                     NotationEntityId    id) {
  for (const auto& event : voice.events()) {
    if (const auto* note = std::get_if<Note>(&event)) {
      if (note->id == id)
        return note->tied_to_next;
      continue;
    }
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const auto& notehead : chord->notes) {
        if (notehead.id == id)
          return notehead.tied_to_next;
      }
    }
  }
  return std::nullopt;
}

std::optional<SelectionDiagnostic> validate_voice_marking(
    const VoiceContent& voice, const MarkingItem& item,
    std::size_t item_index) {
  switch (item.kind) {
    case MarkingKind::kDynamic: {
      for (const auto& dynamic : voice.dynamics()) {
        if (dynamic.id == item.anchor)
          return std::nullopt;
      }
      return anchor_mismatch(voice_contains_entity(voice, item.anchor),
                             item_index, "anchor is not a dynamic marking");
    }
    case MarkingKind::kHairpin: {
      for (const auto& hairpin : voice.hairpins()) {
        if (hairpin.id == item.anchor)
          return std::nullopt;
      }
      return anchor_mismatch(voice_contains_entity(voice, item.anchor),
                             item_index, "anchor is not a hairpin");
    }
    case MarkingKind::kSlur: {
      for (const auto& slur : voice.slurs()) {
        if (slur.id == item.anchor)
          return std::nullopt;
      }
      return anchor_mismatch(voice_contains_entity(voice, item.anchor),
                             item_index, "anchor is not a slur");
    }
    case MarkingKind::kArticulation: {
      const std::optional<std::size_t> index =
          find_event_index(voice, item.anchor);
      const std::vector<Articulation>* articulations =
          index.has_value() ? event_articulations(voice.events()[*index])
                            : nullptr;
      if (articulations == nullptr)
        return anchor_mismatch(
            voice_contains_entity(voice, item.anchor), item_index,
            "anchor is not a top-level note or chord carrying articulations");
      assert(item.articulation.has_value());
      if (std::find(articulations->begin(), articulations->end(),
                    *item.articulation) == articulations->end())
        return SelectionDiagnostic{item_index,
                                   SelectionDiagnosticCode::kMarkingNotPresent,
                                   "event does not carry that articulation"};
      return std::nullopt;
    }
    case MarkingKind::kTie: {
      const std::optional<bool> tied = tie_origin_state(voice, item.anchor);
      if (!tied.has_value())
        return anchor_mismatch(voice_contains_entity(voice, item.anchor),
                               item_index, "anchor is not a note or notehead");
      if (!*tied)
        return SelectionDiagnostic{item_index,
                                   SelectionDiagnosticCode::kMarkingNotPresent,
                                   "note is not tied to the following event"};
      return std::nullopt;
    }
    case MarkingKind::kTuplet: {
      const std::optional<std::size_t> index =
          find_event_index(voice, item.anchor);
      if (!index.has_value())
        return anchor_mismatch(voice_contains_entity(voice, item.anchor),
                               item_index, "anchor is not a top-level event");
      const std::optional<TupletRatio> ratio =
          event_duration(voice.events()[*index]).tuplet();
      if (!ratio.has_value())
        return SelectionDiagnostic{item_index,
                                   SelectionDiagnosticCode::kMarkingNotPresent,
                                   "event carries no tuplet"};
      // The bracket and number belong to the run, drawn from its first
      // event; a later event of the same run is not an address for it.
      if (*index > 0) {
        const std::optional<TupletRatio> previous =
            event_duration(voice.events()[*index - 1]).tuplet();
        if (previous.has_value() && *previous == *ratio)
          return SelectionDiagnostic{
              item_index, SelectionDiagnosticCode::kMarkingNotPresent,
              "event is not the first event of its tuplet run"};
      }
      return std::nullopt;
    }
    case MarkingKind::kPedalSpan:
      break;
  }
  // kPedalSpan is stave-scoped: validate_marking_set routes it to
  // validate_pedal_span_marking instead, and MarkingSet::create leaves its
  // `voice` disengaged so there is no voice to search here.  Reaching this
  // line means that routing was broken, so refuse rather than accept.
  return SelectionDiagnostic{item_index,
                             SelectionDiagnosticCode::kWrongEntityKind,
                             "pedal span is not a voice-scoped marking"};
}

std::optional<SelectionDiagnostic> validate_pedal_span_marking(
    const TrackLane& lane, const StaveVoices& stave, const MarkingItem& item,
    std::size_t item_index) {
  const std::vector<PedalSpan>* spans = lane.pedal_spans(item.stave);
  if (spans != nullptr) {
    for (const PedalSpan& span : *spans) {
      if (span.id == item.anchor)
        return std::nullopt;
    }
  }
  // Pedal is stave-scoped, so "elsewhere in scope" means any voice of the
  // addressed stave.
  bool elsewhere = false;
  for (std::uint8_t v = Voice::kMin; !elsewhere && v <= Voice::kMax; ++v) {
    const std::optional<Voice> voice = Voice::create(v);
    if (voice.has_value())
      elsewhere = voice_contains_entity(stave.voice(*voice), item.anchor);
  }
  return anchor_mismatch(elsewhere, item_index, "anchor is not a pedal span");
}

std::vector<SelectionDiagnostic> validate_marking_set(const Project&    project,
                                                      const MarkingSet& set) {
  std::vector<SelectionDiagnostic> diags;
  for (std::size_t i = 0; i < set.items().size(); ++i) {
    const auto& item = set.items()[i];

    auto scope_diag = validate_track_scope_chain(project, item.node, item.track,
                                                 item.stave, i);
    if (scope_diag.has_value()) {
      diags.push_back(*scope_diag);
      continue;
    }

    const Node*        node = project.find_node(item.node);
    const TrackLane*   lane = node->lane(item.track);
    const StaveVoices* sv   = lane->stave(item.stave);

    std::optional<SelectionDiagnostic> diag;
    if (item.kind == MarkingKind::kPedalSpan) {
      diag = validate_pedal_span_marking(*lane, *sv, item, i);
    } else {
      // MarkingSet::create guarantees an engaged voice for every other
      // kind.
      assert(item.voice.has_value());
      diag = validate_voice_marking(sv->voice(*item.voice), item, i);
    }
    if (diag.has_value())
      diags.push_back(*diag);
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

  std::vector<SelectionDiagnostic> operator()(const RestSet& set) const {
    return validate_rest_set(project, set);
  }

  std::vector<SelectionDiagnostic> operator()(const MarkingSet& set) const {
    return validate_marking_set(project, set);
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

std::optional<RestSet> RestSet::create(std::vector<RestItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  return RestSet(std::move(items));
}

std::optional<MarkingSet> MarkingSet::create(std::vector<MarkingItem> items) {
  if (items.empty())
    return std::nullopt;
  if (!all_distinct(items))
    return std::nullopt;
  for (const auto& item : items) {
    if (item.articulation.has_value() !=
        (item.kind == MarkingKind::kArticulation))
      return std::nullopt;
    if (item.voice.has_value() == (item.kind == MarkingKind::kPedalSpan))
      return std::nullopt;
  }
  return MarkingSet(std::move(items));
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
