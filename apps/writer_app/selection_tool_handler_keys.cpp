// SPDX-License-Identifier: Apache-2.0

#include "key_bindings.hpp"
#include "selection_tool_handler.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace graphscore::writer_app {

// The platform-normalized action table
// (docs/plan/05-notation-editor-action-table.md §3-§11). Five disjoint bound
// chord classes plus an explicit unbound remainder; physical identity for
// positional/digit/symbol keys and logical identity for letter mnemonics
// (§4); a single focus owner with the command palette above the notation
// context (§5); and per-binding repeat-safe vs repeat-once policy (§6).
void SelectionToolHandler::on_key_press(graphscore::KeyEvent event) {
  const ChordClass chord =
      classify_chord(event.modifiers, kPlatformPrimaryModifier);

  // Focus context 2: the command palette, above the notation context. Only
  // the palette's own keys are interpreted; every notation binding is
  // suppressed, and character keys are consumed here (their composed text is
  // what feeds the filter, delivered on the separate text-input channel --
  // §5 context 2, §4). Primary+K is the close here and the toggle in the
  // notation context.
  if (palette_open_) {
    if (chord == ChordClass::kPrimary &&
        event.logical == graphscore::LogicalKey::kK) {
      // §6: the command palette is repeat-once — the initial press toggled it
      // open, so an auto-repeat must not immediately close it again.
      if (!event.repeat) {
        close_command_palette();
      }
      return;
    }
    if (chord == ChordClass::kUnmodified &&
        event.code == graphscore::KeyCode::kEscape) {
      if (!event.repeat) {
        close_command_palette();
      }
      return;
    }
    if (chord == ChordClass::kUnmodified) {
      switch (event.code) {
        case graphscore::KeyCode::kUp:
          command_palette_move_selection(-1);
          return;
        case graphscore::KeyCode::kDown:
          command_palette_move_selection(1);
          return;
        case graphscore::KeyCode::kReturn:
          std::ignore = command_palette_run_selected();
          return;
        default:
          break;
      }
    }
    // Every other key — all character keys included — is consumed here and
    // never reaches a notation binding; the filter accumulates only composed
    // text (see on_text_input below).
    return;
  }

  switch (chord) {
    case ChordClass::kUnmodified: {
      // Numpad key family (physical, Num Lock-independent): durations, the
      // step-entry rest, and the dot cycle are Entry-tool actions (§7.6).
      if (const auto note_value = numpad_duration(event.code);
          note_value.has_value()) {
        if (active_tool_ == graphscore::ActiveTool::kNoteEntry) {
          std::ignore = step_entry_arm_duration(*note_value);
        }
        return;
      }
      if (event.code == graphscore::KeyCode::kNumPad0) {
        if (active_tool_ == graphscore::ActiveTool::kNoteEntry) {
          std::ignore = step_entry_commit_rest();
        }
        return;
      }
      if (event.code == graphscore::KeyCode::kNumPadDecimal) {
        if (active_tool_ == graphscore::ActiveTool::kNoteEntry &&
            !event.repeat) {
          std::ignore = step_entry_cycle_dots();
        }
        return;
      }

      // Letter mnemonics by logical identity (§4).
      if (event.logical == graphscore::LogicalKey::kN) {
        if (!event.repeat) {
          toggle_tool();
        }
        return;
      }
      if (const auto letter = logical_pitch_letter(event.logical);
          letter.has_value()) {
        if (active_tool_ == graphscore::ActiveTool::kNoteEntry) {
          std::ignore = step_entry_commit_pitch(*letter);
        }
        return;
      }
      if (event.logical == graphscore::LogicalKey::kR) {
        std::ignore = convert_selection_to_rest();
        return;
      }

      // Physical editing keys (Both tools).
      switch (event.code) {
        case graphscore::KeyCode::kBackspace:
        case graphscore::KeyCode::kDelete:
          std::ignore = delete_selected_notehead();
          return;
        case graphscore::KeyCode::kUp:
          std::ignore =
              move_selected_notehead(graphscore::NoteheadStepDirection::kUp);
          return;
        case graphscore::KeyCode::kDown:
          std::ignore =
              move_selected_notehead(graphscore::NoteheadStepDirection::kDown);
          return;
        case graphscore::KeyCode::kMinus:
          std::ignore = step_selected_accidental(
              graphscore::AccidentalStepDirection::kLower);
          return;
        case graphscore::KeyCode::kEquals:
          std::ignore = step_selected_accidental(
              graphscore::AccidentalStepDirection::kRaise);
          return;
        default:
          break;
      }

      // Top-row `2`..`8` add a diatonic interval above (Both); `1`, `9`,
      // and `0` are explicit no-ops (§7.1, §7.7).
      if (const auto interval = digit_interval(event.code);
          interval.has_value()) {
        std::ignore = run_interval_action(graphscore::IntervalDirection::kAbove,
                                          *interval);
      }
      return;
    }

    case ChordClass::kShift: {
      // Shift+`2`..`8` add a diatonic interval below (Both), tested before
      // range extension so Shift+digits never reach it (§7.2).
      if (const auto interval = digit_interval(event.code);
          interval.has_value()) {
        std::ignore = run_interval_action(graphscore::IntervalDirection::kBelow,
                                          *interval);
        return;
      }
      // Range extension is a Selection-tool action only (§7.2).
      if (active_tool_ != graphscore::ActiveTool::kSelection) {
        return;
      }
      const auto* existing = current_range_set();
      if (existing == nullptr || existing->items().empty()) {
        return;
      }
      const graphscore::MusicalSpan& span = existing->items().front().span;
      switch (event.code) {
        case graphscore::KeyCode::kLeft: {
          const graphscore::Rational current =
              focus_edge_ == graphscore::RangeEdge::kStart ? span.start
                                                           : span.end;
          std::ignore = extend_range_edge(
              focus_edge_, current - kProvisionalRangeExtensionStep);
          break;
        }
        case graphscore::KeyCode::kRight: {
          const graphscore::Rational current =
              focus_edge_ == graphscore::RangeEdge::kStart ? span.start
                                                           : span.end;
          std::ignore = extend_range_edge(
              focus_edge_, current + kProvisionalRangeExtensionStep);
          break;
        }
        case graphscore::KeyCode::kUp:
          std::ignore = step_staff_scope(-1);
          break;
        case graphscore::KeyCode::kDown:
          std::ignore = step_staff_scope(1);
          break;
        case graphscore::KeyCode::kHome:
          std::ignore = select_to_node_start();
          break;
        case graphscore::KeyCode::kEnd:
          std::ignore = select_to_node_end();
          break;
        case graphscore::KeyCode::kUnknown:
        default:
          break;
      }
      return;
    }

    case ChordClass::kPrimary: {
      // Primary+Up/Down step staffs (Both tools).
      switch (event.code) {
        case graphscore::KeyCode::kUp:
          std::ignore =
              step_selected_staff(graphscore::StaffStepDirection::kPrevious);
          return;
        case graphscore::KeyCode::kDown:
          std::ignore =
              step_selected_staff(graphscore::StaffStepDirection::kNext);
          return;
        default:
          break;
      }
      // Clipboard / undo / palette by logical letter mnemonic (§7.3).
      switch (event.logical) {
        case graphscore::LogicalKey::kX:
          if (!event.repeat) {
            std::ignore = cut_selection();
          }
          return;
        case graphscore::LogicalKey::kC:
          std::ignore = copy_selection();
          return;
        case graphscore::LogicalKey::kV:
          if (!event.repeat) {
            std::ignore = paste_clipboard();
          }
          return;
        case graphscore::LogicalKey::kZ:
          std::ignore = undo_action();
          return;
        case graphscore::LogicalKey::kK:
          if (!event.repeat) {
            toggle_command_palette();
          }
          return;
        case graphscore::LogicalKey::kUnknown:
        default:
          return;
      }
    }

    case ChordClass::kShiftPrimary: {
      if (event.logical == graphscore::LogicalKey::kZ) {
        std::ignore = redo_action();
      }
      return;
    }

    case ChordClass::kAlt: {
      if (active_tool_ != graphscore::ActiveTool::kNoteEntry) {
        return;
      }
      // Alt+`1`..`4` (physical digits) arm voices 1-4 (§7.5).
      if (const auto digit = top_row_digit(event.code);
          digit.has_value() && *digit >= 1 && *digit <= 4) {
        const auto voice = graphscore::Voice::create(*digit);
        if (voice.has_value()) {
          std::ignore = step_entry_arm_voice(*voice);
        }
        return;
      }
      // Alt+Up/Down step the octave reference (§7.5).
      switch (event.code) {
        case graphscore::KeyCode::kUp:
          std::ignore = step_entry_step_octave(1);
          return;
        case graphscore::KeyCode::kDown:
          std::ignore = step_entry_step_octave(-1);
          return;
        default:
          return;
      }
    }

    case ChordClass::kUnbound:
      // The explicit no-op remainder: mixed chords, the non-Primary modifier,
      // and every other combination outside the five bound classes (§3).
      return;
  }
}

// Composed text input (SDL_EVENT_TEXT_INPUT), delivered on a channel
// separate from key identity (§4): while the command palette is open (focus
// context 2, §5) every printable text accumulates into the filter, including
// shifted characters, symbols, and non-US-layout compositions that never map
// to a bound LogicalKey. The notation context consumes no text input — all
// of its actions are key chords routed through on_key_press.
void SelectionToolHandler::on_text_input(graphscore::TextInputEvent event) {
  if (palette_open_ && !event.text.empty()) {
    command_palette_set_filter(command_palette_filter() + event.text);
  }
}

}  // namespace graphscore::writer_app
