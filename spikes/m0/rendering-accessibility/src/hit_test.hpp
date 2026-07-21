// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include "graph_renderer.hpp"
#include <vector>
#include <string>

namespace spike {

struct HitResult {
    bool hit = false;
    std::string targetType;  // "node", "connector", "staff"
    int targetIndex = -1;
    Vec2 worldPos;
    Vec2 screenPos;
};

class HitTester {
public:
    void ClearTargets() { m_nodes.clear(); m_connectors.clear(); m_staffRects.clear(); }

    void AddNode(const GraphNode& node) { m_nodes.push_back(node); }
    void AddConnector(const GraphConnector& conn) { m_connectors.push_back(conn); }
    void AddStaffRect(Rect r) { m_staffRects.push_back(r); }

    // Test a screen-space point against registered targets
    HitResult Test(Vec2 screenPos, const Transform& xform) const;

    // Helper: build hit-test targets from demo data
    static void PopulateFromDemo(HitTester& ht);

private:
    std::vector<GraphNode> m_nodes;
    std::vector<GraphConnector> m_connectors;
    std::vector<Rect> m_staffRects;
};

} // namespace spike
