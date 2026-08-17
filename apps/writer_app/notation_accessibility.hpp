// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/accessibility/graphscore_accessibility.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace graphscore::writer_app {

class SelectionToolHandler;

// Connects the toolkit-neutral semantic tree to the app-owned action router.
// Platform accessibility bridges retain only the stable string action ID and
// invoke it here; pointer and key-event synthesis are deliberately unnecessary.
class NotationAccessibilityController {
 public:
  explicit NotationAccessibilityController(SelectionToolHandler* handler);

  [[nodiscard]] graphscore::AccessibilityBuildResult build_tree();

  bool set_focus(std::string_view semantic_id);
  void clear_focus() noexcept;

  [[nodiscard]] const std::optional<std::string>& focused_id() const noexcept;

  [[nodiscard]] std::vector<graphscore::AccessibilityNode::Action>
  available_actions() const;

  bool invoke(std::string_view action_id);

 private:
  SelectionToolHandler*      handler_;
  std::optional<std::string> focused_id_;
  std::vector<std::string>   focus_ancestors_;
};

}  // namespace graphscore::writer_app
