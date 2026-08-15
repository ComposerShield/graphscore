// SPDX-License-Identifier: Apache-2.0

#include "command_palette.hpp"

#include <string>
#include <utility>
#include <vector>

namespace graphscore::writer_app {

namespace {

// One row per §7 action, in the action table's own section order (7.1, 7.2,
// 7.3, 7.4, 7.5, 7.6), then the chord-less §7.8 accessible controls.
const std::vector<PaletteCommand> kInventory = [] {
  std::vector<PaletteCommand> rows;
  const auto add = [&rows](PaletteCommandId id, std::string name,
                           std::string chord_hint, std::string description) {
    rows.push_back(PaletteCommand{id, std::move(name), std::move(chord_hint),
                                  std::move(description)});
  };

  // 7.1 Unmodified (non-numpad).
  add(PaletteCommandId::kMoveNoteUp, "Move note up", "Up",
      "Move the selected notehead one diatonic step up");
  add(PaletteCommandId::kMoveNoteDown, "Move note down", "Down",
      "Move the selected notehead one diatonic step down");
  add(PaletteCommandId::kAccidentalDown, "Accidental down", "-",
      "Step the selected notehead one rung down the accidental ladder");
  add(PaletteCommandId::kAccidentalUp, "Accidental up", "=",
      "Step the selected notehead one rung up the accidental ladder");
  add(PaletteCommandId::kDeleteNotehead, "Delete notehead", "Backspace/Delete",
      "Delete the selected notehead, selecting the prior onset");
  add(PaletteCommandId::kConvertToRest, "Convert to rest", "R",
      "Convert the selected note or chord to an equal-duration rest");
  for (std::uint8_t i = 2; i <= 8; ++i) {
    add(static_cast<PaletteCommandId>(
            static_cast<std::uint8_t>(PaletteCommandId::kIntervalAbove2) +
            (i - 2)),
        "Add interval " + std::to_string(i) + " above", std::to_string(i),
        "Add a key-spelled diatonic interval " + std::to_string(i) +
            " above the selected notehead");
  }
  for (std::uint8_t i = 2; i <= 8; ++i) {
    add(static_cast<PaletteCommandId>(
            static_cast<std::uint8_t>(PaletteCommandId::kIntervalBelow2) +
            (i - 2)),
        "Add interval " + std::to_string(i) + " below",
        "Shift+" + std::to_string(i),
        "Add a key-spelled diatonic interval " + std::to_string(i) +
            " below the selected notehead");
  }
  add(PaletteCommandId::kToggleTool, "Toggle note entry", "N",
      "Toggle the active tool between note entry and selection");
  add(PaletteCommandId::kPitchA, "Enter pitch A", "A",
      "Commit a natural A at the step-entry caret");
  add(PaletteCommandId::kPitchB, "Enter pitch B", "B",
      "Commit a natural B at the step-entry caret");
  add(PaletteCommandId::kPitchC, "Enter pitch C", "C",
      "Commit a natural C at the step-entry caret");
  add(PaletteCommandId::kPitchD, "Enter pitch D", "D",
      "Commit a natural D at the step-entry caret");
  add(PaletteCommandId::kPitchE, "Enter pitch E", "E",
      "Commit a natural E at the step-entry caret");
  add(PaletteCommandId::kPitchF, "Enter pitch F", "F",
      "Commit a natural F at the step-entry caret");
  add(PaletteCommandId::kPitchG, "Enter pitch G", "G",
      "Commit a natural G at the step-entry caret");

  // 7.2 Shift.
  add(PaletteCommandId::kRangeEdgeEarlier, "Range edge earlier", "Shift+Left",
      "Move the focus edge one quarter note earlier");
  add(PaletteCommandId::kRangeEdgeLater, "Range edge later", "Shift+Right",
      "Move the focus edge one quarter note later");
  add(PaletteCommandId::kRangeStaffScopeUp, "Range staff scope up", "Shift+Up",
      "Extend the staff scope one staff earlier in score order");
  add(PaletteCommandId::kRangeStaffScopeDown, "Range staff scope down",
      "Shift+Down", "Extend the staff scope one staff later in score order");
  add(PaletteCommandId::kRangeSelectToStart, "Select to node start",
      "Shift+Home", "Extend the range selection to the node start");
  add(PaletteCommandId::kRangeSelectToEnd, "Select to node end", "Shift+End",
      "Extend the range selection to the node end");

  // 7.3 Primary.
  add(PaletteCommandId::kStaffStepPrevious, "Staff step previous", "Primary+Up",
      "Move the selection to the prior staff, wrapping");
  add(PaletteCommandId::kStaffStepNext, "Staff step next", "Primary+Down",
      "Move the selection to the next staff, wrapping");
  add(PaletteCommandId::kCut, "Cut", "Primary+X",
      "Cut the selection to the in-memory clipboard");
  add(PaletteCommandId::kCopy, "Copy", "Primary+C",
      "Copy the selection to the in-memory clipboard");
  add(PaletteCommandId::kPaste, "Paste", "Primary+V",
      "Paste the in-memory clipboard at the caret or anchor");
  add(PaletteCommandId::kUndo, "Undo", "Primary+Z", "Undo the last command");
  add(PaletteCommandId::kTogglePalette, "Toggle command palette", "Primary+K",
      "Open or close the command palette");

  // 7.4 Shift+Primary.
  add(PaletteCommandId::kRedo, "Redo", "Shift+Primary+Z",
      "Redo the last undone command");

  // 7.5 Alt.
  add(PaletteCommandId::kVoice1, "Arm voice 1", "Alt+1",
      "Arm voice 1 for step entry");
  add(PaletteCommandId::kVoice2, "Arm voice 2", "Alt+2",
      "Arm voice 2 for step entry");
  add(PaletteCommandId::kVoice3, "Arm voice 3", "Alt+3",
      "Arm voice 3 for step entry");
  add(PaletteCommandId::kVoice4, "Arm voice 4", "Alt+4",
      "Arm voice 4 for step entry");
  add(PaletteCommandId::kOctaveUp, "Octave reference up", "Alt+Up",
      "Raise the step-entry octave reference one octave");
  add(PaletteCommandId::kOctaveDown, "Octave reference down", "Alt+Down",
      "Lower the step-entry octave reference one octave");

  // 7.6 Numpad.
  add(PaletteCommandId::kDurationWhole, "Duration whole", "KP_1",
      "Arm a whole-note duration");
  add(PaletteCommandId::kDurationHalf, "Duration half", "KP_2",
      "Arm a half-note duration");
  add(PaletteCommandId::kDurationQuarter, "Duration quarter", "KP_3",
      "Arm a quarter-note duration");
  add(PaletteCommandId::kDurationEighth, "Duration eighth", "KP_4",
      "Arm an eighth-note duration");
  add(PaletteCommandId::kDurationSixteenth, "Duration sixteenth", "KP_5",
      "Arm a sixteenth-note duration");
  add(PaletteCommandId::kDurationThirtySecond, "Duration thirty-second", "KP_6",
      "Arm a thirty-second-note duration");
  add(PaletteCommandId::kDurationSixtyFourth, "Duration sixty-fourth", "KP_7",
      "Arm a sixty-fourth-note duration");
  add(PaletteCommandId::kStepEntryRest, "Step-entry rest", "KP_0",
      "Commit a rest of the armed duration at the step-entry caret");
  add(PaletteCommandId::kCycleDots, "Cycle dots", "KP_DECIMAL",
      "Cycle the armed dot count 0, 1, 2, 0");

  // 7.8 Accessible range controls (chord-less).
  add(PaletteCommandId::kAccessibleRangeStart, "Range: select to start", "",
      "Select from the range start (accessible control)");
  add(PaletteCommandId::kAccessibleRangeEnd, "Range: select to end", "",
      "Select to the range end (accessible control)");
  add(PaletteCommandId::kAccessibleRangeStaffScope, "Range: staff scope", "",
      "Set the range staff scope (accessible control)");

  // M5-phase-28b structural measure editing: chord-less, palette-only rows.
  add(PaletteCommandId::kInsertMeasureBefore, "Insert measure before", "",
      "Insert a new measure before the selected measure");
  add(PaletteCommandId::kAppendMeasure, "Append measure", "",
      "Append a new measure at the end of the node");
  add(PaletteCommandId::kDeleteMeasure, "Delete measure", "",
      "Delete the selected measure");

  return rows;
}();

}  // namespace

const std::vector<PaletteCommand>& palette_inventory() {
  return kInventory;
}

}  // namespace graphscore::writer_app
