// SPDX-License-Identifier: Apache-2.0

#include "selection_tool_handler.hpp"

#include "command_palette.hpp"
#include "key_bindings.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {

namespace {

[[nodiscard]] bool contains_ci(const std::string& haystack,
                               const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  const auto to_lower = [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  };
  std::string h;
  h.reserve(haystack.size());
  for (const char c : haystack) {
    h.push_back(to_lower(static_cast<unsigned char>(c)));
  }
  std::string n;
  n.reserve(needle.size());
  for (const char c : needle) {
    n.push_back(to_lower(static_cast<unsigned char>(c)));
  }
  return h.find(n) != std::string::npos;
}

}  // namespace

void SelectionToolHandler::toggle_command_palette() {
  if (palette_open_) {
    close_command_palette();
  } else {
    palette_open_ = true;
    if (shell_ != nullptr) {
      shell_->set_text_input_active(true);
    }
  }
}

void SelectionToolHandler::close_command_palette() {
  if (shell_ != nullptr) {
    shell_->set_text_input_active(false);
  }
  palette_open_ = false;
  palette_filter_.clear();
  palette_selected_index_ = 0;
}

bool SelectionToolHandler::command_palette_open() const noexcept {
  return palette_open_;
}

const std::string& SelectionToolHandler::command_palette_filter()
    const noexcept {
  return palette_filter_;
}

void SelectionToolHandler::command_palette_set_filter(std::string filter) {
  palette_filter_ = std::move(filter);
  // Filtering changes the candidate list; re-anchor the selection to the top.
  palette_selected_index_ = 0;
}

std::vector<PaletteCommand> SelectionToolHandler::command_palette_filtered()
    const {
  std::vector<PaletteCommand> rows;
  for (const PaletteCommand& command : palette_inventory()) {
    if (palette_filter_.empty() || contains_ci(command.name, palette_filter_) ||
        contains_ci(command.description, palette_filter_)) {
      rows.push_back(command);
    }
  }
  return rows;
}

void SelectionToolHandler::command_palette_move_selection(int delta) {
  const std::size_t size = palette_filtered_size();
  if (size == 0) {
    palette_selected_index_ = 0;
    return;
  }
  const std::size_t clamped = std::min(palette_selected_index_, size - 1);
  if (delta < 0) {
    palette_selected_index_ = clamped == 0 ? 0 : clamped - 1;
  } else {
    palette_selected_index_ = clamped >= size - 1 ? size - 1 : clamped + 1;
  }
}

std::size_t SelectionToolHandler::command_palette_selected_index()
    const noexcept {
  return palette_selected_index_;
}

std::size_t SelectionToolHandler::palette_filtered_size() const {
  return command_palette_filtered().size();
}

std::optional<PaletteCommand> SelectionToolHandler::palette_selected_command()
    const {
  const std::vector<PaletteCommand> filtered = command_palette_filtered();
  if (filtered.empty()) {
    return std::nullopt;
  }
  const std::size_t index =
      std::min(palette_selected_index_, filtered.size() - 1);
  return filtered[index];
}

bool SelectionToolHandler::command_palette_run_selected() {
  const std::optional<PaletteCommand> selected = palette_selected_command();
  if (!selected.has_value()) {
    return false;
  }
  return run_palette_command(selected->id);
}

// A private notehead-set availability probe shared by the availability
// switches below: a single-item NoteheadSet is the precondition of every
// notehead-editing action.

// ---- shared, side-effect-free eligibility probes (M5-phase-27 §11) -------

std::optional<graphscore::NoteheadItem>
SelectionToolHandler::single_notehead_item() const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return std::nullopt;
  }
  const auto* set = std::get_if<graphscore::NoteheadSet>(&*committed);
  if (set == nullptr || set->items().size() != 1u) {
    return std::nullopt;
  }
  return set->items().front();
}

std::optional<graphscore::ChordItem> SelectionToolHandler::single_chord_item()
    const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return std::nullopt;
  }
  const auto* set = std::get_if<graphscore::ChordSet>(&*committed);
  if (set == nullptr || set->items().size() != 1u) {
    return std::nullopt;
  }
  return set->items().front();
}

bool SelectionToolHandler::single_rest_item() const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  const auto* set = std::get_if<graphscore::RestSet>(&*committed);
  return set != nullptr && set->items().size() == 1u;
}

bool SelectionToolHandler::single_caret_item() const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  const auto* set = std::get_if<graphscore::InsertionCaretSet>(&*committed);
  return set != nullptr && set->items().size() == 1u;
}

bool SelectionToolHandler::convert_to_rest_available() const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  // The same no-op set make_convert_event_to_rest_command enforces: a
  // single-item NoteheadSet naming a Note/ChordNote (never a GraceNote) or a
  // single-item ChordSet, still resolving against the project.
  return graphscore::make_convert_event_to_rest_command(project_, *committed) !=
         nullptr;
}

bool SelectionToolHandler::staff_step_available() const {
  return staff_step_available(graphscore::StaffStepDirection::kNext);
}

bool SelectionToolHandler::staff_step_available(
    graphscore::StaffStepDirection direction) const {
  if (active_tool_ == graphscore::ActiveTool::kNoteEntry) {
    if (!step_entry_cursor_.has_value()) {
      return false;
    }
    const graphscore::Node* node = project_.find_node(step_entry_cursor_->node);
    if (node == nullptr ||
        !live_stave(step_entry_cursor_->node, step_entry_cursor_->track,
                    step_entry_cursor_->stave)) {
      return false;
    }
    std::vector<graphscore::MeasureScope> live;
    for (const auto& scope : graphscore::score_ordered_staves(project_)) {
      if (live_stave(step_entry_cursor_->node, scope.track_id,
                     scope.stave_id)) {
        live.push_back(scope);
      }
    }
    const graphscore::MeasureScope current{step_entry_cursor_->track,
                                           step_entry_cursor_->stave};
    return live.size() >= 2 &&
           std::find(live.begin(), live.end(), current) != live.end();
  }
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  // The exact eligible-source set selection_after_staff_step enforces
  // (single-item notehead/chord/rest/caret that still validates; a
  // single-staff node or a stale/ambiguous selection is a no-op). The probe
  // direction is immaterial: eligibility is direction-independent.
  return graphscore::selection_after_staff_step(project_, layout_, *committed,
                                                direction)
      .has_value();
}

bool SelectionToolHandler::copy_cut_available() const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  const bool is_measure =
      std::holds_alternative<graphscore::FullMeasureSet>(*committed);
  const bool is_range =
      std::holds_alternative<graphscore::ArbitraryRangeSet>(*committed);
  if (!is_measure && !is_range) {
    return false;
  }
  // The exact extraction copy/cut will perform: a pure, non-mutating query
  // whose failure (stale entities, archived track, multi-node, mixed span,
  // mixed measure index, straddling tuplet, ...) is the same set
  // extract_fragment rejects.
  return graphscore::extract_fragment(project_, *committed).status.ok();
}

// M5-phase-28b: exact make_insert_measure_command/make_delete_measure_command
// eligibility against the committed selection, so the palette can never offer
// a measure-structure row its own execution would reject. The sole-measure
// delete guard and the alignment/arm checks both live in the domain helper,
// so insert and delete eligibility naturally differ without any duplicated
// precondition logic here.
bool SelectionToolHandler::measure_insert_available(
    graphscore::MeasureInsertMode mode) const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  return graphscore::make_insert_measure_command(project_, *committed, mode) !=
         nullptr;
}

bool SelectionToolHandler::measure_delete_available() const {
  const auto& committed = drag_.committed_selection();
  if (!committed.has_value()) {
    return false;
  }
  return graphscore::make_delete_measure_command(project_, *committed) !=
         nullptr;
}

bool SelectionToolHandler::step_entry_cursor_live() const {
  if (!step_entry_cursor_.has_value()) {
    return false;
  }
  const StepEntryCursor& cursor = *step_entry_cursor_;
  return live_stave(cursor.node, cursor.track, cursor.stave) &&
         resolve_voice(cursor.node, cursor.track, cursor.stave, cursor.voice) !=
             nullptr;
}

bool SelectionToolHandler::step_entry_pitch_available(
    graphscore::Letter letter) const {
  if (active_tool_ != graphscore::ActiveTool::kNoteEntry ||
      !step_entry_cursor_live()) {
    return false;
  }
  const auto pitch = resolve_pitch_for_letter(letter);
  if (!pitch.has_value()) {
    return false;
  }
  const StepEntryCursor& cursor  = *step_entry_cursor_;
  auto                   command = make_note_entry_command_at(
      cursor.node, cursor.track, cursor.stave, cursor.voice, cursor.position,
      graphscore::NotePaletteEntryKind::kNote, pitch);
  if (command == nullptr) {
    return false;
  }
  graphscore::Project candidate = project_;
  return command->execute(candidate).ok();
}

bool SelectionToolHandler::step_entry_rest_available() const {
  if (active_tool_ != graphscore::ActiveTool::kNoteEntry ||
      !step_entry_cursor_live()) {
    return false;
  }
  const StepEntryCursor& cursor  = *step_entry_cursor_;
  auto                   command = make_note_entry_command_at(
      cursor.node, cursor.track, cursor.stave, cursor.voice, cursor.position,
      graphscore::NotePaletteEntryKind::kRest, std::nullopt);
  if (command == nullptr) {
    return false;
  }
  graphscore::Project candidate = project_;
  return command->execute(candidate).ok();
}

namespace {

[[nodiscard]] std::optional<
    std::pair<graphscore::IntervalDirection, std::uint8_t>>
palette_interval(PaletteCommandId id) {
  const int value = static_cast<int>(id);
  const int above = static_cast<int>(PaletteCommandId::kIntervalAbove2);
  const int below = static_cast<int>(PaletteCommandId::kIntervalBelow2);
  if (value >= above && value <= above + 6) {
    return std::pair{graphscore::IntervalDirection::kAbove,
                     static_cast<std::uint8_t>(2 + value - above)};
  }
  if (value >= below && value <= below + 6) {
    return std::pair{graphscore::IntervalDirection::kBelow,
                     static_cast<std::uint8_t>(2 + value - below)};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<graphscore::Letter> palette_letter(
    PaletteCommandId id) {
  const int value = static_cast<int>(id);
  const int first = static_cast<int>(PaletteCommandId::kPitchA);
  if (value >= first && value <= first + 6) {
    return static_cast<graphscore::Letter>(
        static_cast<std::uint8_t>(value - first));
  }
  return std::nullopt;
}

[[nodiscard]] bool palette_action_requires_usable_history(PaletteCommandId id) {
  switch (id) {
    case PaletteCommandId::kMoveNoteUp:
    case PaletteCommandId::kMoveNoteDown:
    case PaletteCommandId::kAccidentalDown:
    case PaletteCommandId::kAccidentalUp:
    case PaletteCommandId::kDeleteNotehead:
    case PaletteCommandId::kConvertToRest:
    case PaletteCommandId::kIntervalAbove2:
    case PaletteCommandId::kIntervalAbove3:
    case PaletteCommandId::kIntervalAbove4:
    case PaletteCommandId::kIntervalAbove5:
    case PaletteCommandId::kIntervalAbove6:
    case PaletteCommandId::kIntervalAbove7:
    case PaletteCommandId::kIntervalAbove8:
    case PaletteCommandId::kIntervalBelow2:
    case PaletteCommandId::kIntervalBelow3:
    case PaletteCommandId::kIntervalBelow4:
    case PaletteCommandId::kIntervalBelow5:
    case PaletteCommandId::kIntervalBelow6:
    case PaletteCommandId::kIntervalBelow7:
    case PaletteCommandId::kIntervalBelow8:
    case PaletteCommandId::kPitchA:
    case PaletteCommandId::kPitchB:
    case PaletteCommandId::kPitchC:
    case PaletteCommandId::kPitchD:
    case PaletteCommandId::kPitchE:
    case PaletteCommandId::kPitchF:
    case PaletteCommandId::kPitchG:
    case PaletteCommandId::kCut:
    case PaletteCommandId::kPaste:
    case PaletteCommandId::kUndo:
    case PaletteCommandId::kRedo:
    case PaletteCommandId::kStepEntryRest:
    case PaletteCommandId::kInsertMeasureBefore:
    case PaletteCommandId::kAppendMeasure:
    case PaletteCommandId::kDeleteMeasure:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool palette_history_eligible(PaletteCommandId id,
                                            bool             history_poisoned) {
  return !palette_action_requires_usable_history(id) || !history_poisoned;
}

}  // namespace

bool SelectionToolHandler::palette_command_available(
    PaletteCommandId id) const {
  if (!palette_history_eligible(id, history_.poisoned())) {
    return false;
  }
  switch (id) {
    case PaletteCommandId::kMoveNoteUp:
    case PaletteCommandId::kMoveNoteDown: {
      const auto item = single_notehead_item();
      if (!item.has_value()) {
        return false;
      }
      const auto direction = id == PaletteCommandId::kMoveNoteUp
                                 ? graphscore::NoteheadStepDirection::kUp
                                 : graphscore::NoteheadStepDirection::kDown;
      // The same source check make_move_notehead_command uses, including a
      // stale identity and a step that would leave the pitch range.
      return graphscore::audition_for_notehead_move(project_, *item, direction)
          .has_value();
    }
    case PaletteCommandId::kAccidentalDown:
    case PaletteCommandId::kAccidentalUp: {
      const auto item = single_notehead_item();
      if (!item.has_value()) {
        return false;
      }
      const auto direction = id == PaletteCommandId::kAccidentalDown
                                 ? graphscore::AccidentalStepDirection::kLower
                                 : graphscore::AccidentalStepDirection::kRaise;
      return graphscore::audition_for_accidental_step(project_, *item,
                                                      direction)
          .has_value();
    }
    case PaletteCommandId::kDeleteNotehead: {
      const auto item = single_notehead_item();
      if (!item.has_value()) {
        return false;
      }
      // A stale identity or a non-notehead entity is the same no-op
      // make_delete_notehead_command rejects.
      return graphscore::make_delete_notehead_command(project_, *item) !=
             nullptr;
    }
    case PaletteCommandId::kConvertToRest:
      return convert_to_rest_available();
    case PaletteCommandId::kIntervalAbove2:
    case PaletteCommandId::kIntervalAbove3:
    case PaletteCommandId::kIntervalAbove4:
    case PaletteCommandId::kIntervalAbove5:
    case PaletteCommandId::kIntervalAbove6:
    case PaletteCommandId::kIntervalAbove7:
    case PaletteCommandId::kIntervalAbove8:
    case PaletteCommandId::kIntervalBelow2:
    case PaletteCommandId::kIntervalBelow3:
    case PaletteCommandId::kIntervalBelow4:
    case PaletteCommandId::kIntervalBelow5:
    case PaletteCommandId::kIntervalBelow6:
    case PaletteCommandId::kIntervalBelow7:
    case PaletteCommandId::kIntervalBelow8: {
      const auto item = single_notehead_item();
      if (!item.has_value()) {
        return false;
      }
      const auto interval = palette_interval(id);
      if (!interval.has_value()) {
        return false;
      }
      // GraceNote, stale identity, no-timeline, and out-of-main-region are
      // all the same no-op make_add_interval_command rejects.
      return graphscore::make_add_interval_command(
                 project_, *item, interval->second, interval->first) != nullptr;
    }
    case PaletteCommandId::kToggleTool:
    case PaletteCommandId::kTogglePalette:
      return true;
    case PaletteCommandId::kPitchA:
    case PaletteCommandId::kPitchB:
    case PaletteCommandId::kPitchC:
    case PaletteCommandId::kPitchD:
    case PaletteCommandId::kPitchE:
    case PaletteCommandId::kPitchF:
    case PaletteCommandId::kPitchG: {
      const auto letter = palette_letter(id);
      return letter.has_value() && step_entry_pitch_available(*letter);
    }
    case PaletteCommandId::kStepEntryRest:
      return step_entry_rest_available();
    case PaletteCommandId::kCycleDots:
    case PaletteCommandId::kDurationWhole:
    case PaletteCommandId::kDurationHalf:
    case PaletteCommandId::kDurationQuarter:
    case PaletteCommandId::kDurationEighth:
    case PaletteCommandId::kDurationSixteenth:
    case PaletteCommandId::kDurationThirtySecond:
    case PaletteCommandId::kDurationSixtyFourth:
    case PaletteCommandId::kVoice1:
    case PaletteCommandId::kVoice2:
    case PaletteCommandId::kVoice3:
    case PaletteCommandId::kVoice4:
      return active_tool_ == graphscore::ActiveTool::kNoteEntry;
    case PaletteCommandId::kOctaveUp:
      return octave_step_available(1);
    case PaletteCommandId::kOctaveDown:
      return octave_step_available(-1);
    case PaletteCommandId::kRangeEdgeEarlier:
    case PaletteCommandId::kRangeEdgeLater:
    case PaletteCommandId::kRangeStaffScopeUp:
    case PaletteCommandId::kRangeStaffScopeDown:
    case PaletteCommandId::kRangeSelectToStart:
    case PaletteCommandId::kRangeSelectToEnd:
    case PaletteCommandId::kAccessibleRangeStart:
    case PaletteCommandId::kAccessibleRangeEnd:
    case PaletteCommandId::kAccessibleRangeStaffScope:
      // Range actions are Selection-tool actions (§7.2) with a committed
      // range precondition, matching their chord's tool gate + fallback.
      return active_tool_ == graphscore::ActiveTool::kSelection &&
             current_range_set() != nullptr;
    case PaletteCommandId::kStaffStepPrevious:
      return staff_step_available(graphscore::StaffStepDirection::kPrevious);
    case PaletteCommandId::kStaffStepNext:
      return staff_step_available(graphscore::StaffStepDirection::kNext);
    case PaletteCommandId::kCut:
    case PaletteCommandId::kCopy:
      return copy_cut_available();
    case PaletteCommandId::kPaste:
      return paste_available();
    case PaletteCommandId::kUndo:
      return history_.can_undo();
    case PaletteCommandId::kRedo:
      return history_.can_redo();
    case PaletteCommandId::kInsertMeasureBefore:
      return measure_insert_available(graphscore::MeasureInsertMode::kBefore);
    case PaletteCommandId::kAppendMeasure:
      return measure_insert_available(graphscore::MeasureInsertMode::kAppend);
    case PaletteCommandId::kDeleteMeasure:
      return measure_delete_available();
  }
  return false;
}

std::string SelectionToolHandler::palette_command_unavailable_reason(
    PaletteCommandId id) const {
  if (!palette_history_eligible(id, history_.poisoned())) {
    return "command history is unavailable";
  }
  switch (id) {
    case PaletteCommandId::kMoveNoteUp:
    case PaletteCommandId::kMoveNoteDown:
    case PaletteCommandId::kAccidentalDown:
    case PaletteCommandId::kAccidentalUp:
    case PaletteCommandId::kDeleteNotehead:
    case PaletteCommandId::kIntervalAbove2:
    case PaletteCommandId::kIntervalAbove3:
    case PaletteCommandId::kIntervalAbove4:
    case PaletteCommandId::kIntervalAbove5:
    case PaletteCommandId::kIntervalAbove6:
    case PaletteCommandId::kIntervalAbove7:
    case PaletteCommandId::kIntervalAbove8:
    case PaletteCommandId::kIntervalBelow2:
    case PaletteCommandId::kIntervalBelow3:
    case PaletteCommandId::kIntervalBelow4:
    case PaletteCommandId::kIntervalBelow5:
    case PaletteCommandId::kIntervalBelow6:
    case PaletteCommandId::kIntervalBelow7:
    case PaletteCommandId::kIntervalBelow8:
      return "no single notehead selected";
    case PaletteCommandId::kConvertToRest:
      return "no single notehead or chord selected";
    case PaletteCommandId::kPitchA:
    case PaletteCommandId::kPitchB:
    case PaletteCommandId::kPitchC:
    case PaletteCommandId::kPitchD:
    case PaletteCommandId::kPitchE:
    case PaletteCommandId::kPitchF:
    case PaletteCommandId::kPitchG:
    case PaletteCommandId::kStepEntryRest:
      if (active_tool_ != graphscore::ActiveTool::kNoteEntry) {
        return "note-entry tool is not active";
      }
      if (!step_entry_cursor_live()) {
        return "step-entry cursor is stale or absent";
      }
      return "armed step-entry commit is not valid at the cursor";
    case PaletteCommandId::kCycleDots:
    case PaletteCommandId::kDurationWhole:
    case PaletteCommandId::kDurationHalf:
    case PaletteCommandId::kDurationQuarter:
    case PaletteCommandId::kDurationEighth:
    case PaletteCommandId::kDurationSixteenth:
    case PaletteCommandId::kDurationThirtySecond:
    case PaletteCommandId::kDurationSixtyFourth:
    case PaletteCommandId::kVoice1:
    case PaletteCommandId::kVoice2:
    case PaletteCommandId::kVoice3:
    case PaletteCommandId::kVoice4:
      return "note-entry tool is not active";
    case PaletteCommandId::kOctaveUp:
      return active_tool_ != graphscore::ActiveTool::kNoteEntry
                 ? "note-entry tool is not active"
                 : "octave reference is at the upper bound";
    case PaletteCommandId::kOctaveDown:
      return active_tool_ != graphscore::ActiveTool::kNoteEntry
                 ? "note-entry tool is not active"
                 : "octave reference is at the lower bound";
    case PaletteCommandId::kRangeEdgeEarlier:
    case PaletteCommandId::kRangeEdgeLater:
    case PaletteCommandId::kRangeStaffScopeUp:
    case PaletteCommandId::kRangeStaffScopeDown:
    case PaletteCommandId::kRangeSelectToStart:
    case PaletteCommandId::kRangeSelectToEnd:
    case PaletteCommandId::kAccessibleRangeStart:
    case PaletteCommandId::kAccessibleRangeEnd:
    case PaletteCommandId::kAccessibleRangeStaffScope:
      if (active_tool_ != graphscore::ActiveTool::kSelection) {
        return "selection tool is not active";
      }
      return "no range selection";
    case PaletteCommandId::kStaffStepPrevious:
    case PaletteCommandId::kStaffStepNext:
      return active_tool_ == graphscore::ActiveTool::kNoteEntry
                 ? "no eligible step-entry cursor or adjacent staff"
                 : "no eligible selection";
    case PaletteCommandId::kCut:
    case PaletteCommandId::kCopy:
      return "requires a full-measure or range selection";
    case PaletteCommandId::kPaste:
      if (!clipboard_.has_value()) {
        return "clipboard is empty";
      }
      if (!derive_paste_anchor().has_value()) {
        return "no live paste anchor";
      }
      return "paste placement or meter is invalid";
    case PaletteCommandId::kUndo:
      return "nothing to undo";
    case PaletteCommandId::kRedo:
      return "nothing to redo";
    case PaletteCommandId::kToggleTool:
    case PaletteCommandId::kTogglePalette:
      return "";
    case PaletteCommandId::kInsertMeasureBefore:
    case PaletteCommandId::kAppendMeasure:
      return "requires an aligned full-measure selection";
    case PaletteCommandId::kDeleteMeasure: {
      // Alignment is checked first, via the exact aligned/valid precondition
      // make_insert_measure_command already enforces (delete requires the
      // identical alignment -- see make_delete_measure_command's own
      // comment), so a misaligned FullMeasureSet is reported as misaligned
      // rather than misattributed to the sole-measure guard below merely
      // because its front item's node happens to carry one measure.
      const auto& committed = drag_.committed_selection();
      if (!committed.has_value() ||
          graphscore::make_insert_measure_command(
              project_, *committed, graphscore::MeasureInsertMode::kBefore) ==
              nullptr) {
        return "requires an aligned full-measure selection";
      }
      const auto*             set = current_measure_set();
      const graphscore::Node* node_ptr =
          project_.find_node(set->items().front().node);
      if (node_ptr != nullptr && node_ptr->timeline() != nullptr &&
          node_ptr->timeline()->measures().measure_count() <= 1u) {
        return "cannot delete the node's only measure";
      }
      return "requires an aligned full-measure selection";
    }
  }
  return "";
}

namespace {

[[nodiscard]] std::optional<graphscore::NoteValue> palette_duration(
    PaletteCommandId id) {
  const int value = static_cast<int>(id);
  const int first = static_cast<int>(PaletteCommandId::kDurationWhole);
  if (value >= first && value <= first + 6) {
    return static_cast<graphscore::NoteValue>(
        static_cast<std::uint8_t>(value - first));
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<graphscore::Voice> palette_voice(
    PaletteCommandId id) {
  const int value = static_cast<int>(id);
  const int first = static_cast<int>(PaletteCommandId::kVoice1);
  if (value >= first && value <= first + 3) {
    const auto voice =
        graphscore::Voice::create(static_cast<std::uint8_t>(value - first + 1));
    return voice;
  }
  return std::nullopt;
}

// The tool gate of a palette row, mirroring the action table's Tool column
// (§7): Entry-tool actions are live only while kNoteEntry is active;
// Selection-tool actions (range extension and the accessible range controls)
// are live only while kSelection is active; everything else is Any/Both and
// is not gated by the tool.
enum class PaletteToolGate : std::uint8_t { kAny, kEntry, kSelection };

[[nodiscard]] PaletteToolGate palette_tool_gate(PaletteCommandId id) {
  switch (id) {
    case PaletteCommandId::kPitchA:
    case PaletteCommandId::kPitchB:
    case PaletteCommandId::kPitchC:
    case PaletteCommandId::kPitchD:
    case PaletteCommandId::kPitchE:
    case PaletteCommandId::kPitchF:
    case PaletteCommandId::kPitchG:
    case PaletteCommandId::kStepEntryRest:
    case PaletteCommandId::kCycleDots:
    case PaletteCommandId::kDurationWhole:
    case PaletteCommandId::kDurationHalf:
    case PaletteCommandId::kDurationQuarter:
    case PaletteCommandId::kDurationEighth:
    case PaletteCommandId::kDurationSixteenth:
    case PaletteCommandId::kDurationThirtySecond:
    case PaletteCommandId::kDurationSixtyFourth:
    case PaletteCommandId::kVoice1:
    case PaletteCommandId::kVoice2:
    case PaletteCommandId::kVoice3:
    case PaletteCommandId::kVoice4:
    case PaletteCommandId::kOctaveUp:
    case PaletteCommandId::kOctaveDown:
      return PaletteToolGate::kEntry;
    case PaletteCommandId::kRangeEdgeEarlier:
    case PaletteCommandId::kRangeEdgeLater:
    case PaletteCommandId::kRangeStaffScopeUp:
    case PaletteCommandId::kRangeStaffScopeDown:
    case PaletteCommandId::kRangeSelectToStart:
    case PaletteCommandId::kRangeSelectToEnd:
    case PaletteCommandId::kAccessibleRangeStart:
    case PaletteCommandId::kAccessibleRangeEnd:
    case PaletteCommandId::kAccessibleRangeStaffScope:
      return PaletteToolGate::kSelection;
    default:
      return PaletteToolGate::kAny;
  }
}

}  // namespace

bool SelectionToolHandler::run_palette_command(PaletteCommandId id) {
  // Tool gate (§11): running an Entry-tool action while kSelection is active
  // is the same silent no-op as pressing its chord (the letter/numpad/Alt
  // routing is inert outside kNoteEntry); likewise a Selection-tool action
  // while kNoteEntry is active. The gate is checked before the action, so no
  // diagnostic is posted and no state (palette arming, cursor) is touched.
  const PaletteToolGate gate = palette_tool_gate(id);
  if (gate == PaletteToolGate::kEntry &&
      active_tool_ != graphscore::ActiveTool::kNoteEntry) {
    return false;
  }
  if (gate == PaletteToolGate::kSelection &&
      active_tool_ != graphscore::ActiveTool::kSelection) {
    return false;
  }

  switch (id) {
    case PaletteCommandId::kMoveNoteUp:
      return move_selected_notehead(graphscore::NoteheadStepDirection::kUp);
    case PaletteCommandId::kMoveNoteDown:
      return move_selected_notehead(graphscore::NoteheadStepDirection::kDown);
    case PaletteCommandId::kAccidentalDown:
      return step_selected_accidental(
          graphscore::AccidentalStepDirection::kLower);
    case PaletteCommandId::kAccidentalUp:
      return step_selected_accidental(
          graphscore::AccidentalStepDirection::kRaise);
    case PaletteCommandId::kDeleteNotehead:
      return delete_selected_notehead();
    case PaletteCommandId::kConvertToRest:
      return convert_selection_to_rest();
    case PaletteCommandId::kIntervalAbove2:
    case PaletteCommandId::kIntervalAbove3:
    case PaletteCommandId::kIntervalAbove4:
    case PaletteCommandId::kIntervalAbove5:
    case PaletteCommandId::kIntervalAbove6:
    case PaletteCommandId::kIntervalAbove7:
    case PaletteCommandId::kIntervalAbove8:
    case PaletteCommandId::kIntervalBelow2:
    case PaletteCommandId::kIntervalBelow3:
    case PaletteCommandId::kIntervalBelow4:
    case PaletteCommandId::kIntervalBelow5:
    case PaletteCommandId::kIntervalBelow6:
    case PaletteCommandId::kIntervalBelow7:
    case PaletteCommandId::kIntervalBelow8: {
      const auto interval = palette_interval(id);
      if (!interval.has_value()) {
        return false;
      }
      return run_interval_action(interval->first, interval->second);
    }
    case PaletteCommandId::kToggleTool:
      toggle_tool();
      return true;
    case PaletteCommandId::kPitchA:
    case PaletteCommandId::kPitchB:
    case PaletteCommandId::kPitchC:
    case PaletteCommandId::kPitchD:
    case PaletteCommandId::kPitchE:
    case PaletteCommandId::kPitchF:
    case PaletteCommandId::kPitchG: {
      const auto letter = palette_letter(id);
      if (!letter.has_value()) {
        return false;
      }
      return step_entry_commit_pitch(*letter);
    }
    case PaletteCommandId::kRangeEdgeEarlier:
    case PaletteCommandId::kRangeEdgeLater: {
      const auto* existing = current_range_set();
      if (existing == nullptr || existing->items().empty()) {
        return false;
      }
      const graphscore::MusicalSpan& span = existing->items().front().span;
      const graphscore::Rational     current =
          focus_edge_ == graphscore::RangeEdge::kStart ? span.start : span.end;
      const graphscore::Rational target =
          id == PaletteCommandId::kRangeEdgeEarlier
              ? current - kProvisionalRangeExtensionStep
              : current + kProvisionalRangeExtensionStep;
      return extend_range_edge(focus_edge_, target);
    }
    case PaletteCommandId::kRangeStaffScopeUp:
      return step_staff_scope(-1);
    case PaletteCommandId::kRangeStaffScopeDown:
      return step_staff_scope(1);
    case PaletteCommandId::kRangeSelectToStart:
      return select_to_node_start();
    case PaletteCommandId::kRangeSelectToEnd:
      return select_to_node_end();
    case PaletteCommandId::kStaffStepPrevious:
      return step_selected_staff(graphscore::StaffStepDirection::kPrevious);
    case PaletteCommandId::kStaffStepNext:
      return step_selected_staff(graphscore::StaffStepDirection::kNext);
    case PaletteCommandId::kCut:
      return cut_selection();
    case PaletteCommandId::kCopy:
      return copy_selection();
    case PaletteCommandId::kPaste:
      return paste_clipboard();
    case PaletteCommandId::kUndo:
      return undo_action();
    case PaletteCommandId::kRedo:
      return redo_action();
    case PaletteCommandId::kTogglePalette:
      toggle_command_palette();
      return true;
    case PaletteCommandId::kVoice1:
    case PaletteCommandId::kVoice2:
    case PaletteCommandId::kVoice3:
    case PaletteCommandId::kVoice4: {
      const auto voice = palette_voice(id);
      if (!voice.has_value()) {
        return false;
      }
      return step_entry_arm_voice(*voice);
    }
    case PaletteCommandId::kOctaveUp:
      return step_entry_step_octave(1);
    case PaletteCommandId::kOctaveDown:
      return step_entry_step_octave(-1);
    case PaletteCommandId::kDurationWhole:
    case PaletteCommandId::kDurationHalf:
    case PaletteCommandId::kDurationQuarter:
    case PaletteCommandId::kDurationEighth:
    case PaletteCommandId::kDurationSixteenth:
    case PaletteCommandId::kDurationThirtySecond:
    case PaletteCommandId::kDurationSixtyFourth: {
      const auto duration = palette_duration(id);
      if (!duration.has_value()) {
        return false;
      }
      return step_entry_arm_duration(*duration);
    }
    case PaletteCommandId::kStepEntryRest:
      return step_entry_commit_rest();
    case PaletteCommandId::kCycleDots:
      return step_entry_cycle_dots();
    case PaletteCommandId::kAccessibleRangeStart:
      return select_to_node_start();
    case PaletteCommandId::kAccessibleRangeEnd:
      return select_to_node_end();
    case PaletteCommandId::kAccessibleRangeStaffScope:
      // The staff-scope accessible control is parameterized (AT supplies the
      // endpoints); the palette route cannot name them, so its execution is
      // an explicit no-op with a diagnostic.
      post_diagnostic("range staff scope requires explicit endpoints");
      return false;
    case PaletteCommandId::kInsertMeasureBefore:
      return insert_measure_before();
    case PaletteCommandId::kAppendMeasure:
      return append_measure();
    case PaletteCommandId::kDeleteMeasure:
      return delete_measure();
  }
  return false;
}

}  // namespace graphscore::writer_app
