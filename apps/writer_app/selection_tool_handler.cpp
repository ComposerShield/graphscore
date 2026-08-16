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

SelectionToolHandler::SelectionToolHandler(graphscore::Project        project,
                                           graphscore::NotationLayout layout,
                                           graphscore::WriterShell*   shell)
    : project_(std::move(project)), layout_(std::move(layout)), shell_(shell) {}

SelectionToolHandler::~SelectionToolHandler() {
  if (drag_.is_dragging()) {
    drag_.cancel();
  }
  if (shell_ != nullptr) {
    // The palette owns the only current text-input focus. Disable production
    // composition before this InputHandler can become stale.
    shell_->set_text_input_active(false);
    shell_->set_highlight_rects({});
  }
}

void SelectionToolHandler::set_active_tool(graphscore::ActiveTool tool) {
  if (tool != active_tool_) {
    if (drag_.is_dragging()) {
      drag_.cancel();
    }
    // A pending note-entry press does not survive a tool switch.
    note_entry_press_pending_ = false;
    active_tool_              = tool;
    // M5-phase-27: the step-entry cursor and pitch reference exist only while
    // kNoteEntry is active (§8.1, §8.4). Entering initializes the cursor from
    // the committed Selection; exiting discards both.
    if (tool == graphscore::ActiveTool::kNoteEntry) {
      enter_step_entry();
    } else {
      exit_step_entry();
    }
    // After tool switch, show whatever highlight fits (committed, if any,
    // or clear entirely if none).
    update_highlight();
  }
}

graphscore::ActiveTool SelectionToolHandler::active_tool() const noexcept {
  return active_tool_;
}

// Supplies the glyph metrics used to refresh the retained layout after a
// notehead move (M5-phase-20). Must be set before any notehead move that
// expects the layout to refresh; the pointer/range-selection paths never
// need it. `run()` sets the production font; the notehead-move test sets
// SelfTestMetrics.
void SelectionToolHandler::set_metrics(
    const graphscore::GlyphMetrics* metrics) noexcept {
  metrics_ = metrics;
}

void SelectionToolHandler::set_surface_publisher(SurfacePublisher publisher) {
  publish_surface_ = std::move(publisher);
}

// Supplies the layout options the incremental layout cache uses. Must match
// the options the retained layout was produced with. `run()` keeps the
// default options; the cross-measure-tie test sets narrow one-measure-per-
// system options so a tie chain spanning two measures also spans two
// systems, which is what makes the full-chain invalidation observable.
void SelectionToolHandler::set_layout_options(
    graphscore::NotationLayoutOptions options) {
  layout_options_ = options;
}

void SelectionToolHandler::set_move_command_factory(
    MoveCommandFactory factory) {
  move_command_factory_ = std::move(factory);
}

void SelectionToolHandler::set_accidental_command_factory(
    AccidentalCommandFactory factory) {
  accidental_command_factory_ = std::move(factory);
}

void SelectionToolHandler::set_marking_edit_command_wrapper(
    MarkingEditCommandWrapper wrapper) {
  marking_edit_command_wrapper_ = std::move(wrapper);
}

// Builds the retained incremental layout cache from the current project and
// layout, so a later refresh_layout() reuses unaffected systems instead of
// full-resetting on its first call (which rebuilds everything and hides a
// stale invalidation scope). run() calls this at startup -- immediately
// after set_metrics() -- so the first production edit is already
// incremental; the notehead-move tests call it to reproduce that startup
// seeding before asserting rebuild scope.
void SelectionToolHandler::warm_layout_cache() {
  if (metrics_ == nullptr) {
    return;
  }
  std::ignore = layout_cache_.update(project_, layout_.node_id, *metrics_,
                                     layout_options_, {});
}

// Stores a selection directly, mirroring SelectionDragState's own
// keyboard/accessible entry point (M5-phase-19b). A pointer click reaches
// this through resolve_selection_at (resolve_single_click_selection);
// Shift/keyboard range extension and the accessible controls reach it
// through the range methods above. Kept public so those paths and the
// no-op tests below share one entry point.
void SelectionToolHandler::set_committed_selection(
    std::optional<graphscore::Selection> selection) {
  drag_.set_committed_selection(std::move(selection));
  update_highlight();
}

const graphscore::SelectionDragState& SelectionToolHandler::drag_state()
    const noexcept {
  return drag_;
}

const graphscore::Project& SelectionToolHandler::project() const noexcept {
  return project_;
}

const graphscore::NotationLayout& SelectionToolHandler::layout()
    const noexcept {
  return layout_;
}

// The audition request the most recent successful notehead move issued, or
// nullopt when no move has succeeded yet (or the last move had nothing to
// audition). A failed or no-op move leaves this unchanged.
const std::optional<graphscore::NoteAuditionRequest>&
SelectionToolHandler::last_audition() const noexcept {
  return last_audition_;
}

// The incremental-layout work the most recent refresh_layout() recorded:
// which measures/systems were rebuilt vs reused. Test-only; lets a test
// prove a cold-cache first local move rebuilds only the affected system
// (finding 1) rather than every system.
const graphscore::NotationLayoutWork&
SelectionToolHandler::test_last_layout_work() const noexcept {
  return last_layout_work_;
}

std::optional<graphscore::NotationInvalidation>
SelectionToolHandler::test_marking_edit_invalidation(
    bool include_following_event) const {
  return marking_edit_invalidation(include_following_event);
}

// The number of commands on the undo/redo stacks. Test-only; lets a
// failing-publisher test prove a rollback leaves history unchanged.
std::size_t SelectionToolHandler::test_undo_stack_size() const noexcept {
  return history_.undo_stack_size();
}

std::size_t SelectionToolHandler::test_redo_stack_size() const noexcept {
  return history_.redo_stack_size();
}

// True while a failed rollback has left the history poisoned and the
// handler unavailable: further moves (and any undo/redo through the same
// history) are blocked until the history recovers. Test-only; the
// rollback-failure tests assert this to prove the explicit unavailable
// state, rather than a silent half-rolled-back project.
bool SelectionToolHandler::history_unavailable() const noexcept {
  return history_.poisoned();
}

// Retries the rollback of a failed move's provisional command once the
// history has been poisoned by that failure. On success the authoritative
// project and history are restored; the visible layout/surface/highlight
// were never committed (refresh_layout() failed before committing layout_),
// so they are already coherent with the restored project, and the retained
// incremental cache is re-seeded from it. On a persistent failure the
// history stays poisoned and the handler remains unavailable. Either way
// the move did not complete.
void SelectionToolHandler::recover_from_failed_rollback() {
  const graphscore::Result recovered = history_.recover();
  if (!recovered.ok()) {
    return;
  }
  layout_cache_.reset();
  warm_layout_cache();
}

bool SelectionToolHandler::test_undo() {
  return history_.undo(project_).ok();
}

bool SelectionToolHandler::test_redo() {
  return history_.redo(project_).ok();
}

// Primary+Z (M5-phase-27): undo the most recent command. Selection-independent
// (Any tool). A no-op on an empty undo stack. The pitch reference is reset
// only when the undo actually removed the commit that produced the current
// previous_pitch_ (including an interval-inserted producer), tracked by the
// undo-stack depth recorded in record_step_entry_previous_pitch (§8.3):
// undoing a later accidental step or an unrelated command preserves the
// reference, while undoing the producer invalidates it.
bool SelectionToolHandler::undo_action() {
  if (history_.poisoned() || !history_.can_undo()) {
    return false;
  }
  if (!history_.undo(project_).ok()) {
    post_diagnostic("undo: command failed");
    return false;
  }
  const auto invalidation = full_invalidation();
  if (!invalidation.has_value() || !refresh_layout(invalidation)) {
    const graphscore::Result restored = history_.rollback_last_undo(project_);
    layout_cache_.reset();
    warm_layout_cache();
    post_diagnostic(restored.ok() ? "undo: layout refresh failed; undo restored"
                                  : "undo: layout refresh and restore failed");
    return false;
  }
  if (step_entry_cursor_.has_value() && previous_pitch_.has_value() &&
      history_.undo_stack_size() < previous_pitch_undo_depth_) {
    reset_pitch_reference();
  }
  return true;
}

// Shift+Primary+Z (M5-phase-27): redo the most recently undone command.
bool SelectionToolHandler::redo_action() {
  if (history_.poisoned() || !history_.can_redo()) {
    return false;
  }
  if (!history_.redo(project_).ok()) {
    post_diagnostic("redo: command failed");
    return false;
  }
  const auto invalidation = full_invalidation();
  if (!invalidation.has_value() || !refresh_layout(invalidation)) {
    const graphscore::Result restored = history_.rollback_last_redo(project_);
    layout_cache_.reset();
    warm_layout_cache();
    post_diagnostic(restored.ok() ? "redo: layout refresh failed; redo restored"
                                  : "redo: layout refresh and restore failed");
    return false;
  }
  return true;
}

void SelectionToolHandler::post_diagnostic(std::string message) {
  diagnostics_.push_back(std::move(message));
}

const std::vector<std::string>& SelectionToolHandler::diagnostics()
    const noexcept {
  return diagnostics_;
}

std::vector<std::string> SelectionToolHandler::take_diagnostics() {
  std::vector<std::string> taken;
  taken.swap(diagnostics_);
  return taken;
}

}  // namespace graphscore::writer_app
