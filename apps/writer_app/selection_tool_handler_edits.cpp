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

// Moves the single selected notehead one diatonic staff step (M5-phase-20).
// Requires the committed selection to be a NoteheadSet with exactly one
// item; any other selection -- none, a range, a non-notehead arm, or a
// multi-notehead set -- is a no-op returning false. The move runs as one
// reversible command through the handler's CommandHistory. On success the
// same notehead identity stays selected (MoveNoteheadCommand preserves ids),
// the retained layout is refreshed incrementally and re-published to the
// shell, and the short audition request for the post-edit pitch is recorded
// (M5-phase-15; nothing plays it until Milestone 08).
//
// The move is one provisional CommandHistory transaction: the pitch
// mutation executes without clearing the redo stack, the candidate layout
// is refreshed and its surface published, and only then does the
// transaction commit (clearing redo). If the cache refresh or the surface
// publish fails, the transaction aborts — undoing the pitch mutation and
// restoring the exact prior history, including any pre-existing redo —
// and the layout cache is re-seeded from the restored project, so project,
// layout, surface, selection/highlight, history, and audition all remain
// exactly as they were before the move. A stale notehead identity or a
// pitch step that would leave the SpelledPitch/MIDI range fails the same
// way, leaving everything unchanged.
//
// Abort reliability: MoveNoteheadCommand::undo() restores from its pre-edit
// snapshot, so the rollback of a just-applied pitch mutation cannot fail
// for any ordinary recoverable publication failure; only a process-level
// allocation failure inside undo itself can. If it does, the move hands
// the poisoned history to recover_from_failed_rollback() below: the layout
// cache is NOT re-seeded from a project that disagrees with the visible
// layout/surface, and a further move is blocked until recovery succeeds.
bool SelectionToolHandler::move_selected_notehead(
    graphscore::NoteheadStepDirection direction) {
  if (history_.poisoned()) {
    // A prior rollback failed and has not been recovered: the authoritative
    // project may disagree with the visible layout/surface. Refuse the
    // mutation rather than pretend the history is consistent.
    return false;
  }
  const auto* set = current_notehead_set();
  if (set == nullptr || set->items().size() != 1u) {
    return false;
  }
  const graphscore::NoteheadItem&                      item = set->items()[0];
  const std::optional<graphscore::NoteAuditionRequest> audition =
      graphscore::audition_for_notehead_move(project_, item, direction);
  std::unique_ptr<graphscore::Command> command =
      move_command_factory_(project_, item, direction);
  if (command == nullptr) {
    return false;
  }

  // Provisional execute: apply the pitch mutation WITHOUT clearing the
  // redo stack, so a failed publication can restore the exact prior
  // history (including any pre-existing redo) rather than destroying it.
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    return false;
  }

  if (!refresh_layout()) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      // The rollback failed: the history is now poisoned (the command and
      // its exact project are retained for recover()). Attempt recovery
      // once; a one-shot failure recovers here, while a persistent failure
      // leaves the handler unavailable and further moves blocked.
      recover_from_failed_rollback();
      return false;
    }
    // Project restored exactly: re-seed the retained cache from it so the
    // next move stays incremental.
    layout_cache_.reset();
    warm_layout_cache();
    return false;
  }

  // Publication succeeded: commit the command (clears redo, exactly
  // execute_new's normal success path). Capacity was reserved before
  // execute, so this cannot fail here.
  if (!transaction.commit().ok()) {
    return false;
  }
  last_audition_ = audition;
  return true;
}

// Steps the single selected notehead's accidental one rung along the
// double-flat .. double-sharp ladder (M5-phase-21). Requires the committed
// selection to be a NoteheadSet with exactly one item; any other selection
// -- none, a range, a non-notehead arm, or a multi-notehead set -- is a
// no-op returning false. The step runs as one reversible command through
// the handler's CommandHistory, exactly as move_selected_notehead above
// does: same provisional-transaction, same rollback and poisoned-history
// handling, same incremental layout refresh and surface publication, and
// the same short audition request for the post-edit pitch (M5-phase-15; an
// accidental step changes the sounding pitch, so it auditions).
//
// Either end of the ladder is a hard reject, never a clamp: `-` on a
// double-flat and `=` on a double-sharp build no command, execute nothing,
// push no undo entry, and issue no audition. The same is true when the
// resulting spelling would leave the sounding MIDI range.
bool SelectionToolHandler::step_selected_accidental(
    graphscore::AccidentalStepDirection direction) {
  if (history_.poisoned()) {
    // A prior rollback failed and has not been recovered: the authoritative
    // project may disagree with the visible layout/surface. Refuse the
    // mutation rather than pretend the history is consistent.
    return false;
  }
  const auto* set = current_notehead_set();
  if (set == nullptr || set->items().size() != 1u) {
    return false;
  }
  const graphscore::NoteheadItem&                      item = set->items()[0];
  const std::optional<graphscore::NoteAuditionRequest> audition =
      graphscore::audition_for_accidental_step(project_, item, direction);
  std::unique_ptr<graphscore::Command> command =
      accidental_command_factory_(project_, item, direction);
  if (command == nullptr) {
    return false;
  }

  // Provisional execute: apply the spelling mutation WITHOUT clearing the
  // redo stack, so a failed publication can restore the exact prior
  // history (including any pre-existing redo) rather than destroying it.
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    return false;
  }

  if (!refresh_layout()) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      // The rollback failed: the history is now poisoned (the command and
      // its exact project are retained for recover()). Attempt recovery
      // once; a one-shot failure recovers here, while a persistent failure
      // leaves the handler unavailable and further steps blocked.
      recover_from_failed_rollback();
      return false;
    }
    // Project restored exactly: re-seed the retained cache from it so the
    // next step stays incremental.
    layout_cache_.reset();
    warm_layout_cache();
    return false;
  }

  // Publication succeeded: commit the command (clears redo, exactly
  // execute_new's normal success path). Capacity was reserved before
  // execute, so this cannot fail here.
  if (!transaction.commit().ok()) {
    return false;
  }
  last_audition_ = audition;
  return true;
}

// Adds one key-spelled diatonic interval notehead above/below the single
// selected notehead (M5-phase-25): unmodified `2`..`8` add above, Shift+
// `2`..`8` add below. Requires the committed selection to be a NoteheadSet
// with exactly one item naming a top-level Note or a ChordNote; any other
// selection -- none, a range, a non-notehead arm, a multi-notehead set, a
// GraceNote -- is a no-op returning false. The interval runs as one
// reversible command through the handler's CommandHistory, exactly as
// move_selected_notehead/step_selected_accidental do: same provisional
// transaction, same rollback and poisoned-history handling, same
// incremental layout refresh and surface publication. On success only the
// newly inserted notehead identity becomes the committed selection, and
// the short audition request for the resulting chord is recorded
// (M5-phase-15; nothing plays it until Milestone 08).
//
// Rejections (an impossible octave/MIDI result, a duplicate spelled pitch,
// a stale identity) build no committed change: they fail inside execute and
// return false, leaving the project, selection, history, layout, surface,
// and audition all unchanged.
bool SelectionToolHandler::add_selected_interval(
    graphscore::IntervalDirection direction, std::uint8_t interval) {
  if (history_.poisoned()) {
    // A prior rollback failed and has not been recovered: the authoritative
    // project may disagree with the visible layout/surface. Refuse the
    // mutation rather than pretend the history is consistent.
    return false;
  }
  const auto* set = current_notehead_set();
  if (set == nullptr || set->items().size() != 1u) {
    return false;
  }
  const graphscore::NoteheadItem&                      item = set->items()[0];
  const std::optional<graphscore::NoteAuditionRequest> audition =
      graphscore::audition_for_add_interval(project_, item, interval,
                                            direction);
  std::unique_ptr<graphscore::Command> command =
      graphscore::make_add_interval_command(project_, item, interval,
                                            direction);
  if (command == nullptr) {
    return false;
  }
  // make_add_interval_command always returns an AddIntervalCommand (or
  // nullptr), so the concrete type is known; the fresh inserted notehead id
  // is read before the command is moved into the transaction.
  const auto* add_command =
      static_cast<const graphscore::AddIntervalCommand*>(command.get());

  const std::optional<graphscore::NotationInvalidation> invalidation =
      interval_invalidation(item);
  if (!invalidation.has_value()) {
    return false;
  }

  // Provisional execute: apply the mutation WITHOUT clearing the redo
  // stack, so a failed publication can restore the exact prior history
  // (including any pre-existing redo) rather than destroying it.
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    return false;
  }

  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      // The rollback failed: the history is now poisoned. Attempt recovery
      // once; a persistent failure leaves the handler unavailable and
      // further edits blocked, exactly as the other domain-mutation paths
      // do.
      recover_from_failed_rollback();
      return false;
    }
    // Project restored exactly: re-seed the retained cache from it so the
    // next edit stays incremental.
    layout_cache_.reset();
    warm_layout_cache();
    return false;
  }

  // Publication succeeded: commit the command (clears redo). Capacity was
  // reserved before execute, so this cannot fail here.
  if (!transaction.commit().ok()) {
    return false;
  }
  const std::optional<graphscore::NoteheadSet> next =
      graphscore::NoteheadSet::create({graphscore::NoteheadItem{
          item.node, item.track, item.stave, item.voice,
          add_command->inserted_notehead_id()}});
  if (!next.has_value()) {
    return false;
  }
  set_committed_selection(graphscore::Selection{*next});
  last_audition_ = audition;
  return true;
}

bool SelectionToolHandler::delete_selected_notehead() {
  if (history_.poisoned())
    return false;
  const auto* set = current_notehead_set();
  if (set == nullptr || set->items().size() != 1u)
    return false;
  const graphscore::NoteheadItem       item = set->items().front();
  std::optional<graphscore::Selection> next_selection =
      graphscore::selection_after_notehead_delete(project_, item);
  std::unique_ptr<graphscore::Command> command =
      graphscore::make_delete_notehead_command(project_, item);
  if (command == nullptr || !next_selection.has_value())
    return false;

  const std::optional<graphscore::NotationInvalidation> invalidation =
      notehead_invalidation(item);
  if (!invalidation.has_value())
    return false;
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active())
    return false;
  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      // The rollback failed: the history is now poisoned. Attempt recovery
      // once; a persistent failure leaves the handler unavailable and
      // further edits blocked, exactly as the notehead-move/accidental-step
      // paths do.
      recover_from_failed_rollback();
      return false;
    }
    // Project restored exactly: re-seed the retained cache from it so the
    // next edit stays incremental.
    layout_cache_.reset();
    warm_layout_cache();
    return false;
  }
  if (!transaction.commit().ok())
    return false;
  set_committed_selection(std::move(next_selection));
  return true;
}

// "R" (M5-phase-23): converts the entire selected note/chord event to an
// equal-duration rest and selects the resulting rest. Requires the
// committed selection to be a single-item NoteheadSet (a top-level Note or
// a ChordNote -- converting the WHOLE containing chord, unlike delete's
// per-pitch semantics) or a single-item ChordSet; every other selection is
// a no-op returning false, exactly the make_convert_event_to_rest_command
// no-op set. Produces no audition request: "R" produces silence.
bool SelectionToolHandler::convert_selection_to_rest() {
  if (history_.poisoned())
    return false;

  // Read the committed selection once, checked, so it can be reused
  // directly below instead of rebuilt through NoteheadSet::create/
  // ChordSet::create -- current_notehead_set()/current_chord_set() (used
  // elsewhere in this class) each perform this same has_value() check
  // internally, but a pointer returned from either does not carry that
  // proof to a later dereference of committed_selection() itself.
  const std::optional<graphscore::Selection>& committed =
      drag_.committed_selection();
  if (!committed.has_value())
    return false;

  std::optional<graphscore::NoteheadItem> notehead_item;
  std::optional<graphscore::ChordItem>    chord_item;
  if (const auto* set = std::get_if<graphscore::NoteheadSet>(&*committed);
      set != nullptr && set->items().size() == 1u) {
    notehead_item = set->items().front();
  } else if (const auto* chords =
                 std::get_if<graphscore::ChordSet>(&*committed);
             chords != nullptr && chords->items().size() == 1u) {
    chord_item = chords->items().front();
  } else {
    return false;
  }

  const graphscore::Selection& selection = *committed;

  std::optional<graphscore::Selection> next_selection =
      graphscore::selection_after_convert_to_rest(project_, selection);
  std::unique_ptr<graphscore::Command> command =
      graphscore::make_convert_event_to_rest_command(project_, selection);
  if (command == nullptr || !next_selection.has_value())
    return false;

  const std::optional<graphscore::NotationInvalidation> invalidation =
      convert_to_rest_invalidation(notehead_item, chord_item);
  if (!invalidation.has_value())
    return false;
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active())
    return false;
  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      // The rollback failed: the history is now poisoned. Attempt recovery
      // once; a persistent failure leaves the handler unavailable and
      // further edits blocked, exactly as the other domain-mutation paths
      // do.
      recover_from_failed_rollback();
      return false;
    }
    // Project restored exactly: re-seed the retained cache from it so the
    // next edit stays incremental.
    layout_cache_.reset();
    warm_layout_cache();
    return false;
  }
  if (!transaction.commit().ok())
    return false;
  set_committed_selection(std::move(next_selection));
  return true;
}

// Palette-only (M5-phase-28b, no key chord): inserts a new measure before
// the selected measure across every track, stave, and voice, remapping
// signatures, clefs, tempo anchors, spans, selection, and rests atomically
// -- the domain's own InsertMeasureCommand cascade
// (make_insert_measure_command's own comment). Requires the committed
// selection to be an ALIGNED FullMeasureSet (one NodeId, one
// measure_index); every other selection -- including a misaligned
// FullMeasureSet -- is a no-op with a diagnostic. A boundary the domain
// only detects at execute() (a tuplet group the insertion would cut, or a
// pickdown it would invalidate) still builds a valid command here; the
// rejection surfaces as an inactive transaction below, diagnosed rather
// than silently returning true.
bool SelectionToolHandler::insert_measure_before() {
  if (history_.poisoned()) {
    post_diagnostic("insert measure: history unavailable");
    return false;
  }
  const auto* set = current_measure_set();
  if (set == nullptr) {
    post_diagnostic("insert measure: requires a full-measure selection");
    return false;
  }
  const graphscore::Selection selection{*set};

  std::optional<graphscore::Selection> next_selection =
      graphscore::selection_after_insert_measure(
          project_, selection, graphscore::MeasureInsertMode::kBefore);
  std::unique_ptr<graphscore::Command> command =
      graphscore::make_insert_measure_command(
          project_, selection, graphscore::MeasureInsertMode::kBefore);
  if (command == nullptr || !next_selection.has_value()) {
    post_diagnostic("insert measure: ineligible or misaligned selection");
    return false;
  }

  const graphscore::NodeId node          = set->items().front().node;
  const std::size_t        measure_index = set->items().front().measure_index;
  const std::optional<graphscore::NotationInvalidation> invalidation =
      measure_structure_invalidation(node, measure_index,
                                     MeasureStructureEdit::kInsertBefore);
  if (!invalidation.has_value()) {
    post_diagnostic("insert measure: cannot invalidate layout");
    return false;
  }
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    post_diagnostic(
        "insert measure: rejected (tuplet boundary, invalid pickdown, or "
        "history is full)");
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
    post_diagnostic("insert measure: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok()) {
    post_diagnostic("insert measure: commit failed");
    return false;
  }
  set_committed_selection(std::move(next_selection));
  return true;
}

// Palette-only (M5-phase-28b): appends a new measure at the node's own end,
// inheriting the final measure's signature, regardless of which measure the
// committed selection itself names. Same eligible-selection requirement and
// atomic execute()-time rejection handling as insert_measure_before() above.
bool SelectionToolHandler::append_measure() {
  if (history_.poisoned()) {
    post_diagnostic("append measure: history unavailable");
    return false;
  }
  const auto* set = current_measure_set();
  if (set == nullptr) {
    post_diagnostic("append measure: requires a full-measure selection");
    return false;
  }
  const graphscore::Selection selection{*set};

  std::optional<graphscore::Selection> next_selection =
      graphscore::selection_after_insert_measure(
          project_, selection, graphscore::MeasureInsertMode::kAppend);
  std::unique_ptr<graphscore::Command> command =
      graphscore::make_insert_measure_command(
          project_, selection, graphscore::MeasureInsertMode::kAppend);
  if (command == nullptr || !next_selection.has_value()) {
    post_diagnostic("append measure: ineligible or misaligned selection");
    return false;
  }

  const graphscore::NodeId node          = set->items().front().node;
  const std::size_t        measure_index = set->items().front().measure_index;
  const std::optional<graphscore::NotationInvalidation> invalidation =
      measure_structure_invalidation(node, measure_index,
                                     MeasureStructureEdit::kAppend);
  if (!invalidation.has_value()) {
    post_diagnostic("append measure: cannot invalidate layout");
    return false;
  }
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    post_diagnostic(
        "append measure: rejected (tuplet boundary, invalid pickdown, or "
        "history is full)");
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
    post_diagnostic("append measure: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok()) {
    post_diagnostic("append measure: commit failed");
    return false;
  }
  set_committed_selection(std::move(next_selection));
  return true;
}

// Palette-only (M5-phase-28b): deletes the selected measure across every
// track, stave, and voice. Same eligible-selection requirement as
// insert_measure_before() above, plus the domain's own sole-measure guard
// (make_delete_measure_command pre-rejects a node whose timeline carries
// exactly one measure).
bool SelectionToolHandler::delete_measure() {
  if (history_.poisoned()) {
    post_diagnostic("delete measure: history unavailable");
    return false;
  }
  const auto* set = current_measure_set();
  if (set == nullptr) {
    post_diagnostic("delete measure: requires a full-measure selection");
    return false;
  }
  const graphscore::Selection selection{*set};

  std::optional<graphscore::Selection> next_selection =
      graphscore::selection_after_delete_measure(project_, selection);
  std::unique_ptr<graphscore::Command> command =
      graphscore::make_delete_measure_command(project_, selection);
  if (command == nullptr || !next_selection.has_value()) {
    post_diagnostic(
        "delete measure: ineligible, misaligned, or sole-measure selection");
    return false;
  }

  const graphscore::NodeId node          = set->items().front().node;
  const std::size_t        measure_index = set->items().front().measure_index;
  const std::optional<graphscore::NotationInvalidation> invalidation =
      measure_structure_invalidation(node, measure_index,
                                     MeasureStructureEdit::kDelete);
  if (!invalidation.has_value()) {
    post_diagnostic("delete measure: cannot invalidate layout");
    return false;
  }
  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    post_diagnostic(
        "delete measure: rejected (tuplet boundary, invalid pickdown, or "
        "history is full)");
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
    post_diagnostic("delete measure: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok()) {
    post_diagnostic("delete measure: commit failed");
    return false;
  }
  set_committed_selection(std::move(next_selection));
  return true;
}

bool SelectionToolHandler::tuplet_ratio_available(
    graphscore::TupletRatio ratio) const {
  const auto& selection = drag_.committed_selection();
  if (!selection.has_value())
    return false;
  return graphscore::make_tuplet_create_command(project_, *selection, ratio) !=
             nullptr ||
         graphscore::make_tuplet_change_command(project_, *selection, ratio) !=
             nullptr;
}

bool SelectionToolHandler::tuplet_remove_available() const {
  const auto& selection = drag_.committed_selection();
  return selection.has_value() && graphscore::make_tuplet_remove_command(
                                      project_, *selection) != nullptr;
}

bool SelectionToolHandler::create_triplet() {
  return apply_tuplet_ratio(3, 2);
}

bool SelectionToolHandler::apply_tuplet_ratio(std::uint16_t played,
                                              std::uint16_t normal) {
  const auto ratio = graphscore::TupletRatio::create(played, normal);
  if (!ratio.has_value() || played == normal) {
    post_diagnostic("tuplet: ratio must be non-zero and non-trivial N:M");
    return false;
  }
  if (history_.poisoned()) {
    post_diagnostic("tuplet: command history is unavailable");
    return false;
  }
  const auto& selection = drag_.committed_selection();
  if (!selection.has_value()) {
    post_diagnostic("tuplet: select an exact rhythmic range or tuplet marking");
    return false;
  }

  const bool creating =
      std::holds_alternative<graphscore::ArbitraryRangeSet>(*selection);
  std::optional<graphscore::Selection> next;
  std::unique_ptr<graphscore::Command> command;
  if (creating) {
    next = graphscore::selection_after_tuplet_create(project_, *selection);
    command =
        graphscore::make_tuplet_create_command(project_, *selection, *ratio);
  } else {
    command =
        graphscore::make_tuplet_change_command(project_, *selection, *ratio);
  }
  if (command == nullptr || (creating && !next.has_value())) {
    post_diagnostic(
        "tuplet: target must be complete contiguous events in one voice; "
        "nested tuplets are not allowed");
    return false;
  }
  const auto invalidation = full_invalidation();
  if (!invalidation.has_value()) {
    post_diagnostic("tuplet: cannot invalidate layout");
    return false;
  }
  auto transaction = history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    post_diagnostic("tuplet: ratio does not fit the fixed group bounds");
    return false;
  }
  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      recover_from_failed_rollback();
    } else {
      layout_cache_.reset();
      warm_layout_cache();
    }
    post_diagnostic("tuplet: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok()) {
    post_diagnostic("tuplet: commit failed");
    return false;
  }
  if (next.has_value())
    set_committed_selection(std::move(next));
  tuplet_ratio_entry_requested_ = false;
  return true;
}

bool SelectionToolHandler::remove_selected_tuplet() {
  if (history_.poisoned()) {
    post_diagnostic("remove tuplet: command history is unavailable");
    return false;
  }
  const auto&                          selection = drag_.committed_selection();
  std::unique_ptr<graphscore::Command> command =
      selection.has_value()
          ? graphscore::make_tuplet_remove_command(project_, *selection)
          : nullptr;
  if (command == nullptr) {
    post_diagnostic("remove tuplet: requires a selected tuplet marking");
    return false;
  }
  const auto invalidation = full_invalidation();
  if (!invalidation.has_value())
    return false;
  auto transaction = history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    post_diagnostic("remove tuplet: fixed group bounds cannot be normalized");
    return false;
  }
  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      recover_from_failed_rollback();
    } else {
      layout_cache_.reset();
      warm_layout_cache();
    }
    post_diagnostic("remove tuplet: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok())
    return false;
  set_committed_selection(std::nullopt);
  return true;
}

void SelectionToolHandler::request_tuplet_ratio_entry() {
  tuplet_ratio_entry_requested_ = true;
  post_diagnostic("tuplet ratio: enter played N and normal M, then apply N:M");
}

bool SelectionToolHandler::tuplet_ratio_entry_requested() const noexcept {
  return tuplet_ratio_entry_requested_;
}

// Primary+Up/Down (M5-phase-24): moves the committed selection to the
// prior/next staff of the node, wrapping within it, and selects the
// same-voice note nearest the musical position -- then the visually
// nearest note on a tie, or an insertion caret. Every rule lives in
// graphscore::selection_after_staff_step; this method owns only the
// read/commit wiring.
//
// In kNoteEntry (M5-phase-27 §8.4) the staff step additionally moves the
// step-entry cursor to the same voice and position on the target staff and
// resets the pitch reference, because the staff changed.
//
// Unlike every other keyboard action on this handler, this is a PURE
// SELECTION change: it builds no Command, opens no CommandHistory
// transaction, mutates no project state, and neither invalidates nor
// refreshes the retained layout -- so it needs none of their rollback
// machinery and is not blocked by a poisoned history. A rejected step
// (no committed selection, an ineligible or multi-item selection arm, a
// single-staff node) returns false and leaves the selection untouched.
bool SelectionToolHandler::step_selected_staff(
    graphscore::StaffStepDirection direction) {
  if (active_tool_ == graphscore::ActiveTool::kNoteEntry) {
    // In kNoteEntry the cursor is the primary state (§8.4): it moves to the
    // same voice and position on the target staff and the pitch reference
    // resets, independent of whether the committed selection is an eligible
    // arm. The committed selection is then re-derived on the target staff by
    // the same resolution the kSelection path uses, when it is eligible.
    const bool cursor_moved = step_cursor_staff(direction);
    const std::optional<graphscore::Selection>& committed =
        drag_.committed_selection();
    if (committed.has_value()) {
      std::optional<graphscore::Selection> stepped =
          graphscore::selection_after_staff_step(project_, layout_, *committed,
                                                 direction);
      if (stepped.has_value()) {
        set_committed_selection(std::move(stepped));
      }
    }
    return cursor_moved;
  }

  // Bound once and guarded once: a pointer handed back from one of the
  // current_*_set() accessors would not carry its own has_value() proof
  // to a later dereference of committed_selection() itself.
  const std::optional<graphscore::Selection>& committed =
      drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  std::optional<graphscore::Selection> stepped =
      graphscore::selection_after_staff_step(project_, layout_, *committed,
                                             direction);
  if (!stepped.has_value()) {
    return false;
  }
  set_committed_selection(std::move(stepped));
  return true;
}

}  // namespace graphscore::writer_app
