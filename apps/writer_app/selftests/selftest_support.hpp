// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../selection_tool_handler.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace graphscore::writer_app {

[[nodiscard]] graphscore::KeyEvent plain_key(graphscore::KeyCode code);

[[nodiscard]] graphscore::KeyEvent shift_key(graphscore::KeyCode code);

// Voice 1, the only voice the fixtures write. Voice::create(1) cannot fail
// (index 1 is always in range), but the value is checked rather than
// dereferenced so the unchecked-optional-access check sees a guarded access.
[[nodiscard]] graphscore::Voice voice_one();

// A spelled pitch, natural unless an accidental is named; the octaves the
// tests use (4, 5) are always in range, but the value is checked before
// dereferencing.
[[nodiscard]] graphscore::SpelledPitch spelled(
    graphscore::Letter letter, std::int8_t octave,
    graphscore::Accidental accidental = graphscore::Accidental::kNatural);

// Sets the committed selection to the given notehead items. Returns false
// (leaving the handler untouched) if NoteheadSet::create rejects the items.
[[nodiscard]] bool select_noteheads(
    SelectionToolHandler& handler, std::vector<graphscore::NoteheadItem> items);

// The committed selection's own NoteheadSet, or nullptr when there is no
// committed selection or it is not that arm — the free-function counterpart
// of SelectionToolHandler::current_notehead_set().
[[nodiscard]] const graphscore::NoteheadSet* committed_notehead_set(
    const SelectionToolHandler& handler);

// The committed selection's own ArbitraryRangeSet, or nullptr when there is
// no committed selection or it is not that arm — the free-function
// counterpart of SelectionToolHandler::current_range_set().
[[nodiscard]] const graphscore::ArbitraryRangeSet* committed_range_set(
    const SelectionToolHandler& handler);

// Presses and releases the primary button at (x, y) with no intervening move:
// a click, which on_pointer_release resolves through resolve_selection_at
// rather than through the range-drag path.
void click_at(graphscore::WriterShell& shell, double x, double y);

void drag_through_shell(graphscore::WriterShell& shell, double x1, double y1,
                        double x2, double y2);

// The origin of the "<id>/notehead" GlyphCommand in `layout` — ground truth
// read out of the real layout, never a reproduction of notation_engraving.cpp's
// own placement formulas. Clicking this point selects that notehead.
[[nodiscard]] graphscore::NotationPoint notehead_origin(
    const graphscore::NotationLayout&   layout,
    const graphscore::NotationEntityId& id);

// The origin of the "<id>/grace-notehead" GlyphCommand in `layout` (a grace
// notehead uses a distinct glyph role from an ordinary/chord notehead).
[[nodiscard]] graphscore::NotationPoint grace_notehead_origin(
    const graphscore::NotationLayout&   layout,
    const graphscore::NotationEntityId& id);

// A headless surface publisher: encodes a hash of the layout's glyph origins
// into a small RGBA surface and publishes it through the shell's real
// set_notation_surface path, so a mutation refresh can be observed as a
// re-published *different* visible surface without a font or rendering
// backend.
[[nodiscard]] graphscore::ShellResult publish_headless_test_surface(
    const graphscore::NotationLayout& layout, graphscore::WriterShell* shell);

// Expected highlight rect for a full-measure drag across measure 0 of the
// default fixture (C major, 4/4, staff_space=10.0, any GlyphMetrics).
[[nodiscard]] graphscore::NotationRect expected_full_measure_highlight_rect(
    const graphscore::NotationLayout& layout);

// The first voice event as a Chord, or nullopt when it is not a Chord.
[[nodiscard]] std::optional<graphscore::Chord> first_chord(
    const graphscore::Project& project, graphscore::NodeId node_id,
    graphscore::TrackId track_id, graphscore::StaveId stave_id);

// Captures the full observable state a no-op must leave byte-for-byte
// unchanged, runs `act`, then re-checks every one of them. Returns an empty
// string when nothing moved, or a diagnostic naming the first thing that did.
[[nodiscard]] std::string no_op_violation(SelectionToolHandler&        handler,
                                          graphscore::WriterShell&     shell,
                                          graphscore::NodeId           node_id,
                                          graphscore::TrackId          track_id,
                                          graphscore::StaveId          stave_id,
                                          const char*                  what,
                                          const std::function<void()>& act);

// A deterministic test wrapper that delegates execute/redo to a real
// MoveNoteheadCommand but fails undo() exactly `fail_times` times before
// delegating, so a rollback failure can be injected without relying on an
// actual allocation failure. `fail_times <= 0` means "never fail".
class FailUndoCommand final : public graphscore::Command {
 public:
  FailUndoCommand(std::unique_ptr<graphscore::Command> inner, int fail_times);

  graphscore::Result execute(graphscore::Project& project) noexcept override;

  graphscore::Result undo(graphscore::Project& project) noexcept override;

  graphscore::Result redo(graphscore::Project& project) noexcept override;

 private:
  std::unique_ptr<graphscore::Command> inner_;
  int                                  fail_times_;
};

}  // namespace graphscore::writer_app
