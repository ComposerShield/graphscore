// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

namespace graphscore {

// Toolkit-neutral roles consumed by the platform accessibility bridge. These
// describe musical/editor concepts; engraving glyphs are deliberately absent.
enum class AccessibilityRole : std::uint8_t {
  kNode = 0,
  kTrack,
  kStaff,
  kMeasure,
  kVoice,
  kChord,
  kNote,
  kRest,
  kMarking,
  kPalette,
  kSelection,
};

enum class AccessibilityState : std::uint8_t {
  kNone      = 0,
  kSelected  = 1U << 0U,
  kFocused   = 1U << 1U,
  kOffscreen = 1U << 2U,
};

[[nodiscard]] constexpr AccessibilityState operator|(AccessibilityState left,
                                                     AccessibilityState right) {
  return static_cast<AccessibilityState>(static_cast<std::uint8_t>(left) |
                                         static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_state(AccessibilityState states,
                                       AccessibilityState state) noexcept {
  return (static_cast<std::uint8_t>(states) &
          static_cast<std::uint8_t>(state)) != 0U;
}

struct AccessibilityNode {
  struct Action {
    std::string id;
    std::string name;

    [[nodiscard]] bool operator==(const Action&) const = default;
  };

  std::string       id;
  AccessibilityRole role = AccessibilityRole::kNode;
  std::string       name;
  // Spoken value separate from the concise semantic name. For notation
  // events this carries duration, voice, bar/beat, and sounding pitch.
  std::string                 value;
  std::optional<NotationRect> bounds;
  AccessibilityState          states = AccessibilityState::kNone;
  // Actions currently available for this semantic object. IDs are stable
  // application-owned invocation keys; names are suitable for announcement.
  std::vector<Action>        actions;
  std::optional<std::size_t> parent;
  std::vector<std::size_t>   children;
  // Stable semantic IDs named by aggregate concepts such as a selection.
  std::vector<std::string> related_ids;

  [[nodiscard]] bool operator==(const AccessibilityNode&) const = default;
};

struct AccessibilityBuildResult;

class AccessibilityTree {
 public:
  AccessibilityTree() = default;

  [[nodiscard]] const std::vector<AccessibilityNode>& nodes() const noexcept {
    return nodes_;
  }

  [[nodiscard]] std::optional<std::size_t> root() const noexcept {
    return root_;
  }

  [[nodiscard]] std::optional<std::size_t> focused() const noexcept {
    return focused_;
  }

  [[nodiscard]] const AccessibilityNode* find(const std::string& id) const;

 private:
  friend AccessibilityBuildResult build_notation_accessibility_tree(
      const Project& project, NodeId node_id, const NotationLayout& layout,
      const NotePaletteState& palette, const Selection* selection,
      std::span<const AccessibilityNode::Action> available_actions,
      std::span<const AccessibilityNode::Action> palette_actions,
      std::string_view                           focused_id,
      std::span<const std::string>               focus_ancestors);

  AccessibilityTree(std::vector<AccessibilityNode> nodes,
                    std::optional<std::size_t>     root,
                    std::optional<std::size_t>     focused) noexcept;

  std::vector<AccessibilityNode> nodes_;
  std::optional<std::size_t>     root_;
  std::optional<std::size_t>     focused_;
};

enum class AccessibilityBuildError : std::uint8_t {
  kNone = 0,
  kNodeNotFound,
  kTimelineMissing,
  kLayoutNodeMismatch,
  kLaneMissing,
  kSelectionInvalid,
  kSelectionOutsideNode,
  kSelectionTargetNotExposed,
  kDuplicateSemanticId,
};

struct AccessibilityBuildResult {
  AccessibilityBuildError          error = AccessibilityBuildError::kNone;
  std::optional<AccessibilityTree> tree;

  [[nodiscard]] explicit operator bool() const noexcept {
    return tree.has_value();
  }
};

// Projects the focused notation editor into stable musical semantics. Render
// commands are never inspected, so one logical object remains one accessible
// object regardless of how many glyphs or path segments engrave it. Musical
// objects omitted from the retained layout remain virtual tree nodes with
// kOffscreen state and no bounds, so a platform bridge can materialize them on
// demand without making viewport culling observable to assistive technology.
[[nodiscard]] AccessibilityBuildResult build_notation_accessibility_tree(
    const Project& project, NodeId node_id, const NotationLayout& layout,
    const NotePaletteState& palette, const Selection* selection = nullptr,
    std::span<const AccessibilityNode::Action> available_actions = {},
    std::span<const AccessibilityNode::Action> palette_actions   = {},
    std::string_view                           focused_id        = {},
    std::span<const std::string>               focus_ancestors   = {});

}  // namespace graphscore
