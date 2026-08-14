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

// Re-lays out the node after a successful domain mutation through the
// retained incremental layout cache (M5-phase-8), then publishes the
// rasterised surface to the shell and commits the new layout as visible.
// The publish happens BEFORE the layout is committed, so a publish failure
// leaves layout_ (and therefore the highlight geometry) unchanged for the
// caller to roll back. Returns false when the cache refresh fails, when no
// invalidation can be computed, or when the surface publish fails; true
// means the new layout is committed and published.
//
// Requires metrics_ to have been set (run() and the notehead-move test both
// do); returns true otherwise so the range-selection paths that never move
// a notehead remain no-ops.
//
// When the caller does not pass `forced`, the invalidation is derived from
// the committed selection's own single-item NoteheadSet, and the call is a
// no-op (returns true without touching layout_) for any other committed
// selection -- move_selected_notehead and step_selected_accidental rely on
// this, since both already require that same NoteheadSet arm before ever
// calling refresh_layout(). A caller that passes an explicit `forced`
// invalidation (delete_selected_notehead, convert_selection_to_rest) always
// reaches the actual cache refresh below, regardless of which selection
// arm is committed -- notably including a committed ChordSet, which
// current_notehead_set() cannot see.
bool SelectionToolHandler::refresh_layout(
    std::optional<graphscore::NotationInvalidation> forced) {
  if (metrics_ == nullptr) {
    return true;
  }
  if (!forced.has_value()) {
    const auto* set = current_notehead_set();
    if (set == nullptr || set->items().size() != 1u) {
      return true;
    }
    forced = notehead_invalidation(set->items()[0]);
  }
  if (!forced.has_value()) {
    return false;
  }
  graphscore::IncrementalNotationLayoutResult result = layout_cache_.update(
      project_, layout_.node_id, *metrics_, layout_options_, {*forced});
  if (!result || !result.layout.has_value()) {
    return false;
  }
  last_layout_work_ = result.work;
  if (publish_surface_) {
    const graphscore::ShellResult publish = publish_surface_(*result.layout);
    if (!publish.ok()) {
      return false;
    }
  }
  layout_ = std::move(*result.layout);
  update_highlight();
  return true;
}

// The exact invalidation scope the incremental layout cache needs after the
// selected notehead's pitch moves: the notehead's own measure as
// kLocalContent for a local move, or the full connected tie-chain measure
// range as kCrossMeasureSpan when the chain crosses a barline. The measure
// range comes from graphscore::notehead_move_scope, the shared source of
// truth the MoveNoteheadCommand also walks, so the layout invalidation can
// never drift from the mutation's actual extent.
std::optional<graphscore::NotationInvalidation>
SelectionToolHandler::notehead_invalidation(
    const graphscore::NoteheadItem& item) const {
  const std::optional<graphscore::NoteheadMoveScope> scope =
      graphscore::notehead_move_scope(project_, item);
  if (!scope.has_value()) {
    return std::nullopt;
  }
  const graphscore::NotationInvalidationKind kind =
      scope->first_measure == scope->last_measure
          ? graphscore::NotationInvalidationKind::kLocalContent
          : graphscore::NotationInvalidationKind::kCrossMeasureSpan;
  return graphscore::NotationInvalidation{kind, scope->first_measure,
                                          scope->last_measure};
}

// The exact invalidation scope the incremental layout cache needs after an
// interval entry: the selected notehead's own measure as kLocalContent.
// Adding a notehead to an event never moves or re-ties any existing
// notehead, so no cross-measure span is touched and the tie-chain range
// notehead_invalidation walks is deliberately not consulted. The measure
// comes from graphscore::notehead_measure_index, the same resolution the
// interval command and audition use for the key signature, so the
// invalidation can never drift from the mutation's actual extent.
std::optional<graphscore::NotationInvalidation>
SelectionToolHandler::interval_invalidation(
    const graphscore::NoteheadItem& item) const {
  const std::optional<std::size_t> measure =
      graphscore::notehead_measure_index(project_, item);
  if (!measure.has_value()) {
    return std::nullopt;
  }
  return graphscore::NotationInvalidation{
      graphscore::NotationInvalidationKind::kLocalContent, *measure, *measure};
}

// Resolves node/track/stave/voice to the addressed VoiceContent, or
// nullptr if any link in that chain does not exist. Shared by both arms
// of convert_to_rest_invalidation below so the same four-step lookup is
// not duplicated per arm.
const graphscore::VoiceContent* SelectionToolHandler::resolve_voice(
    graphscore::NodeId node, graphscore::TrackId track,
    graphscore::StaveId stave, graphscore::Voice voice) const {
  const graphscore::Node* node_ptr = project_.find_node(node);
  if (node_ptr == nullptr) {
    return nullptr;
  }
  const graphscore::TrackLane* lane = node_ptr->lane(track);
  if (lane == nullptr) {
    return nullptr;
  }
  const graphscore::StaveVoices* stave_voices = lane->stave(stave);
  if (stave_voices == nullptr) {
    return nullptr;
  }
  return &stave_voices->voice(voice);
}

// The union, across every pitch in `chord`, of that pitch's own
// notehead_move_scope -- the shared core behind
// convert_to_rest_invalidation below, for both of its arms that convert
// a whole chord to a rest. Converting an entire chord to a rest can
// normalize away an incoming tie on any of its pitches (not just one),
// so the invalidated range must cover every pitch's own connected tie
// chain, not just a single representative one. Returns std::nullopt when
// `chord` has no notes or any pitch's own scope cannot be computed.
std::optional<graphscore::NotationInvalidation>
SelectionToolHandler::chord_notes_invalidation(const graphscore::Chord& chord,
                                               graphscore::NodeId       node,
                                               graphscore::TrackId      track,
                                               graphscore::StaveId      stave,
                                               graphscore::Voice voice) const {
  if (chord.notes.empty()) {
    return std::nullopt;
  }
  std::optional<std::size_t> first;
  std::optional<std::size_t> last;
  for (const graphscore::ChordNote& note : chord.notes) {
    const graphscore::NoteheadItem pitch_item{node, track, stave, voice,
                                              note.id};
    const std::optional<graphscore::NoteheadMoveScope> scope =
        graphscore::notehead_move_scope(project_, pitch_item);
    if (!scope.has_value()) {
      return std::nullopt;
    }
    first = first.has_value() ? std::min(*first, scope->first_measure)
                              : scope->first_measure;
    last  = last.has_value() ? std::max(*last, scope->last_measure)
                             : scope->last_measure;
  }
  if (!first.has_value() || !last.has_value()) {
    return std::nullopt;
  }
  const graphscore::NotationInvalidationKind kind =
      *first == *last ? graphscore::NotationInvalidationKind::kLocalContent
                      : graphscore::NotationInvalidationKind::kCrossMeasureSpan;
  return graphscore::NotationInvalidation{kind, *first, *last};
}

// convert_selection_to_rest's invalidation scope, covering both of its
// accepted arms as one operation -- which they are, by this phase's own
// settled design: a NoteheadSet item whose entity names a ChordNote
// converts the WHOLE containing chord exactly as a directly-addressed
// ChordSet item does (deliberately unlike delete's per-pitch semantics),
// so both cases invalidate chord_notes_invalidation's union over every
// pitch in that chord, not just the addressed pitch's own tie chain. A
// NoteheadSet item naming a top-level Note instead has no sibling
// pitches and falls back to the single-pitch notehead_invalidation. The
// NoteheadSet arm recovers which case applies via
// VoiceContent::position_of_event/find_event_index_at, the same
// top-level-event lookup selection_after_convert_to_rest itself uses
// (src/notation/notation_note_entry.cpp's top_level_event_id_at) to recover
// the owning Chord's id from a ChordNote selection. Exactly one of
// `notehead_item`/`chord_item` is expected to be set, matching
// convert_selection_to_rest's own resolution above; returns std::nullopt
// when neither resolves to anything invalidatable.
std::optional<graphscore::NotationInvalidation>
SelectionToolHandler::convert_to_rest_invalidation(
    const std::optional<graphscore::NoteheadItem>& notehead_item,
    const std::optional<graphscore::ChordItem>&    chord_item) const {
  if (chord_item.has_value()) {
    const graphscore::ChordItem&    item = *chord_item;
    const graphscore::VoiceContent* voice =
        resolve_voice(item.node, item.track, item.stave, item.voice);
    if (voice == nullptr) {
      return std::nullopt;
    }
    const graphscore::Chord* chord = nullptr;
    for (const auto& event : voice->events()) {
      const auto* candidate = std::get_if<graphscore::Chord>(&event);
      if (candidate != nullptr && candidate->id == item.entity) {
        chord = candidate;
        break;
      }
    }
    if (chord == nullptr) {
      return std::nullopt;
    }
    return chord_notes_invalidation(*chord, item.node, item.track, item.stave,
                                    item.voice);
  }

  if (!notehead_item.has_value()) {
    return std::nullopt;
  }
  const graphscore::NoteheadItem& item = *notehead_item;
  const graphscore::VoiceContent* voice =
      resolve_voice(item.node, item.track, item.stave, item.voice);
  if (voice == nullptr) {
    return std::nullopt;
  }
  const std::optional<graphscore::Rational> position =
      voice->position_of_event(item.entity);
  if (!position.has_value()) {
    return std::nullopt;
  }
  const std::optional<std::size_t> index =
      voice->find_event_index_at(*position);
  if (!index.has_value() || *index >= voice->events().size()) {
    return std::nullopt;
  }
  if (const auto* chord =
          std::get_if<graphscore::Chord>(&voice->events()[*index]);
      chord != nullptr) {
    return chord_notes_invalidation(*chord, item.node, item.track, item.stave,
                                    item.voice);
  }
  return notehead_invalidation(item);
}

}  // namespace graphscore::writer_app
