// SPDX-License-Identifier: Apache-2.0

#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {
[[nodiscard]] graphscore::KeyEvent plain_key(graphscore::KeyCode code) {
  graphscore::KeyEvent event;
  event.code = code;
  return event;
}

[[nodiscard]] graphscore::KeyEvent shift_key(graphscore::KeyCode code) {
  graphscore::KeyEvent event;
  event.code            = code;
  event.modifiers.shift = true;
  return event;
}

// Voice 1, the only voice this test's fixture writes. Voice::create(1) cannot
// fail (index 1 is always in range), but the value is checked rather than
// dereferenced so the unchecked-optional-access check sees a guarded access.
[[nodiscard]] graphscore::Voice voice_one() {
  const auto voice = graphscore::Voice::create(1);
  if (!voice.has_value()) {
    return graphscore::Voice{};
  }
  return *voice;
}

// A spelled pitch, natural unless an accidental is named; the octaves these
// tests use (4, 5) are always in range, but the value is checked before
// dereferencing.
[[nodiscard]] graphscore::SpelledPitch spelled(
    graphscore::Letter letter, std::int8_t octave,
    graphscore::Accidental accidental) {
  const auto pitch =
      graphscore::SpelledPitch::create(letter, octave, accidental);
  if (!pitch.has_value()) {
    return graphscore::SpelledPitch{};
  }
  return *pitch;
}

// Sets the committed selection to the given notehead items. Returns false
// (leaving the handler untouched) if NoteheadSet::create rejects the items.
[[nodiscard]] bool select_noteheads(
    SelectionToolHandler&                 handler,
    std::vector<graphscore::NoteheadItem> items) {
  const auto set = graphscore::NoteheadSet::create(std::move(items));
  if (!set.has_value()) {
    return false;
  }
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

// The committed selection's own NoteheadSet, or nullptr when there is no
// committed selection or it is not that arm — the free-function counterpart
// of SelectionToolHandler::current_notehead_set().
[[nodiscard]] const graphscore::NoteheadSet* committed_notehead_set(
    const SelectionToolHandler& handler) {
  const auto& committed = handler.drag_state().committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::NoteheadSet>(&*committed);
}

// The committed selection's own ArbitraryRangeSet, or nullptr when there is
// no committed selection or it is not that arm -- the free-function
// counterpart of SelectionToolHandler::current_range_set(), used by the
// tests below to read the committed selection back through the same
// has_value()-checked-before-dereferenced pattern the rest of this file
// uses.
[[nodiscard]] const graphscore::ArbitraryRangeSet* committed_range_set(
    const SelectionToolHandler& handler) {
  const auto& committed = handler.drag_state().committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
}

// Presses and releases the primary button at (x, y) with no intervening move:
// a click, which on_pointer_release resolves through resolve_selection_at
// rather than through the range-drag path.
void click_at(graphscore::WriterShell& shell, double x, double y) {
  const graphscore::PointerEvent press{x, y,
                                       graphscore::PointerButton::kPrimary};
  const graphscore::PointerEvent release{x, y,
                                         graphscore::PointerButton::kPrimary};
  shell.dispatch_test_pointer_event(0, press);
  shell.dispatch_test_pointer_event(2, release);
}

// Presses at (x1, y1), then moves and releases at (x2, y2): a two-point
// drag through the headless pointer seam, matching selection_tool_test's
// own pattern.
void drag_through_shell(graphscore::WriterShell& shell, double x1, double y1,
                        double x2, double y2) {
  const graphscore::PointerEvent press{x1, y1,
                                       graphscore::PointerButton::kPrimary};
  const graphscore::PointerEvent release{x2, y2,
                                         graphscore::PointerButton::kPrimary};
  shell.dispatch_test_pointer_event(0, press);
  shell.dispatch_test_pointer_event(1, release);
  shell.dispatch_test_pointer_event(2, release);
}

// The origin of the "<id>/notehead" GlyphCommand in `layout` — ground truth
// read out of the real layout, never a reproduction of notation_engraving.cpp's
// own placement formulas. Clicking this point selects that notehead.
[[nodiscard]] graphscore::NotationPoint notehead_origin(
    const graphscore::NotationLayout&   layout,
    const graphscore::NotationEntityId& id) {
  const std::string target = id.to_string() + "/notehead";
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<graphscore::GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value == target) {
      return glyph->origin;
    }
  }
  return graphscore::NotationPoint{};
}

// The origin of the "<id>/grace-notehead" GlyphCommand in `layout` (a grace
// notehead uses a distinct glyph role from an ordinary/chord notehead).
[[nodiscard]] graphscore::NotationPoint grace_notehead_origin(
    const graphscore::NotationLayout&   layout,
    const graphscore::NotationEntityId& id) {
  const std::string target = id.to_string() + "/grace-notehead";
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<graphscore::GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value == target) {
      return glyph->origin;
    }
  }
  return graphscore::NotationPoint{};
}

// A headless surface publisher: encodes a hash of the layout's glyph origins
// into a small RGBA surface and publishes it through the shell's real
// set_notation_surface path, so the notehead-move test can observe that a
// mutation refresh re-published a *different* visible surface without a font
// or rendering backend. Moving a notehead changes that notehead's glyph
// origin, and therefore the surface.
[[nodiscard]] graphscore::ShellResult publish_headless_test_surface(
    const graphscore::NotationLayout& layout, graphscore::WriterShell* shell) {
  std::uint32_t hash = 2166136261u;
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<graphscore::GlyphCommand>(&command);
    if (glyph == nullptr) {
      continue;
    }
    const auto mix = [&hash](double value) {
      hash ^= static_cast<std::uint32_t>(value * 1000.0);
      hash *= 16777619u;
    };
    mix(glyph->origin.x);
    mix(glyph->origin.y);
  }
  graphscore::RasterSurface surface;
  surface.width  = 2;
  surface.height = 2;
  surface.rgba.resize(16);
  surface.rgba[0] = static_cast<std::uint8_t>(hash >> 24);
  surface.rgba[1] = static_cast<std::uint8_t>(hash >> 16);
  surface.rgba[2] = static_cast<std::uint8_t>(hash >> 8);
  surface.rgba[3] = static_cast<std::uint8_t>(hash);
  return shell->set_notation_surface(std::move(surface));
}

// Expected highlight rect for a full-measure drag across measure 0 of the
// default fixture (C major, 4/4, staff_space=10.0, any GlyphMetrics).
// The rect spans the rhythmic area: x starts after the leading
// (clef/key/time-sig) area, width equals the rhythmic width, y and height
// from the staff system bounds.  The formula exactly reproduces
// measure_leading_width + position_x for span [measure_start, measure_end).
[[nodiscard]] graphscore::NotationRect expected_full_measure_highlight_rect(
    const graphscore::NotationLayout& layout) {
  const auto&      staff       = layout.systems[0].staves[0];
  const auto&      measure     = layout.systems[0].measures[0];
  constexpr double kStaffSpace = 10.0;
  // measure_leading_width for C major, 4/4, measure 0:
  //   min(measure_width - staff_space*2, staff_space * (6.5 + 1.5*abs(0)
  //       + 0 + 1.5*1))  =  min(measure_width - 20, 80)
  const double leading =
      std::min(measure.bounds.width - kStaffSpace * 2.0, kStaffSpace * 8.0);
  const double rwidth =
      std::max(kStaffSpace, measure.bounds.width - leading - kStaffSpace);
  return graphscore::NotationRect{measure.bounds.x + leading, staff.bounds.y,
                                  rwidth, staff.bounds.height};
}

// The first voice event as a Chord, or nullopt when it is not a Chord.
[[nodiscard]] std::optional<graphscore::Chord> first_chord(
    const graphscore::Project& project, graphscore::NodeId node_id,
    graphscore::TrackId track_id, graphscore::StaveId stave_id) {
  const auto* lane = project.find_node(node_id)->lane(track_id);
  const auto& vc   = lane->stave(stave_id)->voice(voice_one());
  if (vc.events().empty()) {
    return std::nullopt;
  }
  const auto* chord = std::get_if<graphscore::Chord>(&vc.events().front());
  return chord == nullptr ? std::nullopt
                          : std::optional<graphscore::Chord>(*chord);
}

// Captures the full observable state a no-op must leave byte-for-byte
// unchanged -- voice content, committed selection, layout, surface, highlight,
// undo/redo depth, and the audition hook -- runs `act`, then re-checks every
// one of them. Returns an empty string when nothing moved, or a diagnostic
// naming the first thing that did. Shared by the `1`, forbidden-modifier, and
// stale/empty-selection no-op checks so each asserts the same complete
// before/after state rather than a partial proxy.
[[nodiscard]] std::string no_op_violation(SelectionToolHandler&        handler,
                                          graphscore::WriterShell&     shell,
                                          graphscore::NodeId           node_id,
                                          graphscore::TrackId          track_id,
                                          graphscore::StaveId          stave_id,
                                          const char*                  what,
                                          const std::function<void()>& act) {
  const auto voice = [&]() -> graphscore::VoiceContent {
    const auto* lane = handler.project().find_node(node_id)->lane(track_id);
    return lane->stave(stave_id)->voice(voice_one());
  };
  const graphscore::VoiceContent voice_before = voice();
  const auto selection_before   = handler.drag_state().committed_selection();
  const auto layout_before      = handler.layout();
  const auto surface_before     = shell.test_snapshot_notation_surface();
  const auto highlight_before   = shell.test_snapshot_highlight_rects();
  const std::size_t undo_before = handler.test_undo_stack_size();
  const std::size_t redo_before = handler.test_redo_stack_size();
  const std::optional<graphscore::NoteAuditionRequest> audition_before =
      handler.last_audition();

  act();

  if (!(voice() == voice_before)) {
    return std::string(what) + ": voice content changed";
  }
  if (handler.drag_state().committed_selection() != selection_before) {
    return std::string(what) + ": committed selection changed";
  }
  if (!(handler.layout() == layout_before)) {
    return std::string(what) + ": layout changed";
  }
  if (shell.test_snapshot_notation_surface() != surface_before) {
    return std::string(what) + ": surface changed";
  }
  if (shell.test_snapshot_highlight_rects() != highlight_before) {
    return std::string(what) + ": highlight changed";
  }
  if (handler.test_undo_stack_size() != undo_before ||
      handler.test_redo_stack_size() != redo_before) {
    return std::string(what) + ": history depth changed";
  }
  if (!(handler.last_audition() == audition_before)) {
    return std::string(what) + ": audition hook changed";
  }
  return std::string{};
}

FailUndoCommand::FailUndoCommand(std::unique_ptr<graphscore::Command> inner,
                                 int fail_times)
    : inner_(std::move(inner)), fail_times_(fail_times) {}

graphscore::Result FailUndoCommand::execute(
    graphscore::Project& project) noexcept {
  return inner_->execute(project);
}

graphscore::Result FailUndoCommand::undo(
    graphscore::Project& project) noexcept {
  if (fail_times_ > 0) {
    --fail_times_;
    return graphscore::Result(graphscore::ResultCode::kInternalError);
  }
  return inner_->undo(project);
}

graphscore::Result FailUndoCommand::redo(
    graphscore::Project& project) noexcept {
  return inner_->redo(project);
}

}  // namespace graphscore::writer_app
