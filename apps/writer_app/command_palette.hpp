// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace graphscore::writer_app {

// The nonvisual command-palette model
// (docs/plan/05-notation-editor-action-table.md §11): a stable identifier
// for every palette row. One row per §7 action, with Backspace and Delete
// as one row naming one action, plus the chord-less accessible range
// controls of §7.8, plus the chord-less structural measure-editing actions
// of M5-phase-28 (insert a measure before, append a measure, delete a
// measure -- reachable only through the palette, never a key chord). Visual
// presentation and OS-clipboard interchange are out of scope; this models
// routing, inventory, availability, search, and execution only.
enum class PaletteCommandId : std::uint8_t {
  kMoveNoteUp,
  kMoveNoteDown,
  kAccidentalDown,
  kAccidentalUp,
  kDeleteNotehead,
  kConvertToRest,
  kIntervalAbove2,
  kIntervalAbove3,
  kIntervalAbove4,
  kIntervalAbove5,
  kIntervalAbove6,
  kIntervalAbove7,
  kIntervalAbove8,
  kIntervalBelow2,
  kIntervalBelow3,
  kIntervalBelow4,
  kIntervalBelow5,
  kIntervalBelow6,
  kIntervalBelow7,
  kIntervalBelow8,
  kToggleTool,
  kPitchA,
  kPitchB,
  kPitchC,
  kPitchD,
  kPitchE,
  kPitchF,
  kPitchG,
  kRangeEdgeEarlier,
  kRangeEdgeLater,
  kRangeStaffScopeUp,
  kRangeStaffScopeDown,
  kRangeSelectToStart,
  kRangeSelectToEnd,
  kStaffStepPrevious,
  kStaffStepNext,
  kCut,
  kCopy,
  kPaste,
  kUndo,
  kRedo,
  kTogglePalette,
  kVoice1,
  kVoice2,
  kVoice3,
  kVoice4,
  kOctaveUp,
  kOctaveDown,
  kDurationWhole,
  kDurationHalf,
  kDurationQuarter,
  kDurationEighth,
  kDurationSixteenth,
  kDurationThirtySecond,
  kDurationSixtyFourth,
  kStepEntryRest,
  kCycleDots,
  kAccessibleRangeStart,
  kAccessibleRangeEnd,
  kAccessibleRangeStaffScope,
  kInsertMeasureBefore,
  kAppendMeasure,
  kDeleteMeasure,
  kCreateTriplet,
  kTupletRatioEntry,
  kRemoveTuplet,
};

// One palette row: a stable name, its chord hint (empty for the chord-less
// accessible controls), a one-line description, and its live availability
// is derived from the same precondition/fallback logic as its chord (see
// SelectionToolHandler::palette_command_available).
struct PaletteCommand {
  PaletteCommandId id;
  std::string      name;
  std::string      chord_hint;
  std::string      description;
};

// The complete inventory, in a stable order: every §7 binding family, the
// §7.8 accessible range controls, and the M5-phase-28 chord-less structural
// measure-editing actions. This is a pure value: it never reads handler or
// project state.
[[nodiscard]] const std::vector<PaletteCommand>& palette_inventory();

}  // namespace graphscore::writer_app
