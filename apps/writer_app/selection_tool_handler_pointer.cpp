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

void SelectionToolHandler::on_pointer_press(graphscore::PointerEvent event) {
  if (event.button != graphscore::PointerButton::kPrimary) {
    return;
  }
  const graphscore::NotationPoint point{event.x, event.y};
  if (!drag_.begin(active_tool_, point)) {
    // begin() cancels any prior drag and returns false; committed_selection_
    // persists.  Show whatever highlight fits (committed, if any, or clear).
    update_highlight();
    return;
  }
  initiating_button_ = event.button;
  moved_during_drag_ = false;
  // Press alone may preserve the old committed highlight; update_highlight
  // shows the committed extent because begin() clears live_extent.
  update_highlight();
}

void SelectionToolHandler::on_pointer_move(graphscore::PointerEvent event) {
  if (!drag_.is_dragging()) {
    return;
  }
  const graphscore::NotationPoint point{event.x, event.y};
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return;
  }
  moved_during_drag_ = true;
  std::ignore        = drag_.update(project_, layout_, point);
  update_highlight();
}

void SelectionToolHandler::on_pointer_release(graphscore::PointerEvent event) {
  if (!drag_.is_dragging()) {
    return;
  }
  // Only the initiating button can commit the drag. Any other button
  // release — secondary button, middle button, or unknown — is silently
  // ignored while the drag continues.
  if (event.button != initiating_button_) {
    return;
  }
  const graphscore::NotationPoint point{event.x, event.y};
  // A press followed by a release with no intervening move is a click, not
  // a range drag: resolve the single notehead/chord/rest/marking/caret the
  // point names (M5-phase-16's resolve_selection_at), so an unmodified
  // Up/Down then has a committed single-notehead selection to act on.
  // Range-drag behavior is unchanged: any movement keeps the drag path.
  if (!moved_during_drag_) {
    drag_.cancel();
    if (std::isfinite(point.x) && std::isfinite(point.y)) {
      resolve_single_click_selection(point);
    }
    update_highlight();
    return;
  }
  // Resolve the final release point before committing, so the committed
  // extent matches the pointer position at release, not the last motion
  // position.
  if (std::isfinite(point.x) && std::isfinite(point.y)) {
    // update() resolves the range one more time at the release point.
    // If it returns nullopt (off-stave, invalid), the drag is cancelled
    // rather than committing a stale extent from the last valid move.
    if (drag_.update(project_, layout_, point).has_value()) {
      std::ignore = drag_.commit();
      // A pointer-drag commit is the one place first_staff_/last_staff_
      // are (re)derived from the committed selection's own items, since
      // there is no explicit caller-supplied staff scope to track for a
      // drag the way there is for extend_range_staff_scope() below.
      sync_staff_endpoints_from_committed();
      update_highlight();
      return;
    }
  }
  // The final point is invalid or resolution failed; cancel the drag
  // so no stale extent is committed.
  drag_.cancel();
  // Committed highlight (if any) remains — update_highlight falls through
  // to it since live_extent was cleared above.
  update_highlight();
}

void SelectionToolHandler::on_cancel() {
  if (drag_.is_dragging()) {
    drag_.cancel();
  }
  // Restore the committed selection highlight if one exists.  The
  // in-progress drag is cancelled above, but the previously committed
  // extent survives and should remain visible.
  update_highlight();
}

void SelectionToolHandler::update_highlight() {
  if (shell_ == nullptr) {
    return;
  }
  if (const auto& extent = drag_.live_extent(); extent.has_value()) {
    shell_->set_highlight_rects(
        build_range_highlight_rects(*extent, project_, layout_));
  } else if (const auto& committed = drag_.committed_selection();
             committed.has_value()) {
    shell_->set_highlight_rects(
        build_range_highlight_rects(*committed, project_, layout_));
  } else {
    shell_->set_highlight_rects({});
  }
}

// Resolves a non-drag click to the single notehead/chord/rest/marking/caret
// selection the point names (M5-phase-16's resolve_selection_at) and stores
// it as the committed selection. A point that names nothing (off-stave, or a
// stale layout) leaves the committed selection unchanged, matching the
// pre-M5-phase-20 click behavior.
void SelectionToolHandler::resolve_single_click_selection(
    const graphscore::NotationPoint point) {
  std::optional<graphscore::Selection> selection =
      graphscore::resolve_selection_at(project_, layout_, palette_, point);
  if (selection.has_value()) {
    drag_.set_committed_selection(std::move(selection));
  }
}

}  // namespace graphscore::writer_app
