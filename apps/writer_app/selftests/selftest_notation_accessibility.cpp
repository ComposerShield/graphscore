// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../notation_accessibility.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

#include <graphscore/accessibility/graphscore_accessibility.hpp>
#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {
namespace {

[[nodiscard]] const graphscore::AccessibilityNode::Action* find_action(
    const std::vector<graphscore::AccessibilityNode::Action>& actions,
    std::string_view                                          name) {
  const auto found = std::ranges::find(
      actions, name, &graphscore::AccessibilityNode::Action::name);
  return found == actions.end() ? nullptr : &*found;
}

[[nodiscard]] const graphscore::VoiceContent* voice_content(
    const SelectionToolHandler& handler, const NoteheadMoveFixture& fixture) {
  const graphscore::Node* node = handler.project().find_node(fixture.node_id);
  if (node == nullptr) {
    return nullptr;
  }
  const graphscore::TrackLane* lane = node->lane(fixture.track_id);
  if (lane == nullptr || lane->stave(fixture.stave_id) == nullptr) {
    return nullptr;
  }
  return &lane->stave(fixture.stave_id)->voice(voice_one());
}

}  // namespace

int notation_accessibility_test() {
  const SelfTestMetrics metrics;
  auto                  fixture = build_notehead_move_fixture(metrics);
  if (!fixture.has_value()) {
    std::fprintf(stderr, "notation-accessibility-test: fixture failed\n");
    return 1;
  }

  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(fixture->project),
                                  std::move(fixture->layout), &shell);
  handler.set_metrics(&metrics);
  handler.set_surface_publisher(
      [&shell](const graphscore::NotationLayout& layout) {
        return publish_headless_test_surface(layout, &shell);
      });
  if (!select_noteheads(
          handler, {{fixture->node_id, fixture->track_id, fixture->stave_id,
                     voice_one(), fixture->first_note_id}})) {
    std::fprintf(stderr,
                 "notation-accessibility-test: selection setup failed\n");
    return 1;
  }

  NotationAccessibilityController accessibility(&handler);
  auto                            actions = accessibility.available_actions();
  const auto* move_up    = find_action(actions, "Move note up");
  const auto* enter_mode = find_action(actions, "Toggle note entry");
  if (move_up == nullptr || enter_mode == nullptr ||
      !accessibility.invoke(move_up->id)) {
    std::fprintf(stderr,
                 "notation-accessibility-test: note edit action failed\n");
    return 1;
  }

  const graphscore::AccessibilityBuildResult edited_tree =
      accessibility.build_tree();
  if (!edited_tree || !edited_tree.tree.has_value()) {
    std::fprintf(stderr,
                 "notation-accessibility-test: semantic tree build failed\n");
    return 1;
  }
  const auto selected = std::ranges::find_if(
      edited_tree.tree->nodes(), [](const graphscore::AccessibilityNode& node) {
        return graphscore::has_state(node.states,
                                     graphscore::AccessibilityState::kSelected);
      });
  if (selected == edited_tree.tree->nodes().end() ||
      find_action(selected->actions, "Move note down") == nullptr) {
    std::fprintf(stderr,
                 "notation-accessibility-test: selected actions missing\n");
    return 1;
  }

  actions    = accessibility.available_actions();
  enter_mode = find_action(actions, "Toggle note entry");
  if (enter_mode == nullptr || !accessibility.invoke(enter_mode->id)) {
    std::fprintf(stderr,
                 "notation-accessibility-test: entry-mode action failed\n");
    return 1;
  }
  actions              = accessibility.available_actions();
  const auto* duration = find_action(actions, "Duration eighth");
  const auto* pitch    = find_action(actions, "Enter pitch E");
  if (duration == nullptr || pitch == nullptr ||
      !accessibility.invoke(duration->id)) {
    std::fprintf(stderr,
                 "notation-accessibility-test: entry controls unavailable\n");
    return 1;
  }
  actions = accessibility.available_actions();
  pitch   = find_action(actions, "Enter pitch E");
  if (pitch == nullptr || !accessibility.invoke(pitch->id)) {
    std::fprintf(stderr,
                 "notation-accessibility-test: pitch entry action failed\n");
    return 1;
  }

  const graphscore::VoiceContent* voice = voice_content(handler, *fixture);
  if (voice == nullptr || voice->events().empty() ||
      !std::holds_alternative<graphscore::Chord>(voice->events().front()) ||
      std::get<graphscore::Chord>(voice->events().front()).notes.size() != 2U) {
    std::fprintf(
        stderr,
        "notation-accessibility-test: pointerless entry did not edit score\n");
    return 1;
  }
  if (accessibility.invoke("graphscore.notation.command.unknown")) {
    std::fprintf(stderr,
                 "notation-accessibility-test: unknown action was accepted\n");
    return 1;
  }

  std::printf("notation-accessibility-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
