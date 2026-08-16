// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../key_bindings.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {

// ---- M5-phase-27: clipboard handoff (copy/cut/paste) + undo/redo -----------

namespace {

struct ClipboardFixture {
  graphscore::Project        project;
  graphscore::NodeId         node_id;
  graphscore::TrackId        track_id;
  graphscore::StaveId        stave_id;
  graphscore::NotationLayout layout;
};

// A single-staff, two-measure node: voice 1 carries a whole C4 in measure 0
// and a whole D4 in measure 1, normalized.
[[nodiscard]] std::optional<ClipboardFixture> build_clipboard_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Clipboard"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto whole =
      graphscore::Duration::create(graphscore::NoteValue::kWhole, 0);
  const auto voice = graphscore::Voice::create(1);
  if (!whole.has_value() || !voice.has_value()) {
    return std::nullopt;
  }
  const auto c4 = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto d4 = graphscore::SpelledPitch::create(graphscore::Letter::kD, 4);
  if (!c4.has_value() || !d4.has_value()) {
    return std::nullopt;
  }
  graphscore::VoiceContent& vc = lane->stave(stave_id)->voice(*voice);
  if (!vc.append(graphscore::make_note(*c4, *whole)).ok() ||
      !vc.append(graphscore::make_note(*d4, *whole)).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }
  return ClipboardFixture{std::move(project), node_id, track_id, stave_id,
                          std::move(*layout_result.layout)};
}

[[nodiscard]] graphscore::KeyEvent primary_logical(graphscore::LogicalKey key,
                                                   PrimaryModifier primary) {
  graphscore::KeyEvent event;
  event.logical = key;
  if (primary == PrimaryModifier::kMeta) {
    event.modifiers.meta = true;
  } else {
    event.modifiers.control = true;
  }
  return event;
}

[[nodiscard]] bool select_full_measure(SelectionToolHandler&   handler,
                                       const ClipboardFixture& fixture,
                                       std::size_t             measure_index) {
  const auto set =
      graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
          fixture.node_id, fixture.track_id, fixture.stave_id, measure_index}});
  if (!set.has_value()) {
    return false;
  }
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

[[nodiscard]] const graphscore::VoiceContent& voice_of(
    const SelectionToolHandler& handler, const ClipboardFixture& fixture) {
  const graphscore::Node* node = handler.project().find_node(fixture.node_id);
  return node->lane(fixture.track_id)
      ->stave(fixture.stave_id)
      ->voice(voice_one());
}

// The sounding pitch of the voice's first event, or nullopt for a rest.
[[nodiscard]] std::optional<graphscore::SpelledPitch> first_pitch(
    const SelectionToolHandler& handler, const ClipboardFixture& fixture) {
  const auto& events = voice_of(handler, fixture).events();
  if (events.empty()) {
    return std::nullopt;
  }
  if (const auto* note = std::get_if<graphscore::Note>(&events.front())) {
    return note->pitch;
  }
  return std::nullopt;
}

// The sounding pitch of the event at `index` (first notehead for a Chord),
// or nullopt for a rest or an out-of-range index.
[[nodiscard]] std::optional<graphscore::SpelledPitch> pitch_at(
    const SelectionToolHandler& handler, const ClipboardFixture& fixture,
    std::size_t index) {
  const auto& events = voice_of(handler, fixture).events();
  if (index >= events.size()) {
    return std::nullopt;
  }
  const auto& event = events[index];
  if (const auto* note = std::get_if<graphscore::Note>(&event)) {
    return note->pitch;
  }
  if (const auto* chord = std::get_if<graphscore::Chord>(&event)) {
    return chord->notes.empty() ? std::nullopt
                                : std::optional<graphscore::SpelledPitch>(
                                      chord->notes.front().pitch);
  }
  return std::nullopt;
}

[[nodiscard]] bool select_range(SelectionToolHandler&   handler,
                                const ClipboardFixture& fixture,
                                graphscore::Rational    start,
                                graphscore::Rational    end) {
  const auto set =
      graphscore::ArbitraryRangeSet::create({graphscore::ArbitraryRangeItem{
          fixture.node_id, fixture.track_id, fixture.stave_id, voice_one(),
          graphscore::MusicalSpan{start, end}}});
  if (!set.has_value()) {
    return false;
  }
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

// A single-staff, one-measure (4/4) node whose voice 1 carries one triplet
// eighth note at onset 0 (span [0, 1/12)) and is normalized to the node end.
struct TupletFixture {
  graphscore::Project        project;
  graphscore::NodeId         node_id;
  graphscore::TrackId        track_id;
  graphscore::StaveId        stave_id;
  graphscore::NotationLayout layout;
};

[[nodiscard]] std::optional<TupletFixture> build_tuplet_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Tuplet"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      1, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto tuplet_ratio = graphscore::TupletRatio::create(3, 2);
  const auto duration     = graphscore::Duration::create(
      graphscore::NoteValue::kEighth, 0, tuplet_ratio);
  const auto voice = graphscore::Voice::create(1);
  if (!duration.has_value() || !voice.has_value()) {
    return std::nullopt;
  }
  // Three triplet eighths tile exactly [0, 1/4), so the normalize fill after
  // them is dyadic and succeeds.
  const std::array<graphscore::Letter, 3> kLetters{
      graphscore::Letter::kC, graphscore::Letter::kD, graphscore::Letter::kE};
  graphscore::VoiceContent& vc = lane->stave(stave_id)->voice(*voice);
  for (const graphscore::Letter letter : kLetters) {
    const auto pitch = graphscore::SpelledPitch::create(letter, 4);
    if (!pitch.has_value() ||
        !vc.append(graphscore::make_note(*pitch, *duration)).ok()) {
      return std::nullopt;
    }
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }
  return TupletFixture{std::move(project), node_id, track_id, stave_id,
                       std::move(*layout_result.layout)};
}

// The voice-1 content of a TupletFixture's project.
[[nodiscard]] const graphscore::VoiceContent& tuplet_voice(
    const SelectionToolHandler& handler, const TupletFixture& fixture) {
  return handler.project()
      .find_node(fixture.node_id)
      ->lane(fixture.track_id)
      ->stave(fixture.stave_id)
      ->voice(voice_one());
}

}  // namespace

int clipboard_test() {
  const SelfTestMetrics metrics;

  // --- test 1: copy a full measure populates the clipboard; copy of an
  //     ineligible arm is a no-op with a diagnostic that preserves it. -----
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (1)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto voice_before = voice_of(handler, *fixture);
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment() ||
        handler.clipboard()->span_length() != graphscore::Rational(1)) {
      std::fprintf(stderr,
                   "clipboard-test: copy did not populate clipboard (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!(voice_of(handler, *fixture) == voice_before)) {
      std::fprintf(stderr, "clipboard-test: copy mutated the project (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Ineligible arm (a notehead selection): no-op, diagnostic, clipboard
    // preserved.
    const auto note_id =
        graphscore::event_id(voice_of(handler, *fixture).events().front());
    if (!select_noteheads(
            handler, {graphscore::NoteheadItem{
                         fixture->node_id, fixture->track_id, fixture->stave_id,
                         voice_one(), note_id}})) {
      std::fprintf(stderr, "clipboard-test: notehead selection failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto clipboard_before   = *handler.clipboard();
    const auto diagnostics_before = handler.diagnostics().size();
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment() ||
        !(handler.clipboard()->operator==(clipboard_before))) {
      std::fprintf(stderr,
                   "clipboard-test: ineligible copy replaced clipboard (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.diagnostics().size() != diagnostics_before + 1) {
      std::fprintf(
          stderr, "clipboard-test: ineligible copy posted no diagnostic (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 2: cut a full measure replaces its range with rests, places the
  //     clipboard, and selects a caret at the cut start. --------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (2)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment()) {
      std::fprintf(stderr,
                   "clipboard-test: cut did not populate clipboard (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Measure 0's C4 became a rest.
    if (first_pitch(handler, *fixture).has_value()) {
      std::fprintf(stderr, "clipboard-test: cut left a sounding pitch (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The committed selection is a caret at the cut start (position 0).
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr, "clipboard-test: cut left no selection (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto* caret = std::get_if<graphscore::InsertionCaretSet>(&*committed);
    if (caret == nullptr || caret->items().size() != 1u ||
        caret->items().front().position != graphscore::Rational(0)) {
      std::fprintf(stderr, "clipboard-test: cut caret wrong (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: paste at a caret restores the fragment; paste with an empty
  //     clipboard is a no-op with a diagnostic. -----------------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (3)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    // Paste with an empty clipboard is a no-op + diagnostic.
    const auto voice_before = voice_of(handler, *fixture);
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (!(voice_of(handler, *fixture) == voice_before) ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr, "clipboard-test: empty paste was not a no-op (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Copy measure 0, then paste it at the measure 1 start.
    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    const auto caret =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::Rational(1)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "clipboard-test: caret build failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    // Measure 1's D4 was replaced by the pasted C4.
    if (first_pitch(handler, *fixture) != spelled(graphscore::Letter::kC, 4)) {
      std::fprintf(stderr, "clipboard-test: paste did not replace (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The clipboard survives paste.
    if (!handler.clipboard_has_fragment()) {
      std::fprintf(stderr,
                   "clipboard-test: paste consumed the clipboard (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: undo and redo via Primary+Z / Shift+Primary+Z. --------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (4)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    const auto after_cut = voice_of(handler, *fixture);
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kZ, kPlatformPrimaryModifier));
    if (first_pitch(handler, *fixture) != spelled(graphscore::Letter::kC, 4)) {
      std::fprintf(stderr, "clipboard-test: undo did not restore (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Shift+Primary+Z redoes the cut.
    graphscore::KeyEvent redo =
        primary_logical(graphscore::LogicalKey::kZ, kPlatformPrimaryModifier);
    redo.modifiers.shift = true;
    shell.dispatch_test_key_event(redo);
    if (!(voice_of(handler, *fixture) == after_cut)) {
      std::fprintf(stderr, "clipboard-test: redo did not re-apply (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: copy an arbitrary range is a pure extraction — the project
  //     is unchanged and the clipboard holds the clipped span. --------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (5)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_range(handler, *fixture, graphscore::Rational(0),
                      graphscore::Rational(1) / graphscore::Rational(2))) {
      std::fprintf(stderr, "clipboard-test: range select failed (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto voice_before = voice_of(handler, *fixture);
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment() ||
        handler.clipboard()->span_length() !=
            *graphscore::Rational::create(1, 2)) {
      std::fprintf(stderr, "clipboard-test: range copy wrong span (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!(voice_of(handler, *fixture) == voice_before)) {
      std::fprintf(stderr, "clipboard-test: range copy mutated project (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 6: cut an arbitrary range clears it, places the clipboard, and
  //     selects a caret at span.start with the range item's own voice. ------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (6)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_range(handler, *fixture, graphscore::Rational(0),
                      graphscore::Rational(1) / graphscore::Rational(2))) {
      std::fprintf(stderr, "clipboard-test: range select failed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment() ||
        handler.clipboard()->span_length() !=
            *graphscore::Rational::create(1, 2)) {
      std::fprintf(stderr, "clipboard-test: range cut did not populate (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The straddling C4 whole note's in-range portion became rests.
    if (first_pitch(handler, *fixture).has_value()) {
      std::fprintf(stderr, "clipboard-test: range cut left a pitch (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr, "clipboard-test: range cut left no selection (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto* caret = std::get_if<graphscore::InsertionCaretSet>(&*committed);
    if (caret == nullptr || caret->items().size() != 1u ||
        caret->items().front().position != graphscore::Rational(0) ||
        caret->items().front().voice != voice_one()) {
      std::fprintf(stderr, "clipboard-test: range cut caret wrong (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: a FullMeasure cut derives the caret's voice from the armed
  //     palette voice (FullMeasureItems carry no voice). --------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (7)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    const auto voice2 = graphscore::Voice::create(2);
    if (!voice2.has_value()) {
      std::fprintf(stderr, "clipboard-test: voice build failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.step_entry_arm_voice(*voice2);
    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    const auto& committed = handler.drag_state().committed_selection();
    const auto* caret = std::get_if<graphscore::InsertionCaretSet>(&*committed);
    if (caret == nullptr || caret->items().size() != 1u ||
        caret->items().front().voice != *voice2) {
      std::fprintf(stderr, "clipboard-test: armed-voice caret wrong (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 8: cut with every ineligible arm is a no-op with a diagnostic
  //     that preserves the project, clipboard, and selection. ---------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (8)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    const auto note_id =
        graphscore::event_id(voice_of(handler, *fixture).events().front());
    if (!select_noteheads(
            handler, {graphscore::NoteheadItem{
                         fixture->node_id, fixture->track_id, fixture->stave_id,
                         voice_one(), note_id}})) {
      std::fprintf(stderr, "clipboard-test: notehead select failed (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto voice_before       = voice_of(handler, *fixture);
    const auto selection_before   = handler.drag_state().committed_selection();
    const auto diagnostics_before = handler.diagnostics().size();
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    if (handler.clipboard_has_fragment() ||
        !(voice_of(handler, *fixture) == voice_before) ||
        handler.drag_state().committed_selection() != selection_before ||
        handler.diagnostics().size() != diagnostics_before + 1) {
      std::fprintf(stderr, "clipboard-test: ineligible cut not a no-op (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 9: cutting an arbitrary range that straddles a tuplet is an
  //     atomic rejection (project, clipboard, selection all preserved). -----
  {
    auto fixture = build_tuplet_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: tuplet fixture failed (9)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    const auto range =
        graphscore::ArbitraryRangeSet::create({graphscore::ArbitraryRangeItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::MusicalSpan{
                graphscore::Rational(0),
                graphscore::Rational(1) / graphscore::Rational(24)}}});
    if (!range.has_value()) {
      std::fprintf(stderr, "clipboard-test: range build failed (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*range});
    const auto voice_before     = tuplet_voice(handler, *fixture);
    const auto selection_before = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    if (handler.clipboard_has_fragment() ||
        !(tuplet_voice(handler, *fixture) == voice_before) ||
        handler.drag_state().committed_selection() != selection_before ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "clipboard-test: straddling-tuplet cut not atomic (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 10: undo/redo of a cut never touches the clipboard — the
  //     fragment survives the undo of the cut that produced it. -------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (10)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment()) {
      std::fprintf(stderr, "clipboard-test: cut failed (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto clipboard_before = *handler.clipboard();
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kZ, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment() ||
        !(handler.clipboard()->operator==(clipboard_before))) {
      std::fprintf(stderr, "clipboard-test: undo cleared clipboard (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 11: paste anchors — notehead, range, and full-measure selections
  //     each anchor the paste at their addressed position. ------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (11)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    // Copy measure 0 (a C4 whole note, span 1).
    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));

    // Notehead anchor: the D4 notehead at onset 1.
    const auto d4_id =
        graphscore::event_id(voice_of(handler, *fixture).events()[1]);
    if (!select_noteheads(
            handler, {graphscore::NoteheadItem{
                         fixture->node_id, fixture->track_id, fixture->stave_id,
                         voice_one(), d4_id}})) {
      std::fprintf(stderr, "clipboard-test: notehead select failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (pitch_at(handler, *fixture, 1) != spelled(graphscore::Letter::kC, 4)) {
      std::fprintf(stderr, "clipboard-test: notehead anchor wrong (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Range anchor: span [0, 1) → first item's span.start == 0 (re-pastes the
    // same measure 0 content back onto itself; clipboard survives).
    if (!select_range(handler, *fixture, graphscore::Rational(0),
                      graphscore::Rational(1))) {
      std::fprintf(stderr, "clipboard-test: range select failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (!handler.clipboard_has_fragment()) {
      std::fprintf(stderr,
                   "clipboard-test: range paste consumed clipboard "
                   "(11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 12: paste rejections — a multi-item notehead set, a multi-node
  //     range, a NodeSet, no selection, and an out-of-range anchor are all
  //     no-ops with a diagnostic and the clipboard untouched. ---------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (12)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    // Populate the clipboard first.
    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    const auto clipboard_before = *handler.clipboard();
    const auto voice_before     = voice_of(handler, *fixture);

    // Multi-item notehead set: ambiguous, rejected.
    const auto c4_id =
        graphscore::event_id(voice_of(handler, *fixture).events()[0]);
    const auto d4_id =
        graphscore::event_id(voice_of(handler, *fixture).events()[1]);
    if (!select_noteheads(
            handler,
            {graphscore::NoteheadItem{fixture->node_id, fixture->track_id,
                                      fixture->stave_id, voice_one(), c4_id},
             graphscore::NoteheadItem{fixture->node_id, fixture->track_id,
                                      fixture->stave_id, voice_one(),
                                      d4_id}})) {
      std::fprintf(stderr, "clipboard-test: multi-select failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto diagnostics_before = handler.diagnostics().size();
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (handler.diagnostics().size() != diagnostics_before + 1 ||
        !(voice_of(handler, *fixture) == voice_before)) {
      std::fprintf(stderr,
                   "clipboard-test: multi-item paste not rejected "
                   "(12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // NodeSet arm: no musical-time position, rejected.
    const auto node_set =
        graphscore::NodeSet::create({graphscore::NodeItem{fixture->node_id}});
    if (!node_set.has_value()) {
      std::fprintf(stderr, "clipboard-test: node-set failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*node_set});
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (!(voice_of(handler, *fixture) == voice_before) ||
        !(handler.clipboard()->operator==(clipboard_before))) {
      std::fprintf(stderr,
                   "clipboard-test: node-set paste not rejected (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // No selection: rejected.
    handler.set_committed_selection(std::nullopt);
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (!(voice_of(handler, *fixture) == voice_before) ||
        !(handler.clipboard()->operator==(clipboard_before))) {
      std::fprintf(stderr,
                   "clipboard-test: no-selection paste not rejected (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Out-of-range anchor: a caret at node end pastes past node_end and is
    // rejected by PasteFragmentCommand.
    const auto caret =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::Rational(2)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "clipboard-test: caret build failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (!(voice_of(handler, *fixture) == voice_before) ||
        !(handler.clipboard()->operator==(clipboard_before)) ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "clipboard-test: out-of-range paste not rejected (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 13: cutting a stale full-measure selection (out-of-range measure
  //     index) is rejected atomically — project, clipboard, selection, and
  //     history are all preserved. ------------------------------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (13)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, *fixture, 2)) {
      std::fprintf(stderr, "clipboard-test: selection failed (13)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto voice_before       = voice_of(handler, *fixture);
    const auto selection_before   = handler.drag_state().committed_selection();
    const auto diagnostics_before = handler.diagnostics().size();
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kX, kPlatformPrimaryModifier));
    if (handler.clipboard_has_fragment() ||
        !(voice_of(handler, *fixture) == voice_before) ||
        handler.drag_state().committed_selection() != selection_before ||
        handler.diagnostics().size() != diagnostics_before + 1 ||
        handler.test_undo_stack_size() != 0) {
      std::fprintf(stderr,
                   "clipboard-test: stale-measure cut was not atomic (13)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 14: pasting from a stale full-measure anchor (out-of-range
  //     measure index) is rejected with the clipboard preserved. ------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (14)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (14)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    const auto clipboard_before = *handler.clipboard();
    const auto voice_before     = voice_of(handler, *fixture);
    if (!select_full_measure(handler, *fixture, 2)) {
      std::fprintf(stderr, "clipboard-test: stale select failed (14)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (!(handler.clipboard()->operator==(clipboard_before)) ||
        !(voice_of(handler, *fixture) == voice_before) ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "clipboard-test: stale-measure paste not rejected (14)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 15: paste-anchor rejections — mixed span, mixed measure index,
  //     multi-node, and a missing stave are all no-ops with the clipboard
  //     preserved (§10.2). ---------------------------------------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (15)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    // Populate the clipboard once.
    if (!select_full_measure(handler, *fixture, 0)) {
      std::fprintf(stderr, "clipboard-test: selection failed (15)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    const auto clipboard_before = *handler.clipboard();
    const auto voice_before     = voice_of(handler, *fixture);

    // Mixed span: two range items on one node with different spans.
    {
      const auto range = graphscore::ArbitraryRangeSet::create(
          {graphscore::ArbitraryRangeItem{
               fixture->node_id, fixture->track_id, fixture->stave_id,
               voice_one(),
               graphscore::MusicalSpan{graphscore::Rational(0),
                                       graphscore::Rational(1)}},
           graphscore::ArbitraryRangeItem{
               fixture->node_id, fixture->track_id, fixture->stave_id,
               voice_one(),
               graphscore::MusicalSpan{graphscore::Rational(0),
                                       graphscore::Rational(2)}}});
      if (!range.has_value()) {
        std::fprintf(stderr, "clipboard-test: range build failed (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*range});
      shell.dispatch_test_key_event(primary_logical(graphscore::LogicalKey::kV,
                                                    kPlatformPrimaryModifier));
      if (!(handler.clipboard()->operator==(clipboard_before)) ||
          !(voice_of(handler, *fixture) == voice_before)) {
        std::fprintf(stderr,
                     "clipboard-test: mixed-span paste not rejected (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Mixed measure index: two full-measure items on one node.
    {
      const auto measure_set = graphscore::FullMeasureSet::create(
          {graphscore::FullMeasureItem{fixture->node_id, fixture->track_id,
                                       fixture->stave_id, 0},
           graphscore::FullMeasureItem{fixture->node_id, fixture->track_id,
                                       fixture->stave_id, 1}});
      if (!measure_set.has_value()) {
        std::fprintf(stderr, "clipboard-test: measure build failed (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*measure_set});
      shell.dispatch_test_key_event(primary_logical(graphscore::LogicalKey::kV,
                                                    kPlatformPrimaryModifier));
      if (!(handler.clipboard()->operator==(clipboard_before)) ||
          !(voice_of(handler, *fixture) == voice_before)) {
        std::fprintf(stderr,
                     "clipboard-test: mixed-measure paste not rejected (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Multi-node: two range items on different nodes (the second node id
    // need not exist in the project — the single-node check rejects first).
    {
      const auto range = graphscore::ArbitraryRangeSet::create(
          {graphscore::ArbitraryRangeItem{
               fixture->node_id, fixture->track_id, fixture->stave_id,
               voice_one(),
               graphscore::MusicalSpan{graphscore::Rational(0),
                                       graphscore::Rational(1)}},
           graphscore::ArbitraryRangeItem{
               graphscore::NodeId::generate(), fixture->track_id,
               fixture->stave_id, voice_one(),
               graphscore::MusicalSpan{graphscore::Rational(0),
                                       graphscore::Rational(1)}}});
      if (!range.has_value()) {
        std::fprintf(stderr, "clipboard-test: range build failed (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*range});
      shell.dispatch_test_key_event(primary_logical(graphscore::LogicalKey::kV,
                                                    kPlatformPrimaryModifier));
      if (!(handler.clipboard()->operator==(clipboard_before)) ||
          !(voice_of(handler, *fixture) == voice_before)) {
        std::fprintf(stderr,
                     "clipboard-test: multi-node paste not rejected (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Missing stave: a caret naming a stave the node does not carry.
    {
      const auto caret =
          graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
              fixture->node_id, fixture->track_id,
              graphscore::StaveId::generate(), voice_one(),
              graphscore::Rational(0)}});
      if (!caret.has_value()) {
        std::fprintf(stderr, "clipboard-test: caret build failed (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*caret});
      shell.dispatch_test_key_event(primary_logical(graphscore::LogicalKey::kV,
                                                    kPlatformPrimaryModifier));
      if (!(handler.clipboard()->operator==(clipboard_before)) ||
          !(voice_of(handler, *fixture) == voice_before)) {
        std::fprintf(stderr,
                     "clipboard-test: missing-stave paste not rejected (15)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 16: a paste anchor on an archived track is rejected with the
  //     clipboard preserved. -------------------------------------------------
  {
    // A second track is added and archived before the handler is built, so
    // the clipboard can be populated from the still-active first track while
    // the anchor names the archived track.
    graphscore::Project project{graphscore::ProjectId::generate(), "Archived"};
    const auto          midi_channel = graphscore::MidiChannel::create(0);
    if (!midi_channel.has_value()) {
      std::fprintf(stderr, "clipboard-test: channel failed (16)\n");
      return 1;
    }
    const auto track_added = project.add_track(
        "Track",
        graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
        *midi_channel);
    const auto archived_track = project.add_track(
        "Archived",
        graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
        *midi_channel);
    if (!track_added.has_value() || !archived_track.has_value()) {
      std::fprintf(stderr, "clipboard-test: track add failed (16)\n");
      return 1;
    }
    const graphscore::TrackId track_id   = *track_added;
    const graphscore::TrackId arch_track = *archived_track;
    const graphscore::StaveId arch_stave =
        project.active_tracks()[1].layout().staves()[0].id;
    if (!project.archive_track(arch_track).ok()) {
      std::fprintf(stderr, "clipboard-test: archive failed (16)\n");
      return 1;
    }

    const graphscore::NodeId  node_id = project.add_node("Node");
    auto*                     lane = project.find_node(node_id)->lane(track_id);
    const graphscore::StaveId stave_id =
        project.active_tracks()[0].layout().staves()[0].id;
    lane->ensure_stave(stave_id);
    std::vector<graphscore::StaveDefinition> stave_defs;
    stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
    const auto time_sig = graphscore::TimeSignature::create(4, 4);
    if (!time_sig.has_value()) {
      std::fprintf(stderr, "clipboard-test: time sig failed (16)\n");
      return 1;
    }
    std::vector<graphscore::Measure> measures(
        2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
    auto timeline =
        graphscore::NodeTimeline::create(std::move(measures), stave_defs);
    if (!timeline.has_value()) {
      std::fprintf(stderr, "clipboard-test: timeline failed (16)\n");
      return 1;
    }
    project.find_node(node_id)->set_timeline(std::move(*timeline));
    const auto whole =
        graphscore::Duration::create(graphscore::NoteValue::kWhole, 0);
    const auto c4 = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
    const auto voice = graphscore::Voice::create(1);
    if (!whole.has_value() || !c4.has_value() || !voice.has_value()) {
      std::fprintf(stderr, "clipboard-test: value build failed (16)\n");
      return 1;
    }
    graphscore::VoiceContent& vc = lane->stave(stave_id)->voice(*voice);
    if (!vc.append(graphscore::make_note(*c4, *whole)).ok() ||
        !vc.normalize(project.find_node(node_id)->timeline()->node_end())
             .ok()) {
      std::fprintf(stderr, "clipboard-test: voice build failed (16)\n");
      return 1;
    }
    graphscore::NotationLayoutResult layout_result =
        graphscore::layout_notation(project, node_id, metrics);
    if (!layout_result || !layout_result.layout.has_value()) {
      std::fprintf(stderr, "clipboard-test: layout failed (16)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(project),
                                    std::move(*layout_result.layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);

    const auto measure_set = graphscore::FullMeasureSet::create(
        {graphscore::FullMeasureItem{node_id, track_id, stave_id, 0}});
    if (!measure_set.has_value()) {
      std::fprintf(stderr, "clipboard-test: measure select failed (16)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*measure_set});
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kC, kPlatformPrimaryModifier));
    const auto clipboard_before = *handler.clipboard();

    const auto caret = graphscore::InsertionCaretSet::create(
        {graphscore::InsertionCaretItem{node_id, arch_track, arch_stave,
                                        voice_one(), graphscore::Rational(0)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "clipboard-test: caret build failed (16)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    shell.dispatch_test_key_event(
        primary_logical(graphscore::LogicalKey::kV, kPlatformPrimaryModifier));
    if (!(handler.clipboard()->operator==(clipboard_before)) ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "clipboard-test: archived-track paste not rejected (16)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 17: every item in a multi-item range/full-measure anchor must
  //     be live. A live first item cannot conceal a later missing track or
  //     stave, even when node/span/measure are otherwise common. ------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (17)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    if (!select_full_measure(handler, *fixture, 0) ||
        !handler.copy_selection()) {
      std::fprintf(stderr, "clipboard-test: copy failed (17)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto clipboard_before = *handler.clipboard();
    const auto voice_before     = voice_of(handler, *fixture);

    const auto range = graphscore::ArbitraryRangeSet::create(
        {graphscore::ArbitraryRangeItem{
             fixture->node_id, fixture->track_id, fixture->stave_id,
             voice_one(),
             graphscore::MusicalSpan{graphscore::Rational(0),
                                     graphscore::Rational(1)}},
         graphscore::ArbitraryRangeItem{
             fixture->node_id, graphscore::TrackId::generate(),
             fixture->stave_id, voice_one(),
             graphscore::MusicalSpan{graphscore::Rational(0),
                                     graphscore::Rational(1)}}});
    if (!range.has_value()) {
      std::fprintf(stderr, "clipboard-test: range build failed (17)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*range});
    if (handler.paste_clipboard() ||
        !(handler.clipboard()->operator==(clipboard_before)) ||
        !(voice_of(handler, *fixture) == voice_before)) {
      std::fprintf(stderr,
                   "clipboard-test: later missing range item accepted (17)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto measures = graphscore::FullMeasureSet::create(
        {graphscore::FullMeasureItem{fixture->node_id, fixture->track_id,
                                     fixture->stave_id, 0},
         graphscore::FullMeasureItem{fixture->node_id, fixture->track_id,
                                     graphscore::StaveId::generate(), 0}});
    if (!measures.has_value()) {
      std::fprintf(stderr, "clipboard-test: measure build failed (17)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*measures});
    if (handler.paste_clipboard() ||
        !(handler.clipboard()->operator==(clipboard_before)) ||
        !(voice_of(handler, *fixture) == voice_before)) {
      std::fprintf(
          stderr, "clipboard-test: later missing measure item accepted (17)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 18: a contiguous complete-measure range copies with its full
  //     span, pastes through the explicitly selected destination scope as one
  //     undoable command, and remains ineligible for cut. -------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (18)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    const auto&              staff = handler.layout().systems[0].staves[0];
    graphscore::PointerEvent press{
        staff.measure_bounds[0].x + staff.measure_bounds[0].width * 0.5,
        staff.bounds.y + staff.bounds.height * 0.5,
        graphscore::PointerButton::kPrimary, true};
    graphscore::PointerEvent release{
        staff.measure_bounds[1].x + staff.measure_bounds[1].width * 0.5,
        staff.bounds.y + staff.bounds.height * 0.5,
        graphscore::PointerButton::kPrimary, true};
    shell.dispatch_test_pointer_event(0, press);
    shell.dispatch_test_pointer_event(1, release);
    shell.dispatch_test_pointer_event(2, release);
    const auto& committed = handler.drag_state().committed_selection();
    const auto* measures =
        committed.has_value()
            ? std::get_if<graphscore::FullMeasureSet>(&*committed)
            : nullptr;
    if (measures == nullptr || measures->items().size() != 1u ||
        measures->items().front().measure_index != 0u ||
        measures->items().front().measure_count != 2u) {
      std::fprintf(stderr, "clipboard-test: routed range failed (18)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto voice_before = voice_of(handler, *fixture);
    if (!handler.copy_selection() || !handler.clipboard_has_fragment() ||
        handler.clipboard()->span_length() != graphscore::Rational(2)) {
      std::fprintf(stderr, "clipboard-test: range copy failed (18)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto clipboard_before = *handler.clipboard();
    if (!handler.paste_clipboard() || handler.test_undo_stack_size() != 1u ||
        !handler.clipboard_has_fragment() ||
        !handler.clipboard()->operator==(clipboard_before)) {
      std::fprintf(stderr, "clipboard-test: range paste failed (18)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.test_undo() ||
        !(voice_of(handler, *fixture) == voice_before)) {
      std::fprintf(stderr, "clipboard-test: range paste undo failed (18)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto selection_before = handler.drag_state().committed_selection();
    if (handler.cut_selection() ||
        !(voice_of(handler, *fixture) == voice_before) ||
        handler.drag_state().committed_selection() != selection_before ||
        !handler.clipboard()->operator==(clipboard_before)) {
      std::fprintf(stderr, "clipboard-test: range cut not rejected (18)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 19: a destination range whose start is live but whose end is
  //     stale is rejected before anchor derivation, even when the clipboard
  //     span itself would fit from that start. --------------------------------
  {
    auto fixture = build_clipboard_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "clipboard-test: fixture build failed (19)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    if (!select_full_measure(handler, *fixture, 0) ||
        !handler.copy_selection()) {
      std::fprintf(stderr, "clipboard-test: source copy failed (19)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto stale =
        graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, 1, 2}});
    if (!stale.has_value()) {
      std::fprintf(stderr, "clipboard-test: stale range build failed (19)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*stale});
    const auto voice_before       = voice_of(handler, *fixture);
    const auto clipboard_before   = *handler.clipboard();
    const auto selection_before   = handler.drag_state().committed_selection();
    const auto diagnostics_before = handler.diagnostics().size();
    const auto undo_before        = handler.test_undo_stack_size();
    if (handler.paste_clipboard() ||
        !(voice_of(handler, *fixture) == voice_before) ||
        !handler.clipboard()->operator==(clipboard_before) ||
        handler.drag_state().committed_selection() != selection_before ||
        handler.test_undo_stack_size() != undo_before ||
        handler.diagnostics().size() != diagnostics_before + 1u) {
      std::fprintf(stderr, "clipboard-test: stale end not atomic (19)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("clipboard-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
