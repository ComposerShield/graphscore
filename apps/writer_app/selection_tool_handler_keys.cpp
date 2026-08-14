// SPDX-License-Identifier: Apache-2.0

#include "key_bindings.hpp"
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

// Provisional keyboard bindings (M5-phase-19b-iii), superseded by
// M5-phase-26/M5-phase-27's normalized action table. Shift is required
// for every range-extension binding below; control, alt, and meta never
// substitute for it, and an unmodified arrow or a non-Shift chord is a
// no-op. Acts only when the selection tool is active and a committed
// ArbitraryRangeSet selection already exists to extend -- this sub-phase
// never creates a first selection from the keyboard alone.
//
//   Shift+Left/Right  -- move the current focus edge (focus_edge_) one
//                        provisional step earlier/later.
//   Shift+Up/Down     -- extend the staff scope by one staff in score
//                        order (up = earlier, down = later).
//   Shift+Home/End    -- select_to_node_start()/select_to_node_end().
//
// M5-phase-20 adds unmodified Up/Down: with exactly one selected notehead,
// the notehead moves one diatonic staff step (move_selected_notehead). This
// never conflicts with Shift+Up/Down above, which stays range extension.
//
// M5-phase-21 adds unmodified `-`/`=`: with exactly one selected notehead,
// its accidental steps one rung down/up the double-flat .. double-sharp
// ladder (step_selected_accidental). Shift+`-`/`=` is not a binding either
// phase owns, so it falls into the Shift branch above and is a no-op there.
//
// M5-phase-23 adds unmodified `R`: with a single-item NoteheadSet or
// ChordSet selected, converts the entire selected note/chord event to an
// equal-duration rest (convert_selection_to_rest). Shift+`R` is not a
// binding this phase owns, so it falls into the Shift branch above and is
// a no-op there.
//
// M5-phase-24 adds Primary+Up/Down (Command on macOS, Control elsewhere;
// see is_primary_chord): the selection moves to the prior/next staff of
// the node (step_selected_staff). It also TIGHTENS the Shift branch above
// to an exact Shift chord. Shift used to be tested first and returned
// unconditionally, so Shift+Primary+Up silently performed range
// staff-scope extension; that chord is now a no-op, matching the
// exact-chord discipline every other binding here already follows.
void SelectionToolHandler::on_key_press(graphscore::KeyEvent event) {
  if (active_tool_ != graphscore::ActiveTool::kSelection) {
    return;
  }
  if (event.modifiers.shift && !event.modifiers.control &&
      !event.modifiers.alt && !event.modifiers.meta) {
    // M5-phase-25: Shift+`2`..`8` add a key-spelled diatonic interval BELOW
    // the single selected notehead. Tested before the range-extension
    // branch so Shift+digits are never swallowed by range extension, while
    // the exact Shift+arrow/Home/End behavior below is preserved unchanged.
    if (const auto interval = digit_interval(event.code);
        interval.has_value()) {
      std::ignore = add_selected_interval(graphscore::IntervalDirection::kBelow,
                                          *interval);
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

  // M5-phase-24: Primary+Up/Down steps the selection to the prior/next
  // staff. Tested before the unmodified branch's modifier guard below
  // because that guard rejects every control/meta chord, and after the
  // Shift branch above because is_primary_chord requires Shift clear
  // anyway (so Shift+Primary+Up reaches neither and is a no-op).
  if (is_primary_chord(event.modifiers, kPlatformPrimaryModifier)) {
    switch (event.code) {
      case graphscore::KeyCode::kUp:
        std::ignore =
            step_selected_staff(graphscore::StaffStepDirection::kPrevious);
        break;
      case graphscore::KeyCode::kDown:
        std::ignore =
            step_selected_staff(graphscore::StaffStepDirection::kNext);
        break;
      case graphscore::KeyCode::kUnknown:
      default:
        break;
    }
    return;
  }

  // M5-phase-20: unmodified Up/Down moves the single selected notehead one
  // diatonic staff step. M5-phase-21: unmodified `-`/`=` steps that
  // notehead's accidental one rung down/up the double-flat .. double-sharp
  // ladder. Any other modifier chord is not a binding these phases own and
  // is a no-op (the full action table is M5-phase-26/27's).
  if (event.modifiers.control || event.modifiers.alt || event.modifiers.meta) {
    return;
  }
  switch (event.code) {
    case graphscore::KeyCode::kBackspace:
    case graphscore::KeyCode::kDelete:
      std::ignore = delete_selected_notehead();
      break;
    case graphscore::KeyCode::kUp:
      std::ignore =
          move_selected_notehead(graphscore::NoteheadStepDirection::kUp);
      break;
    case graphscore::KeyCode::kDown:
      std::ignore =
          move_selected_notehead(graphscore::NoteheadStepDirection::kDown);
      break;
    case graphscore::KeyCode::kMinus:
      std::ignore =
          step_selected_accidental(graphscore::AccidentalStepDirection::kLower);
      break;
    case graphscore::KeyCode::kEquals:
      std::ignore =
          step_selected_accidental(graphscore::AccidentalStepDirection::kRaise);
      break;
    case graphscore::KeyCode::kR:
      std::ignore = convert_selection_to_rest();
      break;
    case graphscore::KeyCode::kUnknown:
    default:
      break;
  }
  // M5-phase-25: unmodified `2`..`8` add a key-spelled diatonic interval
  // ABOVE the single selected notehead. `1` is not a binding this phase
  // owns, so it -- and any other unmapped code -- remains a no-op.
  if (const auto interval = digit_interval(event.code); interval.has_value()) {
    std::ignore =
        add_selected_interval(graphscore::IntervalDirection::kAbove, *interval);
  }
}

}  // namespace graphscore::writer_app
