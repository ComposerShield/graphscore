// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace spike {

enum class A11yRole : uint8_t {
    Unknown,
    Application,
    Window,
    Group,
    GraphNode,
    Connector,
    Measure,
    Note,
    Control,
    Selection,
    ActionItem,
    Label,
    Staff,
};

enum class A11yState : uint32_t {
    None = 0,
    Selected = 1u << 0,
    Focused = 1u << 1,
    Disabled = 1u << 2,
    Editable = 1u << 3,
    Checked = 1u << 4,
};
inline constexpr A11yState operator|(A11yState a, A11yState b) noexcept {
    return static_cast<A11yState>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr bool operator&(A11yState a, A11yState b) noexcept {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

struct A11yAction {
    std::string id;          // e.g. "press", "select", "deselect"
    std::string description;  // human-readable label
};

struct SemanticNode {
    std::string stableId;       // unique, stable across tree updates
    A11yRole role = A11yRole::Unknown;
    std::string name;           // user-visible label
    std::string value;          // current value description
    std::string description;    // extended description
    Rect bounds = {};           // world-space bounds (unzoomed)

    A11yState state = A11yState::None;
    bool hasKeyboardFocus = false;

    std::vector<A11yAction> actions;

    // Parent and children indices — NOT for direct mutation by callers.
    // Use SemanticTree::Reparent / DetachChild / SetParentChild.
    int parentIndex = -1;
    std::vector<int> childIndices;
};

/// Result of AddNode.
struct AddNodeResult {
    int index = -1;        // the node's index in the tree, or -1 on rejection
    bool accepted = false; // true if added, false if duplicate stableId
};

using A11yActionCallback = std::function<void(const std::string& nodeId,
                                                const std::string& actionId)>;

class SemanticTree {
public:
    SemanticTree() = default;

    // --- Mutation ---
    //
    // Mutation contract: after any mutation (AddNode, SetParentChild, Reparent,
    // DetachChild, Clear), the generation counter is incremented. Native
    // accessibility bridge elements must re-lookup nodes via index/ID through
    // the owning tree on every query, or rebuild their mappings entirely when
    // the generation they cached differs from the current tree generation.

    // Add a node; returns result with index. Rejects duplicate stable IDs.
    // Increments generation on success.
    AddNodeResult AddNode(SemanticNode node);

    // Set parent/child relationship. Validates bounds, rejects cycles,
    // rejects duplicate parent assignments. Increments generation.
    // Returns true if the relationship was set, false if rejected.
    bool SetParentChild(int parentIdx, int childIdx);

    // Reparent: atomically move childIdx from its current parent to newParent.
    // Validates bounds, rejects cycles, increments generation.
    // Returns true on success. The old parent's childIndices are updated.
    bool Reparent(int newParentIdx, int childIdx);

    // Detach child: remove childIdx from its current parent. The child's
    // parentIndex is set to -1 and it is removed from the old parent's
    // childIndices. Increments generation.
    bool DetachChild(int childIdx);

    // Clear the tree. Increments generation.
    void Clear();

    // Monotonic generation counter — incremented on every mutation.
    uint64_t Generation() const { return m_generation; }

    // --- Query ---

    int RootIndex() const { return m_rootIndex; }
    // Set the root node. Validates that idx is in bounds and increments
    // generation. Returns true if set, false if idx is invalid.
    bool SetRoot(int idx);

    const SemanticNode& Node(int idx) const { return m_nodes[static_cast<size_t>(idx)]; }
    SemanticNode& Node(int idx) { return m_nodes[static_cast<size_t>(idx)]; }
    int NodeCount() const { return static_cast<int>(m_nodes.size()); }

    int FindByStableId(const std::string& id) const;

    // --- State mutators ---

    void SetSelected(int idx, bool selected);
    void SetFocused(int idx, bool focused);
    void SetKeyboardFocus(int idx, bool hasFocus);

    // --- Action dispatch ---

    void SetActionCallback(A11yActionCallback cb);

    // Invoke an action by ID. Validates index bounds and that the actionId
    // is declared in the node's actions vector. Returns true if invoked.
    bool InvokeAction(int idx, const std::string& actionId);

    // --- Exception-atomicity test seam ---
    //
    // Hook invoked at the point where childIndices reserve/allocation would
    // occur in SetParentChild, before any mutation.  If the hook throws
    // (e.g. std::bad_alloc), the tree state must remain unchanged.
    // Production default is empty (zero-cost).
    using ReserveHook = std::function<void()>;
    void SetReserveHook(ReserveHook hook) { m_reserveHook = std::move(hook); }

    // --- Bounds in screen coordinates given a view transform ---
    Rect ComputeScreenBounds(int idx, const Transform& xform) const;

    // --- Hit test (screen coords) ---
    int HitTest(Vec2 screenPos, const Transform& xform) const;

private:
    std::vector<SemanticNode> m_nodes;
    int m_rootIndex = -1;
    uint64_t m_generation = 0;
    A11yActionCallback m_actionCallback;
    ReserveHook m_reserveHook;   // test-only; default is empty (zero-cost)

    // Reserve capacity for a childIndices push — invokes test hook (if set)
    // before the actual vector::reserve call.  Called before any mutation.
    void ParentChildReserve(std::vector<int>& childIndices);

    // Cycle detection: walk parent chain from childIdx up to root,
    // return true if proposedParent is found in the chain.
    [[nodiscard]] bool WouldCreateCycle(int proposedParent, int childIdx) const;
};

// Factory to build a representative demo semantic tree
class SemanticTreeFactory {
public:
    static void BuildDemo(SemanticTree& tree,
                           const char* nodeALabel = "Node A",
                           const char* nodeBLabel = "Node B",
                           const char* nodeCLabel = "Node C",
                           const char* nodeDLabel = "Node D");
};

// Window geometry needed for correct frame conversion.
struct WindowGeometry {
    int width = 0;
    int height = 0;
    float backingScaleFactor = 1.0f;
};

// Platform-neutral accessibility bridge interface.
// macOS bridge implements this; non-macOS gets a no-op.
class IAccessibilityBridge {
public:
    virtual ~IAccessibilityBridge() = default;

    // Attach to a native window (opaque pointer = SDL_Window*).
    // Returns true on success.
    virtual bool Attach(void* sdlWindow, SemanticTree* tree) = 0;

    // Detach and release all platform resources. Safe to call multiple times;
    // must clear all tree/view/native references so the bridge becomes inert.
    virtual void Detach() = 0;

    // Synchronise the live viewport transform and window geometry with the
    // accessibility bridge.  Must be called after Attach and after every
    // pan/zoom/resize/action that changes the viewport.  Native frame
    // queries use the most-recently-synchronised transform+geometry.
    virtual void SyncGeometry(const WindowGeometry& geom,
                               const Transform& viewTransform) = 0;

    // Notify the platform that a node's properties changed.
    virtual void AnnouncePropertyChange(const std::string& stableId) = 0;

    // Notify the platform that the selection changed.
    virtual void AnnounceSelectionChange() = 0;

    // Notify the platform that the focused item moved.
    virtual void AnnounceFocusedChange(const std::string& stableId) = 0;

    // Deprecated — use SyncGeometry.
    virtual void UpdateBounds() = 0;
};

// Factory: returns the platform-appropriate bridge (macOS = real; else = no-op).
std::unique_ptr<IAccessibilityBridge> CreateAccessibilityBridge();

} // namespace spike
