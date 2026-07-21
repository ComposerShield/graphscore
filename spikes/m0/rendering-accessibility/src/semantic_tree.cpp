// SPDX-License-Identifier: Apache-2.0

#include "semantic_tree.hpp"

#include <algorithm>
#include <cstdio>

namespace spike {

// --- SemanticTree ---

AddNodeResult SemanticTree::AddNode(SemanticNode node) {
    // Reject empty stable IDs
    if (node.stableId.empty()) {
        std::fprintf(stderr, "SemanticTree::AddNode: empty stableId rejected\n");
        return {-1, false};
    }

    // Reject duplicate stable IDs
    for (const auto& existing : m_nodes) {
        if (existing.stableId == node.stableId) {
            std::fprintf(stderr, "SemanticTree::AddNode: duplicate stableId '%s' rejected\n",
                         node.stableId.c_str());
            return {-1, false};
        }
    }

    int idx = static_cast<int>(m_nodes.size());
    m_nodes.push_back(std::move(node));
    ++m_generation;
    return {idx, true};
}

bool SemanticTree::WouldCreateCycle(int proposedParent, int childIdx) const {
    if (proposedParent < 0 || childIdx < 0) return false;
    int walker = proposedParent;
    for (int depth = 0; depth < static_cast<int>(m_nodes.size()) + 1; ++depth) {
        if (walker == childIdx) return true;
        if (walker < 0 || static_cast<size_t>(walker) >= m_nodes.size()) return false;
        walker = m_nodes[static_cast<size_t>(walker)].parentIndex;
        if (walker < 0) return false;
    }
    return false;
}

void SemanticTree::ParentChildReserve(std::vector<int>& childIndices) {
    size_t required = childIndices.size() + 1;
    if (required > childIndices.capacity()) {
        // Test seam: invoke hook at the exact allocation point.
        // If the hook throws (e.g. std::bad_alloc), no state has been mutated.
        if (m_reserveHook) {
            m_reserveHook();
        }
        childIndices.reserve(required);
    }
}

bool SemanticTree::SetParentChild(int parentIdx, int childIdx) {
    if (parentIdx < 0 || childIdx < 0) return false;
    size_t pu = static_cast<size_t>(parentIdx);
    size_t cu = static_cast<size_t>(childIdx);
    if (pu >= m_nodes.size() || cu >= m_nodes.size()) return false;
    if (parentIdx == childIdx) return false;

    if (WouldCreateCycle(parentIdx, childIdx)) return false;

    // Reject if child already has a non-negative parent (use Reparent for moves)
    if (m_nodes[cu].parentIndex >= 0) return false;

    // Exception-atomic: perform all potentially-throwing operations
    // BEFORE any state mutation.

    // 1. Reserve capacity on parent's childIndices — invokes test hook
    //    at the allocation point (may throw).  No state mutated yet.
    auto& parent = m_nodes[pu];
    ParentChildReserve(parent.childIndices);

    // 2. Commit non-throwing operations after reserve.
    auto& child = m_nodes[cu];
    child.parentIndex = parentIdx;
    parent.childIndices.push_back(childIdx);  // no-throw: capacity reserved above
    ++m_generation;
    return true;
}

bool SemanticTree::Reparent(int newParentIdx, int childIdx) {
    if (newParentIdx < 0 || childIdx < 0) return false;
    size_t pu = static_cast<size_t>(newParentIdx);
    size_t cu = static_cast<size_t>(childIdx);
    if (pu >= m_nodes.size() || cu >= m_nodes.size()) return false;
    if (newParentIdx == childIdx) return false;

    if (WouldCreateCycle(newParentIdx, childIdx)) return false;

    auto& child = m_nodes[cu];
    int oldParentIdx = child.parentIndex;

    // If the child is already parented to newParentIdx, nothing to do
    if (oldParentIdx == newParentIdx) return true;

    // Exception-safety: perform all potentially-allocating reserve work on the
    // new parent BEFORE altering the old parent or child.  After the reserve,
    // only non-throwing operations remain:
    //   erase from old parent, push to new parent, update child.parentIndex.

    // 1. Reserve capacity on the new parent's childIndices (may throw)
    auto& newParent = m_nodes[pu];
    if (newParent.childIndices.size() == newParent.childIndices.capacity()) {
        newParent.childIndices.reserve(newParent.childIndices.size() + 1);
    }

    // 2. Commit non-throwing changes
    // Remove from old parent
    if (oldParentIdx >= 0 && static_cast<size_t>(oldParentIdx) < m_nodes.size()) {
        auto& oldParent = m_nodes[static_cast<size_t>(oldParentIdx)];
        auto it = std::find(oldParent.childIndices.begin(),
                             oldParent.childIndices.end(), childIdx);
        if (it != oldParent.childIndices.end()) {
            oldParent.childIndices.erase(it);
        }
    }

    // Set new parent — non-throwing after reserve
    child.parentIndex = newParentIdx;
    if (std::find(newParent.childIndices.begin(), newParent.childIndices.end(), childIdx)
        == newParent.childIndices.end()) {
        newParent.childIndices.push_back(childIdx);
    }

    ++m_generation;
    return true;
}

bool SemanticTree::DetachChild(int childIdx) {
    if (childIdx < 0 || static_cast<size_t>(childIdx) >= m_nodes.size()) return false;

    auto& child = m_nodes[static_cast<size_t>(childIdx)];
    int parentIdx = child.parentIndex;
    if (parentIdx < 0) return false;

    if (static_cast<size_t>(parentIdx) < m_nodes.size()) {
        auto& parent = m_nodes[static_cast<size_t>(parentIdx)];
        auto it = std::find(parent.childIndices.begin(),
                             parent.childIndices.end(), childIdx);
        if (it != parent.childIndices.end()) {
            parent.childIndices.erase(it);
        }
    }

    child.parentIndex = -1;
    ++m_generation;
    return true;
}

void SemanticTree::Clear() {
    m_nodes.clear();
    m_rootIndex = -1;
    ++m_generation;
}

bool SemanticTree::SetRoot(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= m_nodes.size()) return false;
    m_rootIndex = idx;
    ++m_generation;
    return true;
}

int SemanticTree::FindByStableId(const std::string& id) const {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].stableId == id) return static_cast<int>(i);
    }
    return -1;
}

void SemanticTree::SetSelected(int idx, bool selected) {
    if (idx < 0 || static_cast<size_t>(idx) >= m_nodes.size()) return;
    auto& n = m_nodes[static_cast<size_t>(idx)];
    if (selected) {
        n.state = n.state | A11yState::Selected;
    } else {
        n.state = static_cast<A11yState>(
            static_cast<uint32_t>(n.state) & ~static_cast<uint32_t>(A11yState::Selected));
    }
}

void SemanticTree::SetFocused(int idx, bool focused) {
    if (idx < 0 || static_cast<size_t>(idx) >= m_nodes.size()) return;
    auto& n = m_nodes[static_cast<size_t>(idx)];
    if (focused) {
        n.state = n.state | A11yState::Focused;
    } else {
        n.state = static_cast<A11yState>(
            static_cast<uint32_t>(n.state) & ~static_cast<uint32_t>(A11yState::Focused));
    }
}

void SemanticTree::SetKeyboardFocus(int idx, bool hasFocus) {
    if (idx < 0 || static_cast<size_t>(idx) >= m_nodes.size()) return;
    m_nodes[static_cast<size_t>(idx)].hasKeyboardFocus = hasFocus;
}

void SemanticTree::SetActionCallback(A11yActionCallback cb) {
    m_actionCallback = std::move(cb);
}

bool SemanticTree::InvokeAction(int idx, const std::string& actionId) {
    if (idx < 0 || static_cast<size_t>(idx) >= m_nodes.size()) return false;
    if (!m_actionCallback) return false;

    const auto& node = m_nodes[static_cast<size_t>(idx)];

    bool found = false;
    for (const auto& a : node.actions) {
        if (a.id == actionId) {
            found = true;
            break;
        }
    }
    if (!found) return false;

    m_actionCallback(node.stableId, actionId);
    return true;
}

Rect SemanticTree::ComputeScreenBounds(int idx, const Transform& xform) const {
    if (idx < 0 || static_cast<size_t>(idx) >= m_nodes.size()) return {};
    const auto& n = m_nodes[static_cast<size_t>(idx)];
    Vec2 topLeft = xform.WorldToScreen({n.bounds.x, n.bounds.y});
    Vec2 botRight = xform.WorldToScreen({n.bounds.x + n.bounds.w, n.bounds.y + n.bounds.h});
    return {topLeft.x, topLeft.y, botRight.x - topLeft.x, botRight.y - topLeft.y};
}

int SemanticTree::HitTest(Vec2 screenPos, const Transform& xform) const {
    for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i) {
        Rect sr = ComputeScreenBounds(i, xform);
        if (sr.Contains(screenPos)) return i;
    }
    return -1;
}

// --- SemanticTreeFactory ---

void SemanticTreeFactory::BuildDemo(SemanticTree& tree,
                                     const char* nodeALabel,
                                     const char* nodeBLabel,
                                     const char* nodeCLabel,
                                     const char* nodeDLabel) {
    tree.Clear();

    // Root: application window
    auto appRes = tree.AddNode({
        "gs_app_root", A11yRole::Application, "GraphScore Spike", "", "", {}, A11yState::None});
    int appIdx = appRes.index;
    (void)tree.SetRoot(appIdx);

    // Graph nodes
    int nodeA = tree.AddNode({
        "gs_node_A", A11yRole::GraphNode, nodeALabel, "", "Graph notation node",
        {50, 50, 200, 100}}).index;
    int nodeB = tree.AddNode({
        "gs_node_B", A11yRole::GraphNode, nodeBLabel, "", "Graph notation node",
        {350, 30, 200, 80}}).index;
    int nodeC = tree.AddNode({
        "gs_node_C", A11yRole::GraphNode, nodeCLabel, "", "Graph notation node",
        {350, 160, 200, 80}}).index;
    int nodeD = tree.AddNode({
        "gs_node_D", A11yRole::GraphNode, nodeDLabel, "", "Graph notation node",
        {620, 60, 180, 100}}).index;

    tree.SetParentChild(appIdx, nodeA);
    tree.SetParentChild(appIdx, nodeB);
    tree.SetParentChild(appIdx, nodeC);
    tree.SetParentChild(appIdx, nodeD);

    // Connectors
    int connAB = tree.AddNode({
        "gs_conn_AB", A11yRole::Connector, "A→B", "", "Connector from Node A to Node B",
        {250, 85, 105, 20}}).index;
    int connBC = tree.AddNode({
        "gs_conn_BC", A11yRole::Connector, "B→C", "", "Connector from Node B to Node C",
        {150, 145, 205, 60}}).index;
    int connBD = tree.AddNode({
        "gs_conn_BD", A11yRole::Connector, "B→D", "", "Connector from Node B to Node D",
        {550, 60, 70, 55}}).index;

    tree.SetParentChild(appIdx, connAB);
    tree.SetParentChild(appIdx, connBC);
    tree.SetParentChild(appIdx, connBD);

    // Staff group
    int staffsGrp = tree.AddNode({
        "gs_staffs", A11yRole::Group, "Staves", "", "Notation staves",
        {10, 280, 780, 180}}).index;
    tree.SetParentChild(appIdx, staffsGrp);

    // Measures
    int measure1 = tree.AddNode({
        "gs_measure_1", A11yRole::Measure, "Measure 1", "4/4", "First measure, treble clef",
        {15, 280, 770, 50}}).index;
    int measure2 = tree.AddNode({
        "gs_measure_2", A11yRole::Measure, "Measure 2", "4/4", "Second measure",
        {15, 342, 770, 50}}).index;
    int measure3 = tree.AddNode({
        "gs_measure_3", A11yRole::Measure, "Measure 3", "4/4", "Third measure",
        {15, 404, 770, 50}}).index;

    tree.SetParentChild(staffsGrp, measure1);
    tree.SetParentChild(staffsGrp, measure2);
    tree.SetParentChild(staffsGrp, measure3);

    // Notes in measure 1
    int noteE4 = tree.AddNode({
        "gs_note_m1E4", A11yRole::Note, "E4", "quarter", "Quarter note E4",
        {80, 280, 20, 15}}).index;
    int noteF4 = tree.AddNode({
        "gs_note_m1F4", A11yRole::Note, "F4", "quarter", "Quarter note F4",
        {110, 280, 20, 15}}).index;
    int noteG4 = tree.AddNode({
        "gs_note_m1G4", A11yRole::Note, "G4", "quarter", "Quarter note G4",
        {140, 280, 20, 15}}).index;
    int noteA4_m1 = tree.AddNode({
        "gs_note_m1A4", A11yRole::Note, "A4", "quarter", "Quarter note A4",
        {170, 280, 20, 15}}).index;
    int noteG4_2 = tree.AddNode({
        "gs_note_m1G4_2", A11yRole::Note, "G4", "quarter", "Quarter note G4",
        {200, 280, 20, 15}}).index;

    tree.SetParentChild(measure1, noteE4);
    tree.SetParentChild(measure1, noteF4);
    tree.SetParentChild(measure1, noteG4);
    tree.SetParentChild(measure1, noteA4_m1);
    tree.SetParentChild(measure1, noteG4_2);

    // Notes in measure 2
    int noteB4 = tree.AddNode({
        "gs_note_m2B4", A11yRole::Note, "B\u266D", "quarter", "Quarter note B-flat",
        {75, 342, 25, 15}}).index;
    int noteF5 = tree.AddNode({
        "gs_note_m2F5", A11yRole::Note, "F\u266F5", "quarter", "Quarter note F-sharp",
        {115, 342, 25, 15}}).index;

    tree.SetParentChild(measure2, noteB4);
    tree.SetParentChild(measure2, noteF5);

    // Notes in measure 3
    int noteHalf_m3 = tree.AddNode({
        "gs_note_m3C5h", A11yRole::Note, "C5", "half", "Half note C5",
        {80, 404, 20, 15}}).index;

    tree.SetParentChild(measure3, noteHalf_m3);

    // Controls
    int zoomCtrl = tree.AddNode({
        "gs_ctrl_zoom", A11yRole::Control, "Zoom", "1.0x", "Canvas zoom control",
        {740, 5, 40, 25}}).index;
    tree.SetParentChild(appIdx, zoomCtrl);

    // Selection placeholder
    int selection = tree.AddNode({
        "gs_selection", A11yRole::Selection, "Current selection", "none",
        "Current selection placeholder", {}, A11yState::None}).index;
    tree.SetParentChild(appIdx, selection);

    // Actionable items
    int playBtn = tree.AddNode({
        "gs_action_play", A11yRole::ActionItem, "Play", "", "Start playback",
        {700, 5, 35, 25}}).index;
    tree.SetParentChild(appIdx, playBtn);

    // Add actions to nodes
    tree.Node(nodeA).actions.push_back({"select", "Select this node"});
    tree.Node(nodeB).actions.push_back({"select", "Select this node"});
    tree.Node(nodeC).actions.push_back({"select", "Select this node"});
    tree.Node(nodeD).actions.push_back({"select", "Select this node"});
    tree.Node(playBtn).actions.push_back({"press", "Press playback button"});
    tree.Node(zoomCtrl).actions.push_back({"increment", "Zoom in"});
    tree.Node(zoomCtrl).actions.push_back({"decrement", "Zoom out"});
}

} // namespace spike
