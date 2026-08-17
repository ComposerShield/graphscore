// SPDX-License-Identifier: Apache-2.0

#include "notation_accessibility.hpp"

#include "command_palette.hpp"
#include "selection_tool_handler.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace graphscore::writer_app {
namespace {

constexpr std::string_view kActionPrefix = "graphscore.notation.command.";

[[nodiscard]] std::string action_id(PaletteCommandId id) {
  return std::string(kActionPrefix) +
         std::to_string(static_cast<unsigned int>(id));
}

[[nodiscard]] std::optional<PaletteCommandId> command_for_action(
    std::string_view id) {
  const auto& inventory = palette_inventory();
  const auto  found =
      std::ranges::find_if(inventory, [id](const PaletteCommand& command) {
        return action_id(command.id) == id;
      });
  if (found == inventory.end()) {
    return std::nullopt;
  }
  return found->id;
}

}  // namespace

NotationAccessibilityController::NotationAccessibilityController(
    SelectionToolHandler* handler)
    : handler_(handler) {}

std::vector<graphscore::AccessibilityNode::Action>
NotationAccessibilityController::available_actions() const {
  std::vector<graphscore::AccessibilityNode::Action> actions;
  if (handler_ == nullptr) {
    return actions;
  }
  const auto& inventory = palette_inventory();
  actions.reserve(inventory.size());
  for (const PaletteCommand& command : inventory) {
    if (handler_->palette_command_available(command.id)) {
      actions.push_back({action_id(command.id), command.name});
    }
  }
  return actions;
}

graphscore::AccessibilityBuildResult
NotationAccessibilityController::build_tree() {
  if (handler_ == nullptr) {
    return {graphscore::AccessibilityBuildError::kNodeNotFound, std::nullopt};
  }
  const std::vector<graphscore::AccessibilityNode::Action> actions =
      available_actions();
  const auto& selection = handler_->drag_state().committed_selection();
  graphscore::AccessibilityBuildResult result =
      graphscore::build_notation_accessibility_tree(
          handler_->project(), handler_->layout().node_id, handler_->layout(),
          handler_->note_palette_state(),
          selection.has_value() ? &*selection : nullptr, actions, actions,
          focused_id_.value_or(""), focus_ancestors_);
  if (result && result.tree->focused().has_value()) {
    const auto& nodes = result.tree->nodes();
    focused_id_       = nodes[*result.tree->focused()].id;
    focus_ancestors_.clear();
    std::optional<std::size_t> parent = nodes[*result.tree->focused()].parent;
    while (parent.has_value()) {
      focus_ancestors_.push_back(nodes[*parent].id);
      parent = nodes[*parent].parent;
    }
  }
  return result;
}

bool NotationAccessibilityController::set_focus(std::string_view semantic_id) {
  if (semantic_id.empty()) {
    return false;
  }
  const std::optional<std::string> previous           = focused_id_;
  std::vector<std::string>         previous_ancestors = focus_ancestors_;
  focused_id_                                         = semantic_id;
  focus_ancestors_.clear();
  const graphscore::AccessibilityBuildResult result = build_tree();
  if (result && focused_id_ == semantic_id) {
    return true;
  }
  focused_id_      = previous;
  focus_ancestors_ = std::move(previous_ancestors);
  return false;
}

void NotationAccessibilityController::clear_focus() noexcept {
  focused_id_.reset();
  focus_ancestors_.clear();
}

const std::optional<std::string>& NotationAccessibilityController::focused_id()
    const noexcept {
  return focused_id_;
}

bool NotationAccessibilityController::invoke(std::string_view action_id_value) {
  if (handler_ == nullptr) {
    return false;
  }
  const std::optional<PaletteCommandId> command =
      command_for_action(action_id_value);
  return command.has_value() && handler_->palette_command_available(*command) &&
         handler_->run_palette_command(*command);
}

}  // namespace graphscore::writer_app
