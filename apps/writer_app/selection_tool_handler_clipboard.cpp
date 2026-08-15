// SPDX-License-Identifier: Apache-2.0

#include "selection_tool_handler.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace graphscore::writer_app {

// Whether (node, track, stave) still names a live, active stave the node
// engraves. Shared by the paste-anchor derivation and the step-entry
// cursor initialization so a stale (archived track / absent stave / lane)
// selection is rejected everywhere it feeds a measure or caret lookup.
bool SelectionToolHandler::live_stave(graphscore::NodeId  node,
                                      graphscore::TrackId track,
                                      graphscore::StaveId stave) const {
  const graphscore::Node* node_ptr = project_.find_node(node);
  if (node_ptr == nullptr) {
    return false;
  }
  if (project_.find_active_track(track) == nullptr) {
    return false;
  }
  const graphscore::TrackLane* lane = node_ptr->lane(track);
  if (lane == nullptr) {
    return false;
  }
  return lane->stave(stave) != nullptr;
}

std::optional<graphscore::Rational> SelectionToolHandler::measure_start_at(
    graphscore::NodeId node, graphscore::TrackId track,
    graphscore::StaveId stave, std::size_t measure_index) const {
  const graphscore::Node* node_ptr = project_.find_node(node);
  if (node_ptr == nullptr || node_ptr->timeline() == nullptr) {
    return std::nullopt;
  }
  if (project_.find_active_track(track) == nullptr) {
    return std::nullopt;
  }
  const graphscore::TrackLane* lane = node_ptr->lane(track);
  if (lane == nullptr || lane->stave(stave) == nullptr) {
    return std::nullopt;
  }
  const graphscore::MeasureMap& measures = node_ptr->timeline()->measures();
  if (measure_index >= measures.measure_count()) {
    return std::nullopt;
  }
  return measures.measure_start(measure_index);
}

std::optional<graphscore::NotationInvalidation>
SelectionToolHandler::full_invalidation() const {
  const graphscore::Node* node = project_.find_node(layout_.node_id);
  if (node == nullptr || node->timeline() == nullptr) {
    return std::nullopt;
  }
  const std::size_t count = node->timeline()->measures().measure_count();
  if (count == 0) {
    return std::nullopt;
  }
  return graphscore::NotationInvalidation{
      graphscore::NotationInvalidationKind::kLocalContent, 0, count - 1};
}

// §10.2: paste-anchor derivation from the committed Selection, first match
// wins. The single-slot clipboard is not consulted here; rejections are a
// no-op with a diagnostic and the clipboard untouched. Every arm is
// rejected when its (node, track, stave) no longer names a live, active
// stave, and the multi-item arms additionally require a single node and a
// single span / measure index (the same preconditions extract_fragment
// enforces), so a stale or ambiguous selection can never produce an anchor.
std::optional<graphscore::PasteAnchor>
SelectionToolHandler::derive_paste_anchor() const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return std::nullopt;
  }

  if (const auto* set =
          std::get_if<graphscore::InsertionCaretSet>(&*committed)) {
    if (set->items().size() != 1u) {
      return std::nullopt;
    }
    const auto& item = set->items().front();
    if (!live_stave(item.node, item.track, item.stave)) {
      return std::nullopt;
    }
    return graphscore::PasteAnchor{item.node, item.track, item.stave,
                                   item.position};
  }

  const auto event_anchor =
      [&](graphscore::NodeId node, graphscore::TrackId track,
          graphscore::StaveId stave, graphscore::Voice voice,
          graphscore::NotationEntityId entity)
      -> std::optional<graphscore::PasteAnchor> {
    if (!live_stave(node, track, stave)) {
      return std::nullopt;
    }
    const auto onset = event_onset(node, track, stave, voice, entity);
    if (!onset.has_value()) {
      return std::nullopt;
    }
    return graphscore::PasteAnchor{node, track, stave, *onset};
  };

  if (const auto* set = std::get_if<graphscore::NoteheadSet>(&*committed)) {
    if (set->items().size() != 1u) {
      return std::nullopt;
    }
    const auto& item = set->items().front();
    return event_anchor(item.node, item.track, item.stave, item.voice,
                        item.entity);
  }
  if (const auto* set = std::get_if<graphscore::ChordSet>(&*committed)) {
    if (set->items().size() != 1u) {
      return std::nullopt;
    }
    const auto& item = set->items().front();
    return event_anchor(item.node, item.track, item.stave, item.voice,
                        item.entity);
  }
  if (const auto* set = std::get_if<graphscore::RestSet>(&*committed)) {
    if (set->items().size() != 1u) {
      return std::nullopt;
    }
    const auto& item = set->items().front();
    return event_anchor(item.node, item.track, item.stave, item.voice,
                        item.entity);
  }
  if (const auto* set =
          std::get_if<graphscore::ArbitraryRangeSet>(&*committed)) {
    if (set->items().empty()) {
      return std::nullopt;
    }
    const auto& first = set->items().front();
    for (const auto& item : set->items()) {
      if (item.node != first.node) {
        return std::nullopt;  // spans more than one node.
      }
      if (item.span != first.span) {
        return std::nullopt;  // items must share one identical span.
      }
      if (!live_stave(item.node, item.track, item.stave)) {
        return std::nullopt;
      }
    }
    return graphscore::PasteAnchor{first.node, first.track, first.stave,
                                   first.span.start};
  }
  if (const auto* set = std::get_if<graphscore::FullMeasureSet>(&*committed)) {
    if (set->items().empty()) {
      return std::nullopt;
    }
    const auto& first = set->items().front();
    for (const auto& item : set->items()) {
      if (item.node != first.node) {
        return std::nullopt;
      }
      if (item.measure_index != first.measure_index) {
        return std::nullopt;  // items must share one measure index.
      }
      // Every item, not merely the first anchor source, must remain live and
      // within the common measure map. Otherwise a deterministic first item
      // would conceal a stale later destination scope.
      if (!measure_start_at(item.node, item.track, item.stave,
                            item.measure_index)
               .has_value()) {
        return std::nullopt;
      }
    }
    const auto start = measure_start_at(first.node, first.track, first.stave,
                                        first.measure_index);
    if (!start.has_value()) {
      return std::nullopt;
    }
    return graphscore::PasteAnchor{first.node, first.track, first.stave,
                                   *start};
  }
  // MarkingSet / NodeSet / ConnectorSet name no musical-time position to
  // anchor a paste.
  return std::nullopt;
}

bool SelectionToolHandler::paste_available() const {
  if (!clipboard_.has_value() || history_.poisoned()) {
    return false;
  }
  const auto anchor = derive_paste_anchor();
  if (!anchor.has_value()) {
    return false;
  }
  // Execute against a value copy: this is side-effect-free for app state and
  // exercises the exact PasteFragmentCommand placement, mapping, range,
  // tuplet, and meter checks rather than maintaining a weaker approximation.
  graphscore::Project              candidate = project_;
  graphscore::PasteFragmentCommand probe(*clipboard_, *anchor);
  return probe.execute(candidate).ok();
}

bool SelectionToolHandler::copy_selection() {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    post_diagnostic("copy: no selection");
    return false;
  }
  const bool is_measure =
      std::holds_alternative<graphscore::FullMeasureSet>(*committed);
  const bool is_range =
      std::holds_alternative<graphscore::ArbitraryRangeSet>(*committed);
  if (!is_measure && !is_range) {
    post_diagnostic("copy: requires a full-measure or range selection");
    return false;
  }
  const graphscore::FragmentExtraction extraction =
      graphscore::extract_fragment(project_, *committed);
  if (!extraction.status.ok() || !extraction.fragment.has_value()) {
    // The clipboard is preserved exactly as it was on failure.
    post_diagnostic("copy: extraction failed");
    return false;
  }
  clipboard_ = std::move(*extraction.fragment);
  return true;
}

bool SelectionToolHandler::cut_selection() {
  if (history_.poisoned()) {
    post_diagnostic("cut: history unavailable");
    return false;
  }
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    post_diagnostic("cut: no selection");
    return false;
  }
  const bool is_measure =
      std::holds_alternative<graphscore::FullMeasureSet>(*committed);
  const bool is_range =
      std::holds_alternative<graphscore::ArbitraryRangeSet>(*committed);
  if (!is_measure && !is_range) {
    post_diagnostic("cut: requires a full-measure or range selection");
    return false;
  }

  // The one mandated post-cut selection (§10.1): a single-item
  // InsertionCaretSet at the cut start, derived from the cut selection's
  // first item in stored order.
  std::optional<graphscore::Selection> caret;
  if (is_range) {
    const auto& set   = std::get<graphscore::ArbitraryRangeSet>(*committed);
    const auto& first = set.items().front();
    const auto  built = graphscore::InsertionCaretSet::create(
        {{first.node, first.track, first.stave, first.voice,
           first.span.start}});
    if (!built.has_value()) {
      post_diagnostic("cut: cannot form caret");
      return false;
    }
    caret = graphscore::Selection{*built};
  } else {
    const auto& set   = std::get<graphscore::FullMeasureSet>(*committed);
    const auto& first = set.items().front();
    // A bounded, validated measure lookup: a stale full-measure selection
    // (out-of-range measure index, archived track, or absent stave) is
    // rejected before anything is mutated, preserving the project,
    // clipboard, selection, and history (§10.1's atomic cut).
    const auto start = measure_start_at(first.node, first.track, first.stave,
                                        first.measure_index);
    if (!start.has_value()) {
      post_diagnostic("cut: stale full-measure selection");
      return false;
    }
    const auto built = graphscore::InsertionCaretSet::create(
        {{first.node, first.track, first.stave, palette_.voice(), *start}});
    if (!built.has_value()) {
      post_diagnostic("cut: cannot form caret");
      return false;
    }
    caret = graphscore::Selection{*built};
  }

  auto command = std::make_unique<graphscore::CutFragmentCommand>(*committed);
  graphscore::CutFragmentCommand*         cut_command = command.get();
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    // Extraction or a straddling-tuplet rejection failed; project, clipboard,
    // and selection are all unchanged.
    post_diagnostic("cut: extraction failed");
    return false;
  }

  const std::optional<graphscore::NotationFragment>& fragment =
      cut_command->fragment();
  if (!fragment.has_value()) {
    std::ignore = transaction.abort();
    post_diagnostic("cut: no fragment produced");
    return false;
  }

  const auto invalidation = full_invalidation();
  if (!invalidation.has_value()) {
    std::ignore = transaction.abort();
    post_diagnostic("cut: cannot invalidate layout");
    return false;
  }
  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      recover_from_failed_rollback();
      return false;
    }
    layout_cache_.reset();
    warm_layout_cache();
    post_diagnostic("cut: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok()) {
    post_diagnostic("cut: commit failed");
    return false;
  }

  clipboard_ = *fragment;
  set_committed_selection(std::move(caret));
  return true;
}

bool SelectionToolHandler::paste_clipboard() {
  if (history_.poisoned()) {
    post_diagnostic("paste: history unavailable");
    return false;
  }
  if (!clipboard_.has_value()) {
    post_diagnostic("paste: clipboard is empty");
    return false;
  }
  const auto anchor = derive_paste_anchor();
  if (!anchor.has_value()) {
    post_diagnostic("paste: no paste anchor");
    return false;
  }

  auto command =
      std::make_unique<graphscore::PasteFragmentCommand>(*clipboard_, *anchor);
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    post_diagnostic("paste: rejected (out of range or meter mismatch)");
    return false;
  }

  const auto invalidation = full_invalidation();
  if (!invalidation.has_value()) {
    std::ignore = transaction.abort();
    post_diagnostic("paste: cannot invalidate layout");
    return false;
  }
  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      recover_from_failed_rollback();
      return false;
    }
    layout_cache_.reset();
    warm_layout_cache();
    post_diagnostic("paste: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok()) {
    post_diagnostic("paste: commit failed");
    return false;
  }
  // Paste does not consume the fragment: the clipboard stays intact.
  return true;
}

bool SelectionToolHandler::clipboard_has_fragment() const noexcept {
  return clipboard_.has_value();
}

const std::optional<graphscore::NotationFragment>&
SelectionToolHandler::clipboard() const noexcept {
  return clipboard_;
}

}  // namespace graphscore::writer_app
