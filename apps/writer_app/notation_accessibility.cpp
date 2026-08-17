// SPDX-License-Identifier: Apache-2.0

#include "notation_accessibility.hpp"

#include "command_palette.hpp"
#include "selection_tool_handler.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
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
NotationAccessibilityController::build_tree() const {
  if (handler_ == nullptr) {
    return {graphscore::AccessibilityBuildError::kNodeNotFound, std::nullopt};
  }
  const std::vector<graphscore::AccessibilityNode::Action> actions =
      available_actions();
  const auto& selection = handler_->drag_state().committed_selection();
  return graphscore::build_notation_accessibility_tree(
      handler_->project(), handler_->layout().node_id, handler_->layout(),
      handler_->note_palette_state(),
      selection.has_value() ? &*selection : nullptr, actions, actions);
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
