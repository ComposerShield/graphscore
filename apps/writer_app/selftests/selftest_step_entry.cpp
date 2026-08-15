// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../key_bindings.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {

// ---- M5-phase-27: keyboard step entry --------------------------------------
//
// Exercises the app-owned step-entry cursor lifecycle (§8), the deterministic
// pitch reference (§8.3), and the commit/advance/rejection rules (§8.5), all
// driven through the same SelectionToolHandler::on_key_press router the
// production key path uses.

namespace {

struct StepEntryFixture {
  graphscore::Project        project;
  graphscore::NodeId         node_id;
  graphscore::TrackId        track_id;
  graphscore::StaveId        stave_id;
  graphscore::NotationLayout layout;
};

// A single-staff node with `measure_count` 4/4 measures; when
// `whole_note_in_measure0` is set, voice 1 carries a C4 whole note in the
// first measure. `clef` selects the stave's default clef (default treble).
[[nodiscard]] std::optional<StepEntryFixture> build_step_entry_fixture(
    const graphscore::GlyphMetrics& metrics, std::size_t measure_count,
    bool             whole_note_in_measure0,
    graphscore::Clef clef = graphscore::Clef::kTreble) {
  graphscore::Project project{graphscore::ProjectId::generate(), "StepEntry"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(clef), *midi_channel);
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
      measure_count,
      graphscore::Measure{*time_sig, graphscore::KeySignature{}});
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
  graphscore::VoiceContent& vc = lane->stave(stave_id)->voice(*voice);
  if (whole_note_in_measure0) {
    const auto pitch =
        graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
    if (!pitch.has_value() ||
        !vc.append(graphscore::make_note(*pitch, *whole)).ok()) {
      return std::nullopt;
    }
    const graphscore::Rational node_end =
        project.find_node(node_id)->timeline()->node_end();
    if (!vc.normalize(node_end).ok()) {
      return std::nullopt;
    }
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }
  return StepEntryFixture{std::move(project), node_id, track_id, stave_id,
                          std::move(*layout_result.layout)};
}

[[nodiscard]] graphscore::KeyEvent logical_key(graphscore::LogicalKey key) {
  graphscore::KeyEvent event;
  event.logical = key;
  return event;
}

[[nodiscard]] graphscore::KeyEvent alt_key(graphscore::KeyCode code) {
  graphscore::KeyEvent event;
  event.code          = code;
  event.modifiers.alt = true;
  return event;
}

[[nodiscard]] graphscore::KeyEvent numpad_key(graphscore::KeyCode code) {
  graphscore::KeyEvent event;
  event.code = code;
  return event;
}

[[nodiscard]] graphscore::KeyEvent primary_key(graphscore::KeyCode code) {
  graphscore::KeyEvent event;
  event.code = code;
  if (kPlatformPrimaryModifier == PrimaryModifier::kMeta) {
    event.modifiers.meta = true;
  } else {
    event.modifiers.control = true;
  }
  return event;
}

// The voice-1 content of the handler's (post-move) project.
[[nodiscard]] const graphscore::VoiceContent& handler_voice(
    const SelectionToolHandler& handler, const StepEntryFixture& fixture) {
  const graphscore::Node* node = handler.project().find_node(fixture.node_id);
  return node->lane(fixture.track_id)
      ->stave(fixture.stave_id)
      ->voice(voice_one());
}

[[nodiscard]] std::vector<graphscore::SpelledPitch> handler_pitches(
    const SelectionToolHandler& handler, const StepEntryFixture& fixture) {
  std::vector<graphscore::SpelledPitch> pitches;
  for (const auto& event : handler_voice(handler, fixture).events()) {
    if (const auto* note = std::get_if<graphscore::Note>(&event)) {
      pitches.push_back(note->pitch);
    } else if (const auto* chord = std::get_if<graphscore::Chord>(&event)) {
      for (const auto& chord_note : chord->notes) {
        pitches.push_back(chord_note.pitch);
      }
    }
  }
  return pitches;
}

[[nodiscard]] graphscore::KeyEvent primary_logical(graphscore::LogicalKey key) {
  graphscore::KeyEvent event;
  event.logical = key;
  if (kPlatformPrimaryModifier == PrimaryModifier::kMeta) {
    event.modifiers.meta = true;
  } else {
    event.modifiers.control = true;
  }
  return event;
}

// The content of `voice` in the handler's (post-move) project.
[[nodiscard]] const graphscore::VoiceContent& handler_voice_at(
    const SelectionToolHandler& handler, const StepEntryFixture& fixture,
    graphscore::Voice voice) {
  const graphscore::Node* node = handler.project().find_node(fixture.node_id);
  return node->lane(fixture.track_id)->stave(fixture.stave_id)->voice(voice);
}

// Whether `vc` contains an event whose pitch set includes `pitch` (the top
// Note's own pitch, or one of a Chord's noteheads).
[[nodiscard]] bool voice_contains_pitch(const graphscore::VoiceContent& vc,
                                        graphscore::SpelledPitch        pitch) {
  for (const auto& event : vc.events()) {
    if (const auto* note = std::get_if<graphscore::Note>(&event)) {
      if (note->pitch == pitch) {
        return true;
      }
    } else if (const auto* chord = std::get_if<graphscore::Chord>(&event)) {
      for (const auto& chord_note : chord->notes) {
        if (chord_note.pitch == pitch) {
          return true;
        }
      }
    }
  }
  return false;
}

// What voice 2 carries in the voice-2 fixtures below.
enum class Voice2Kind { kChord, kRest };

// A single-staff, `measure_count`-measure node whose voice 1 is EMPTY and
// whose voice 2 carries either one whole-note Chord {C4, E4} or one whole
// Rest in measure 0, normalized. The empty voice 1 is the point: the
// palette stays armed to voice 1 while every cursor-init arm below names a
// voice-2 item, so a commit that targets the palette voice would (wrongly)
// write voice 1.
[[nodiscard]] std::optional<StepEntryFixture> build_voice2_fixture(
    const graphscore::GlyphMetrics& metrics, std::size_t measure_count,
    Voice2Kind kind) {
  graphscore::Project project{graphscore::ProjectId::generate(), "StepEntryV2"};
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
      measure_count,
      graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto whole =
      graphscore::Duration::create(graphscore::NoteValue::kWhole, 0);
  const auto voice2 = graphscore::Voice::create(2);
  if (!whole.has_value() || !voice2.has_value()) {
    return std::nullopt;
  }
  graphscore::VoiceContent& vc = lane->stave(stave_id)->voice(*voice2);
  if (kind == Voice2Kind::kChord) {
    const auto c4 = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
    const auto e4 = graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
    if (!c4.has_value() || !e4.has_value()) {
      return std::nullopt;
    }
    const graphscore::ChordNote c{graphscore::NotationEntityId::generate(), *c4,
                                  false};
    const graphscore::ChordNote e{graphscore::NotationEntityId::generate(), *e4,
                                  false};
    if (!vc.append(graphscore::make_chord(*whole, {c, e})).ok()) {
      return std::nullopt;
    }
  } else {
    if (!vc.append(graphscore::make_rest(*whole)).ok()) {
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
  return StepEntryFixture{std::move(project), node_id, track_id, stave_id,
                          std::move(*layout_result.layout)};
}

}  // namespace

int step_entry_test() {
  const SelfTestMetrics metrics;

  // --- test 1: `N` toggles the tool; entering initializes the cursor per
  //     rule 6 (blank state: first staff, armed voice, position 0); exiting
  //     discards it. -------------------------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (1)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kN));
    if (handler.active_tool() != graphscore::ActiveTool::kNoteEntry) {
      std::fprintf(stderr, "step-entry-test: N did not enter note entry (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->node != fixture->node_id ||
        cursor->track != fixture->track_id ||
        cursor->stave != fixture->stave_id || cursor->voice != voice_one() ||
        cursor->position != graphscore::Rational(0)) {
      std::fprintf(stderr, "step-entry-test: rule-6 cursor init failed (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kN));
    if (handler.active_tool() != graphscore::ActiveTool::kSelection ||
        handler.step_entry_cursor().has_value()) {
      std::fprintf(stderr, "step-entry-test: N did not exit note entry (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 2: cursor init rule 2 (a single selected notehead → its onset).
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (2)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    const auto note_id =
        graphscore::event_id(handler_voice(handler, *fixture).events().front());
    if (!select_noteheads(
            handler, {graphscore::NoteheadItem{
                         fixture->node_id, fixture->track_id, fixture->stave_id,
                         voice_one(), note_id}})) {
      std::fprintf(stderr, "step-entry-test: selection setup failed (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->position != graphscore::Rational(0)) {
      std::fprintf(stderr, "step-entry-test: rule-2 cursor init failed (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: letter commit into an empty voice (rule 6 cursor at 0): B
  //     resolves to the middle staff line B4; the cursor advances by a
  //     quarter; previous_pitch records the natural spelling. --------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (3)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const auto pitches = handler_pitches(handler, *fixture);
    if (pitches.size() != 1u ||
        pitches[0] != spelled(graphscore::Letter::kB, 4)) {
      std::fprintf(stderr,
                   "step-entry-test: B did not commit B4 (3): %zu pitches\n",
                   pitches.size());
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() ||
        cursor->position != *graphscore::Rational::create(1, 4)) {
      std::fprintf(stderr, "step-entry-test: cursor did not advance (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.previous_pitch().has_value() ||
        *handler.previous_pitch() != spelled(graphscore::Letter::kB, 4)) {
      std::fprintf(stderr, "step-entry-test: previous_pitch not B4 (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: nearest-octave rule uses previous_pitch: B4 then C resolves
  //     to C5 (nearest to B4), then B resolves back to B4. ------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (4)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kC));
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const auto pitches = handler_pitches(handler, *fixture);
    if (pitches.size() != 3u ||
        pitches[0] != spelled(graphscore::Letter::kB, 4) ||
        pitches[1] != spelled(graphscore::Letter::kC, 5) ||
        pitches[2] != spelled(graphscore::Letter::kB, 4)) {
      std::fprintf(stderr,
                   "step-entry-test: nearest-octave sequence wrong (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: Alt+Up arms an octave offset, consumed by the next successful
  //     letter commit; the lower bound is saturating. ----------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (5)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(alt_key(graphscore::KeyCode::kUp));
    if (handler.octave_offset() != 1) {
      std::fprintf(stderr, "step-entry-test: Alt+Up did not set offset (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const auto pitches = handler_pitches(handler, *fixture);
    if (pitches.size() != 1u ||
        pitches[0] != spelled(graphscore::Letter::kB, 5)) {
      std::fprintf(stderr, "step-entry-test: octave offset not applied (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.octave_offset() != 0) {
      std::fprintf(stderr, "step-entry-test: octave offset not consumed (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Saturating lower bound: decrementing past -8 clamps.
    for (int i = 0; i < 9; ++i) {
      shell.dispatch_test_key_event(alt_key(graphscore::KeyCode::kDown));
    }
    if (handler.octave_offset() != -8) {
      std::fprintf(stderr, "step-entry-test: offset did not clamp (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 6: duration arming, dot cycling, and voice arming. -------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (6)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(numpad_key(graphscore::KeyCode::kNumPad1));
    if (handler.armed_note_value() != graphscore::NoteValue::kWhole) {
      std::fprintf(stderr, "step-entry-test: KP_1 did not arm whole (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        numpad_key(graphscore::KeyCode::kNumPadDecimal));
    shell.dispatch_test_key_event(
        numpad_key(graphscore::KeyCode::kNumPadDecimal));
    if (handler.armed_dots() != 2) {
      std::fprintf(stderr, "step-entry-test: dot cycle failed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(
        numpad_key(graphscore::KeyCode::kNumPadDecimal));
    if (handler.armed_dots() != 0) {
      std::fprintf(stderr, "step-entry-test: dot cycle did not wrap (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(alt_key(graphscore::KeyCode::kDigit2));
    const auto voice2 = graphscore::Voice::create(2);
    if (!voice2.has_value() || handler.armed_voice() != *voice2) {
      std::fprintf(stderr, "step-entry-test: Alt+2 did not arm voice 2 (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: a rest commit (KP_0) materializes the voice and advances. ---
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (7)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(numpad_key(graphscore::KeyCode::kNumPad0));
    const auto& events = handler_voice(handler, *fixture).events();
    if (events.empty() ||
        !std::holds_alternative<graphscore::Rest>(events.front())) {
      std::fprintf(stderr, "step-entry-test: KP_0 did not commit a rest (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 8: a commit at node end is rejected atomically with a diagnostic,
  //     leaving the project and cursor unchanged. --------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 1, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (8)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    // Arm a whole note and commit it at position 0; the cursor advances to
    // node end (one whole note).
    shell.dispatch_test_key_event(numpad_key(graphscore::KeyCode::kNumPad1));
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const std::size_t event_count_before =
        handler_voice(handler, *fixture).events().size();
    const auto cursor_before = handler.step_entry_cursor();

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kC));
    if (handler_voice(handler, *fixture).events().size() !=
            event_count_before ||
        handler.step_entry_cursor() != cursor_before) {
      std::fprintf(stderr,
                   "step-entry-test: node-end commit was not atomic (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "step-entry-test: node-end rejection posted no diagnostic "
                   "(8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 9: a chord tone (interval) does not advance the cursor, and
  //     updates the pitch reference to the inserted notehead. ---------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (9)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const auto cursor_after_base = handler.step_entry_cursor();
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    if (handler.step_entry_cursor() != cursor_after_base) {
      std::fprintf(stderr,
                   "step-entry-test: interval advanced the cursor (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The inserted notehead is the key-spelled second above B (C), so the
    // pitch reference is now a natural C.
    if (!handler.previous_pitch().has_value() ||
        handler.previous_pitch()->letter() != graphscore::Letter::kC) {
      std::fprintf(stderr,
                   "step-entry-test: interval did not update previous_pitch "
                   "(9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 10: Primary+Down in kNoteEntry moves the cursor to the next
  //     staff and resets the pitch reference (§8.4). -----------------------
  {
    auto project = build_key_selection_project(metrics);
    if (!project.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (10)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(project->project),
                                    std::move(project->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    // Commit a note on the first staff so previous_pitch is set.
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    if (!handler.previous_pitch().has_value()) {
      std::fprintf(stderr, "step-entry-test: pitch not set (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto cursor_before = handler.step_entry_cursor();

    shell.dispatch_test_key_event(primary_key(graphscore::KeyCode::kDown));
    const auto cursor_after = handler.step_entry_cursor();
    if (!cursor_after.has_value() ||
        cursor_after->track == cursor_before->track ||
        cursor_after->stave == cursor_before->stave) {
      std::fprintf(stderr, "step-entry-test: cursor did not step staff (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.previous_pitch().has_value() || handler.octave_offset() != 0) {
      std::fprintf(stderr, "step-entry-test: pitch reference not reset (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 11: cursor init rule 1 — a single-item InsertionCaretSet is
  //     taken verbatim. -----------------------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (11)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    const auto caret =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::Rational(1)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "step-entry-test: caret build failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->node != fixture->node_id ||
        cursor->track != fixture->track_id ||
        cursor->stave != fixture->stave_id || cursor->voice != voice_one() ||
        cursor->position != graphscore::Rational(1)) {
      std::fprintf(stderr, "step-entry-test: rule-1 cursor init failed (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 12: cursor init rules 3 and 4 — an ArbitraryRangeSet uses the
  //     first item's span.start; a FullMeasureSet uses the first item, the
  //     armed voice, and measure_start(index). ------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (12)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    // Rule 3: range span [1, 2) → cursor at span.start == 1.
    const auto range =
        graphscore::ArbitraryRangeSet::create({graphscore::ArbitraryRangeItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::MusicalSpan{graphscore::Rational(1),
                                    graphscore::Rational(2)}}});
    if (!range.has_value()) {
      std::fprintf(stderr, "step-entry-test: range build failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*range});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->position != graphscore::Rational(1)) {
      std::fprintf(stderr, "step-entry-test: rule-3 cursor init failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Rule 4: full measure 1 with voice 2 armed → cursor at measure_start(1)
    // == 1 in the armed voice, snapped to that voice's nearest legal caret
    // (voice 2 is empty, so measure_start 1 snaps to the earlier boundary 0).
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto voice2 = graphscore::Voice::create(2);
    if (!voice2.has_value()) {
      std::fprintf(stderr, "step-entry-test: voice build failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.step_entry_arm_voice(*voice2);
    const auto measure_set =
        graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, 1}});
    if (!measure_set.has_value()) {
      std::fprintf(stderr, "step-entry-test: measure build failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*measure_set});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->voice != *voice2) {
      std::fprintf(stderr, "step-entry-test: rule-4 armed voice failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Rule 4 with voice 1 armed (which has content in measure 1): the cursor
    // lands exactly on measure_start(1) == 1, the first item's measure.
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    handler.step_entry_arm_voice(voice_one());
    handler.set_committed_selection(graphscore::Selection{*measure_set});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->voice != voice_one() ||
        cursor->position != graphscore::Rational(1)) {
      std::fprintf(stderr,
                   "step-entry-test: rule-4 measure_start failed (12)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 13: cursor init rule 5 — a NodeSet (no voice/position of its
  //     own) falls through to the rule-6 blank state. -----------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (13)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    const auto node_set =
        graphscore::NodeSet::create({graphscore::NodeItem{fixture->node_id}});
    if (!node_set.has_value()) {
      std::fprintf(stderr, "step-entry-test: node-set build failed (13)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*node_set});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->node != fixture->node_id ||
        cursor->track != fixture->track_id ||
        cursor->stave != fixture->stave_id || cursor->voice != voice_one() ||
        cursor->position != graphscore::Rational(0)) {
      std::fprintf(stderr, "step-entry-test: rule-5 cursor init failed (13)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 14: the nearest legal caret boundary resolves ties earlier — a
  //     caret at 1/2 (equidistant from onsets 0 and 1) snaps to 0. ----------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (14)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    const auto caret =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::Rational(1) / graphscore::Rational(2)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "step-entry-test: caret build failed (14)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->position != graphscore::Rational(0)) {
      std::fprintf(stderr, "step-entry-test: snap tie failed (14)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 15: a node with no timeline rejects initialization with a
  //     diagnostic and no cursor. -------------------------------------------
  {
    graphscore::Project project{graphscore::ProjectId::generate(),
                                "NoTimeline"};
    const auto          midi_channel = graphscore::MidiChannel::create(0);
    const auto          track_added  = project.add_track(
        "Track",
        graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
        *midi_channel);
    const graphscore::NodeId node_id = project.add_node("Node");
    (void)track_added;

    graphscore::NotationLayout layout;
    layout.node_id = node_id;

    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(project), std::move(layout), &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.step_entry_cursor().has_value() ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "step-entry-test: no-timeline rejection failed (15)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 16: an archived track rejects initialization with a diagnostic
  //     and no cursor. -------------------------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (16)\n");
      return 1;
    }
    if (!fixture->project.archive_track(fixture->track_id).ok()) {
      std::fprintf(stderr, "step-entry-test: archive failed (16)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    const auto caret =
        graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, voice_one(),
            graphscore::Rational(0)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "step-entry-test: caret build failed (16)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.step_entry_cursor().has_value() ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "step-entry-test: archived-track rejection failed (16)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 17: an absent stave rejects initialization with a diagnostic and
  //     no cursor. -----------------------------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (17)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);

    const auto caret = graphscore::InsertionCaretSet::create(
        {graphscore::InsertionCaretItem{fixture->node_id, fixture->track_id,
                                        graphscore::StaveId::generate(),
                                        voice_one(), graphscore::Rational(0)}});
    if (!caret.has_value()) {
      std::fprintf(stderr, "step-entry-test: caret build failed (17)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*caret});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.step_entry_cursor().has_value() ||
        handler.diagnostics().empty()) {
      std::fprintf(stderr,
                   "step-entry-test: absent-stave rejection failed (17)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 18: the no-previous-pitch reference is the active clef's middle
  //     staff line (bass D3, alto C4, tenor A3), via the stave's default
  //     clef fallback. ------------------------------------------------------
  {
    struct ClefCase {
      graphscore::Clef       clef;
      graphscore::LogicalKey letter;
      graphscore::Letter     expected_letter;
      std::int8_t            expected_octave;
    };

    const std::array<ClefCase, 3> kCases{{
        {graphscore::Clef::kBass, graphscore::LogicalKey::kD,
         graphscore::Letter::kD, 3},
        {graphscore::Clef::kAlto, graphscore::LogicalKey::kC,
         graphscore::Letter::kC, 4},
        {graphscore::Clef::kTenor, graphscore::LogicalKey::kA,
         graphscore::Letter::kA, 3},
    }};
    for (const ClefCase& test_case : kCases) {
      auto fixture =
          build_step_entry_fixture(metrics, 2, false, test_case.clef);
      if (!fixture.has_value()) {
        std::fprintf(stderr, "step-entry-test: fixture build failed (18)\n");
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fixture->project),
                                      std::move(fixture->layout), &shell);
      handler.set_metrics(&metrics);
      shell.set_input_handler(&handler);
      handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
      shell.dispatch_test_key_event(logical_key(test_case.letter));
      const auto pitches = handler_pitches(handler, *fixture);
      if (pitches.size() != 1u ||
          pitches[0] !=
              spelled(test_case.expected_letter, test_case.expected_octave)) {
        std::fprintf(stderr, "step-entry-test: clef reference wrong (18)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }
  }

  // --- test 19: the octave offset saturates at +8 (and the step is a no-op
  //     at the bound). -------------------------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (19)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    for (int i = 0; i < 9; ++i) {
      shell.dispatch_test_key_event(alt_key(graphscore::KeyCode::kUp));
    }
    if (handler.octave_offset() != 8) {
      std::fprintf(stderr, "step-entry-test: upper saturation failed (19)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 20: a rejected commit preserves previous_pitch and the pending
  //     octave offset (single-shot consumption is success-only). ------------
  {
    auto fixture = build_step_entry_fixture(metrics, 1, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (20)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(numpad_key(graphscore::KeyCode::kNumPad1));
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const auto event_count_before =
        handler_voice(handler, *fixture).events().size();
    const auto previous_before = handler.previous_pitch();
    shell.dispatch_test_key_event(alt_key(graphscore::KeyCode::kUp));
    // Commit at node end: rejected, so the offset and pitch reference stay.
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kC));
    if (handler_voice(handler, *fixture).events().size() !=
            event_count_before ||
        handler.octave_offset() != 1 ||
        handler.previous_pitch() != previous_before) {
      std::fprintf(stderr,
                   "step-entry-test: rejected commit consumed state (20)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 21: a voice change updates the cursor's voice and resets the
  //     pitch reference, and the next commit lands in the newly armed voice.
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (21)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    // Commit B in voice 1, then arm voice 2: the pitch reference resets and
    // the cursor's voice follows the armed voice.
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    shell.dispatch_test_key_event(alt_key(graphscore::KeyCode::kDigit2));
    const auto voice2 = graphscore::Voice::create(2);
    if (!voice2.has_value()) {
      std::fprintf(stderr, "step-entry-test: voice build failed (21)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->voice != *voice2) {
      std::fprintf(stderr, "step-entry-test: cursor voice not updated (21)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.previous_pitch().has_value() || handler.octave_offset() != 0) {
      std::fprintf(stderr,
                   "step-entry-test: voice change did not reset reference "
                   "(21)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // A fresh handler: arming voice 2 first, then committing, writes the
    // note into voice 2 rather than voice 1.
    {
      auto fixture2 = build_step_entry_fixture(metrics, 2, false);
      if (!fixture2.has_value()) {
        std::fprintf(stderr, "step-entry-test: fixture2 failed (21)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      graphscore::WriterShell shell2;
      SelectionToolHandler    handler2(std::move(fixture2->project),
                                       std::move(fixture2->layout), &shell2);
      handler2.set_metrics(&metrics);
      shell2.set_input_handler(&handler2);
      handler2.set_active_tool(graphscore::ActiveTool::kNoteEntry);
      shell2.dispatch_test_key_event(alt_key(graphscore::KeyCode::kDigit2));
      shell2.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
      if (!handler_voice_at(handler2, *fixture2, voice_one())
               .events()
               .empty() ||
          handler_voice_at(handler2, *fixture2, *voice2).events().empty()) {
        std::fprintf(stderr,
                     "step-entry-test: armed-voice commit wrong voice (21)\n");
        shell2.set_input_handler(nullptr);
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell2.set_input_handler(nullptr);
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 22: undoing the commit that produced the current previous_pitch
  //     resets the pitch reference. -----------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (22)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    if (!handler.previous_pitch().has_value()) {
      std::fprintf(stderr, "step-entry-test: pitch not set (22)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(primary_logical(graphscore::LogicalKey::kZ));
    if (handler.previous_pitch().has_value() || handler.octave_offset() != 0) {
      std::fprintf(stderr,
                   "step-entry-test: undo did not reset reference "
                   "(22)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 23: pointer note entry (a kNoteEntry click) commits at the
  //     resolved onset, repositions the cursor there, updates previous_pitch,
  //     and resets the octave offset. ---------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (23)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    // Arm an octave offset so its reset by the pointer commit is observable.
    shell.dispatch_test_key_event(alt_key(graphscore::KeyCode::kUp));
    if (handler.octave_offset() != 1) {
      std::fprintf(stderr, "step-entry-test: offset not armed (23)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto& staff   = handler.layout().systems[0].staves[0];
    const auto& measure = handler.layout().systems[0].measures[0];
    click_at(shell, measure.bounds.x + measure.bounds.width * 0.5,
             staff.bounds.y + staff.bounds.height * 0.5);

    const auto& events = handler_voice(handler, *fixture).events();
    if (events.empty() ||
        (!std::holds_alternative<graphscore::Note>(events.front()) &&
         !std::holds_alternative<graphscore::Chord>(events.front()))) {
      std::fprintf(stderr, "step-entry-test: pointer commit failed (23)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The click at the middle staff line commits B4 and records it as the
    // pitch reference, repositions the cursor to the resolved onset 0, and
    // resets the pending octave offset.
    if (!handler.previous_pitch().has_value() ||
        *handler.previous_pitch() != spelled(graphscore::Letter::kB, 4)) {
      std::fprintf(stderr,
                   "step-entry-test: pointer previous_pitch wrong "
                   "(23)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.octave_offset() != 0) {
      std::fprintf(stderr, "step-entry-test: pointer offset not reset (23)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() || cursor->position != graphscore::Rational(0)) {
      std::fprintf(stderr,
                   "step-entry-test: pointer cursor not repositioned (23)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 24: a dotted armed duration advances the cursor by its resolved
  //     length. -------------------------------------------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (24)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(numpad_key(graphscore::KeyCode::kNumPad3));
    shell.dispatch_test_key_event(
        numpad_key(graphscore::KeyCode::kNumPadDecimal));
    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const auto cursor = handler.step_entry_cursor();
    if (!cursor.has_value() ||
        cursor->position != *graphscore::Rational::create(3, 8)) {
      std::fprintf(stderr,
                   "step-entry-test: dotted duration advance failed (24)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 25: a commit at an existing onset builds a chord when the new
  //     pitch differs from the existing event's pitch (§8.5). ----------------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (25)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    const auto& events = handler_voice(handler, *fixture).events();
    if (events.empty() ||
        !std::holds_alternative<graphscore::Chord>(events.front())) {
      std::fprintf(stderr,
                   "step-entry-test: existing-onset chord build failed (25)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 26: a rest commit selects the inserted rest (a RestSet). -------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (26)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(numpad_key(graphscore::KeyCode::kNumPad0));
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value() ||
        !std::holds_alternative<graphscore::RestSet>(*committed)) {
      std::fprintf(stderr, "step-entry-test: rest selection failed (26)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 27: every cursor-init arm targets the cursor's voice, not the
  //     armed palette voice (§8.1). The palette stays armed to voice 1 while
  //     the cursor names voice 2; a note and a rest commit each land in
  //     voice 2 only. --------------------------------------------------------
  {
    const auto voice2 = graphscore::Voice::create(2);
    if (!voice2.has_value()) {
      std::fprintf(stderr, "step-entry-test: voice build failed (27)\n");
      return 1;
    }
    // (a) caret arm: a voice-2 caret at position 0, palette voice 1.
    {
      auto fixture = build_step_entry_fixture(metrics, 2, false);
      if (!fixture.has_value()) {
        std::fprintf(stderr, "step-entry-test: fixture build failed (27a)\n");
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fixture->project),
                                      std::move(fixture->layout), &shell);
      handler.set_metrics(&metrics);
      shell.set_input_handler(&handler);
      const auto caret =
          graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
              fixture->node_id, fixture->track_id, fixture->stave_id, *voice2,
              graphscore::Rational(0)}});
      if (!caret.has_value()) {
        std::fprintf(stderr, "step-entry-test: caret build failed (27a)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*caret});
      handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
      if (!handler.step_entry_cursor().has_value() ||
          handler.step_entry_cursor()->voice != *voice2 ||
          handler.armed_voice() != voice_one()) {
        std::fprintf(stderr,
                     "step-entry-test: caret cursor voice wrong (27a)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
      if (!voice_contains_pitch(handler_voice_at(handler, *fixture, *voice2),
                                spelled(graphscore::Letter::kB, 4)) ||
          !handler_voice_at(handler, *fixture, voice_one()).events().empty()) {
        std::fprintf(stderr,
                     "step-entry-test: caret note landed in wrong voice "
                     "(27a)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }
    // (b) caret arm: a rest commit (KP_0) also lands in voice 2 only.
    {
      auto fixture = build_step_entry_fixture(metrics, 2, false);
      if (!fixture.has_value()) {
        std::fprintf(stderr, "step-entry-test: fixture build failed (27b)\n");
        return 1;
      }
      graphscore::WriterShell shell;
      SelectionToolHandler    handler(std::move(fixture->project),
                                      std::move(fixture->layout), &shell);
      handler.set_metrics(&metrics);
      shell.set_input_handler(&handler);
      const auto caret =
          graphscore::InsertionCaretSet::create({graphscore::InsertionCaretItem{
              fixture->node_id, fixture->track_id, fixture->stave_id, *voice2,
              graphscore::Rational(0)}});
      if (!caret.has_value()) {
        std::fprintf(stderr, "step-entry-test: caret build failed (27b)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      handler.set_committed_selection(graphscore::Selection{*caret});
      handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
      shell.dispatch_test_key_event(numpad_key(graphscore::KeyCode::kNumPad0));
      const auto& v2 = handler_voice_at(handler, *fixture, *voice2).events();
      if (v2.empty() || !std::holds_alternative<graphscore::Rest>(v2.front()) ||
          !handler_voice_at(handler, *fixture, voice_one()).events().empty()) {
        std::fprintf(stderr,
                     "step-entry-test: caret rest landed in wrong voice "
                     "(27b)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      shell.set_input_handler(nullptr);
    }
    // (c) notehead / chord / rest / range arms each carry the item's voice.
    const auto check_voice2_arm =
        [&](const char* what, Voice2Kind kind,
            const std::function<void(SelectionToolHandler&,
                                     const StepEntryFixture&,
                                     graphscore::Voice)>& select) {
          auto fixture = build_voice2_fixture(metrics, 2, kind);
          if (!fixture.has_value()) {
            std::fprintf(stderr, "step-entry-test: fixture failed (%s)\n",
                         what);
            return false;
          }
          graphscore::WriterShell shell;
          SelectionToolHandler    handler(std::move(fixture->project),
                                          std::move(fixture->layout), &shell);
          handler.set_metrics(&metrics);
          shell.set_input_handler(&handler);
          select(handler, *fixture, *voice2);
          handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
          if (!handler.step_entry_cursor().has_value() ||
              handler.step_entry_cursor()->voice != *voice2 ||
              handler.armed_voice() != voice_one()) {
            std::fprintf(stderr, "step-entry-test: %s cursor voice wrong\n",
                         what);
            shell.set_input_handler(nullptr);
            return false;
          }
          shell.dispatch_test_key_event(
              logical_key(graphscore::LogicalKey::kB));
          const bool ok =
              voice_contains_pitch(handler_voice_at(handler, *fixture, *voice2),
                                   spelled(graphscore::Letter::kB, 4)) &&
              handler_voice_at(handler, *fixture, voice_one()).events().empty();
          shell.set_input_handler(nullptr);
          if (!ok) {
            std::fprintf(stderr, "step-entry-test: %s wrong voice\n", what);
          }
          return ok;
        };
    if (!check_voice2_arm(
            "notehead", Voice2Kind::kChord,
            [](SelectionToolHandler& handler, const StepEntryFixture& fixture,
               graphscore::Voice voice) {
              const auto& events =
                  handler_voice_at(handler, fixture, voice).events();
              const graphscore::Chord& chord =
                  std::get<graphscore::Chord>(events.front());
              (void)select_noteheads(
                  handler,
                  {graphscore::NoteheadItem{fixture.node_id, fixture.track_id,
                                            fixture.stave_id, voice,
                                            chord.notes.front().id}});
            })) {
      return 1;
    }
    if (!check_voice2_arm(
            "chord", Voice2Kind::kChord,
            [](SelectionToolHandler& handler, const StepEntryFixture& fixture,
               graphscore::Voice voice) {
              const auto& events =
                  handler_voice_at(handler, fixture, voice).events();
              const auto chord_id = graphscore::event_id(events.front());
              const auto set      = graphscore::ChordSet::create(
                  {graphscore::ChordItem{fixture.node_id, fixture.track_id,
                                         fixture.stave_id, voice, chord_id}});
              if (set.has_value()) {
                handler.set_committed_selection(graphscore::Selection{*set});
              }
            })) {
      return 1;
    }
    if (!check_voice2_arm(
            "rest", Voice2Kind::kRest,
            [](SelectionToolHandler& handler, const StepEntryFixture& fixture,
               graphscore::Voice voice) {
              const auto& events =
                  handler_voice_at(handler, fixture, voice).events();
              const auto rest_id = graphscore::event_id(events.front());
              const auto set     = graphscore::RestSet::create(
                  {graphscore::RestItem{fixture.node_id, fixture.track_id,
                                        fixture.stave_id, voice, rest_id}});
              if (set.has_value()) {
                handler.set_committed_selection(graphscore::Selection{*set});
              }
            })) {
      return 1;
    }
    if (!check_voice2_arm(
            "range", Voice2Kind::kChord,
            [](SelectionToolHandler& handler, const StepEntryFixture& fixture,
               graphscore::Voice voice) {
              const auto range = graphscore::ArbitraryRangeSet::create(
                  {graphscore::ArbitraryRangeItem{
                      fixture.node_id, fixture.track_id, fixture.stave_id,
                      voice,
                      graphscore::MusicalSpan{graphscore::Rational(0),
                                              graphscore::Rational(1)}}});
              if (range.has_value()) {
                handler.set_committed_selection(graphscore::Selection{*range});
              }
            })) {
      return 1;
    }
  }

  // --- test 28: a stale full-measure selection (out-of-range measure index)
  //     rejects step-entry initialization with a diagnostic and no cursor,
  //     leaving the project/selection untouched. ----------------------------
  {
    auto fixture = build_step_entry_fixture(metrics, 1, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (28)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    shell.set_input_handler(&handler);
    const auto measure_set =
        graphscore::FullMeasureSet::create({graphscore::FullMeasureItem{
            fixture->node_id, fixture->track_id, fixture->stave_id, 1}});
    if (!measure_set.has_value()) {
      std::fprintf(stderr, "step-entry-test: measure build failed (28)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_committed_selection(graphscore::Selection{*measure_set});
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.step_entry_cursor().has_value() ||
        handler.diagnostics().empty() ||
        !std::holds_alternative<graphscore::FullMeasureSet>(
            *handler.drag_state().committed_selection())) {
      std::fprintf(stderr,
                   "step-entry-test: stale full-measure init not rejected "
                   "(28)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 29: undoing a LATER accidental step preserves the pitch
  //     reference; only undoing the producer commit clears it (§8.3). -------
  {
    auto fixture = build_step_entry_fixture(metrics, 2, true);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (29)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    if (!handler.previous_pitch().has_value() ||
        *handler.previous_pitch() != spelled(graphscore::Letter::kB, 4)) {
      std::fprintf(stderr, "step-entry-test: pitch not set (29)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // A later accidental step (a separate undoable command) must NOT clear
    // the reference.
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kEquals));
    shell.dispatch_test_key_event(primary_logical(graphscore::LogicalKey::kZ));
    if (!handler.previous_pitch().has_value() ||
        *handler.previous_pitch() != spelled(graphscore::Letter::kB, 4)) {
      std::fprintf(stderr,
                   "step-entry-test: accidental undo cleared the reference "
                   "(29)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 30: undoing an interval-inserted producer clears the pitch
  //     reference (the interval is the producer of the current reference). --
  {
    auto fixture = build_step_entry_fixture(metrics, 2, false);
    if (!fixture.has_value()) {
      std::fprintf(stderr, "step-entry-test: fixture build failed (30)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler    handler(std::move(fixture->project),
                                    std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);

    shell.dispatch_test_key_event(logical_key(graphscore::LogicalKey::kB));
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDigit2));
    if (!handler.previous_pitch().has_value() ||
        handler.previous_pitch()->letter() != graphscore::Letter::kC) {
      std::fprintf(stderr,
                   "step-entry-test: interval did not set reference (30)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(primary_logical(graphscore::LogicalKey::kZ));
    if (handler.previous_pitch().has_value()) {
      std::fprintf(stderr,
                   "step-entry-test: interval-producer undo did not clear "
                   "reference (30)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("step-entry-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
