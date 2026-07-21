// SPDX-License-Identifier: Apache-2.0

#include "hit_test.hpp"
#include <cmath>
#include <cstdio>

namespace spike {

HitResult HitTester::Test(Vec2 screenPos, const Transform& xform) const {
    Vec2 wp = xform.ScreenToWorld(screenPos);
    HitResult result;
    result.screenPos = screenPos;
    result.worldPos = wp;

    // Test nodes (render order: last drawn on top)
    for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i) {
        if (m_nodes[static_cast<size_t>(i)].bounds.Contains(wp)) {
            result.hit = true;
            result.targetType = "node";
            result.targetIndex = i;
            return result;
        }
    }

    // Test connectors (approximate: distance to nearest segment)
    for (size_t i = 0; i < m_connectors.size(); ++i) {
        const auto& c = m_connectors[i];
        std::vector<Vec2> pts;
        pts.push_back(c.start);
        for (auto& b : c.bends) pts.push_back(b);
        pts.push_back(c.end);

        for (size_t j = 0; j + 1 < pts.size(); ++j) {
            Vec2 a = pts[j];
            Vec2 b = pts[j + 1];
            // Point-to-segment distance
            Vec2 ab = {b.x - a.x, b.y - a.y};
            float len2 = ab.x * ab.x + ab.y * ab.y;
            if (len2 < 1e-6f) {
                float d = std::sqrt((wp.x - a.x) * (wp.x - a.x) +
                                    (wp.y - a.y) * (wp.y - a.y));
                if (d < c.lineWidth + 5.0f) {
                    result.hit = true;
                    result.targetType = "connector";
                    result.targetIndex = static_cast<int>(i);
                    return result;
                }
                continue;
            }
            float t = std::max(0.0f, std::min(1.0f,
                ((wp.x - a.x) * ab.x + (wp.y - a.y) * ab.y) / len2));
            Vec2 proj = {a.x + t * ab.x, a.y + t * ab.y};
            float d = std::sqrt((wp.x - proj.x) * (wp.x - proj.x) +
                                (wp.y - proj.y) * (wp.y - proj.y));
            float threshold = c.lineWidth + 5.0f;
            if (d < threshold) {
                result.hit = true;
                result.targetType = "connector";
                result.targetIndex = static_cast<int>(i);
                return result;
            }
        }
    }

    // Test staff rects
    for (size_t i = 0; i < m_staffRects.size(); ++i) {
        if (m_staffRects[i].Contains(wp)) {
            result.hit = true;
            result.targetType = "staff";
            result.targetIndex = static_cast<int>(i);
            return result;
        }
    }

    return result;
}

void HitTester::PopulateFromDemo(HitTester& ht) {
    for (auto& n : GraphRenderer::DemoNodes()) ht.AddNode(n);
    for (auto& c : GraphRenderer::DemoConnectors()) ht.AddConnector(c);
    // Staff regions (world coords)
    ht.AddStaffRect({10, 10, 780, 40});
    ht.AddStaffRect({10, 72, 780, 40});
    ht.AddStaffRect({10, 134, 780, 40});
}

} // namespace spike
