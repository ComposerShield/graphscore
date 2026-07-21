// SPDX-License-Identifier: Apache-2.0

#include "accessibility_bridge.hpp"
#include "semantic_tree.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace spike {

// Write a platform-neutral accessibility tree dump to a report file.
static void DumpSemanticTree(FILE* f, const SemanticTree& tree, int idx,
                              int depth, const Transform& xform) {
    if (idx < 0 || idx >= tree.NodeCount()) return;

    const auto& n = tree.Node(idx);
    Rect sb = tree.ComputeScreenBounds(idx, xform);

    for (int d = 0; d < depth; ++d) std::fprintf(f, "  ");

    std::fprintf(f, "+- [%s] id=%s role=",
                 n.hasKeyboardFocus ? "FOCUSED" :
                 (n.state & A11yState::Selected) ? "SELECTED" : " ",
                 n.stableId.c_str());

    const char* roleStr = "Unknown";
    switch (n.role) {
    case A11yRole::Application: roleStr = "Application"; break;
    case A11yRole::Window:      roleStr = "Window"; break;
    case A11yRole::Group:       roleStr = "Group"; break;
    case A11yRole::GraphNode:   roleStr = "GraphNode"; break;
    case A11yRole::Connector:   roleStr = "Connector"; break;
    case A11yRole::Measure:     roleStr = "Measure"; break;
    case A11yRole::Note:        roleStr = "Note"; break;
    case A11yRole::Control:     roleStr = "Control"; break;
    case A11yRole::Selection:   roleStr = "Selection"; break;
    case A11yRole::ActionItem:  roleStr = "ActionItem"; break;
    case A11yRole::Label:       roleStr = "Label"; break;
    case A11yRole::Staff:       roleStr = "Staff"; break;
    default: break;
    }

    std::fprintf(f, "%s name=\"%s\"", roleStr, n.name.c_str());
    if (!n.value.empty()) {
        std::fprintf(f, " value=\"%s\"", n.value.c_str());
    }
    std::fprintf(f, " frame=(%.0f,%.0f %.0fx%.0f)", sb.x, sb.y, sb.w, sb.h);

    if (n.state & A11yState::Selected) std::fprintf(f, " selected");
    if (n.state & A11yState::Focused) std::fprintf(f, " focused");
    if (n.state & A11yState::Disabled) std::fprintf(f, " disabled");
    if (n.hasKeyboardFocus) std::fprintf(f, " keyboardFocus");

    if (!n.description.empty()) {
        std::fprintf(f, " desc=\"%s\"", n.description.c_str());
    }

    if (!n.actions.empty()) {
        std::fprintf(f, " actions=[");
        for (size_t ai = 0; ai < n.actions.size(); ++ai) {
            if (ai > 0) std::fprintf(f, ", ");
            std::fprintf(f, "%s", n.actions[ai].id.c_str());
        }
        std::fprintf(f, "]");
    }

    std::fprintf(f, "\n");

    for (int child : n.childIndices) {
        DumpSemanticTree(f, tree, child, depth + 1, xform);
    }
}

int RunAccessibilityReport(void* sdlWindow, SemanticTree* tree,
                            const char* reportPath) {
    if (!tree) return 1;

    FILE* f = std::fopen(reportPath, "w");
    if (!f) {
        std::fprintf(stderr, "[a11y-report] Cannot open report path: %s\n", reportPath);
        return 1;
    }

    std::fprintf(f, "# GraphScore Accessibility Report\n");
    std::fprintf(f, "## Semantic Tree\n\n");
    std::fprintf(f, "Format: [STATE] id=<stableId> role=<Role> name=\"<Name>\""
                  " frame=(x,y wxh) [states] [desc=\"...\"] [actions=[...]]\n\n");

    Transform idXf;
    if (tree->RootIndex() >= 0) {
        DumpSemanticTree(f, *tree, tree->RootIndex(), 0, idXf);
    }

    std::fprintf(f, "\n## Action Test\n\n");

    // Test: select Node A, then deselect it
    int nodeA = tree->FindByStableId("gs_node_A");
    if (nodeA >= 0) {
        std::fprintf(f, "Invoking 'select' on gs_node_A...\n");
        tree->SetSelected(nodeA, true);
        tree->SetFocused(nodeA, true);
        tree->SetKeyboardFocus(nodeA, true);
        const auto& na = tree->Node(nodeA);
        std::fprintf(f, "  After: selected=%d focused=%d keyboardFocus=%d\n",
                     na.state & A11yState::Selected ? 1 : 0,
                     na.state & A11yState::Focused ? 1 : 0,
                     na.hasKeyboardFocus ? 1 : 0);

        tree->SetSelected(nodeA, false);
        tree->SetFocused(nodeA, false);
        tree->SetKeyboardFocus(nodeA, false);
        std::fprintf(f, "  After deselect: selected=%d focused=%d keyboardFocus=%d\n",
                     na.state & A11yState::Selected ? 1 : 0,
                     na.state & A11yState::Focused ? 1 : 0,
                     na.hasKeyboardFocus ? 1 : 0);
    }

    // Test: invoke action on play button
    int playBtn = tree->FindByStableId("gs_action_play");
    if (playBtn >= 0) {
        std::fprintf(f, "Invoking 'press' on gs_action_play...\n");
        tree->InvokeAction(playBtn, "press");
    }

    std::fprintf(f, "\n## Hit Test\n\n");
    Transform hitXf;
    Vec2 testPoints[] = {
        {150, 100},    // Inside Node A
        {400, 70},     // Inside Node B
        {300, 110},    // On connector A-B
        {700, 15},     // Near Play button
        {80, 290},     // Inside measure 1
        {85, 287},     // Inside note E4
    };
    for (const auto& pt : testPoints) {
        int hit = tree->HitTest(pt, hitXf);
        if (hit >= 0) {
            const auto& nh = tree->Node(hit);
            std::fprintf(f, "HitTest(%.0f,%.0f) -> %s (%s)\n",
                         pt.x, pt.y, nh.stableId.c_str(), nh.name.c_str());
        } else {
            std::fprintf(f, "HitTest(%.0f,%.0f) -> (none)\n",
                         pt.x, pt.y);
        }
    }

    std::fprintf(f, "\n## Transform Bounds Test\n\n");
    Transform zoomXf{2.0f, {100, 50}};
    for (int i = 0; i < tree->NodeCount(); ++i) {
        const auto& n = tree->Node(i);
        if (n.role == A11yRole::GraphNode) {
            Rect sb = tree->ComputeScreenBounds(i, zoomXf);
            std::fprintf(f, "%s (%s): identity=(%.0f,%.0f %.0fx%.0f)"
                          " zoomed=(%.0f,%.0f %.0fx%.0f)\n",
                          n.stableId.c_str(), n.name.c_str(),
                          n.bounds.x, n.bounds.y, n.bounds.w, n.bounds.h,
                          sb.x, sb.y, sb.w, sb.h);
        }
    }

    int overallResult = 0;

#ifdef __APPLE__
    std::fprintf(f, "\n## Native macOS NSAccessibility Bridge Test\n\n");
    int macResult = RunAccessibilityBridgeMacReport(sdlWindow, tree);
    if (macResult != 0) {
        std::fprintf(stderr, "[a11y-report] macOS bridge test failed (exit code %d)\n", macResult);
        overallResult = macResult;
    }
    std::fprintf(f, "macOS bridge test: %s\n", macResult == 0 ? "PASSED" : "FAILED");
#else
    std::fprintf(f, "\n## Platform Bridge\n\n");
    std::fprintf(f, "Non-macOS platform: native bridge test skipped.\n");
#endif

    std::fclose(f);
    return overallResult;
}

} // namespace spike
