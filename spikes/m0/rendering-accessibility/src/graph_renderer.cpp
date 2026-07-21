// SPDX-License-Identifier: Apache-2.0

#include "graph_renderer.hpp"
#include "font_manager.hpp"
#include <cmath>

namespace spike {

void GraphRenderer::RenderNode(RenderList& rl, const GraphNode& node,
                                const Transform& xform) {
    Rect sr;
    sr.x = xform.WorldToScreen({node.bounds.x, node.bounds.y}).x;
    sr.y = xform.WorldToScreen({node.bounds.x, node.bounds.y}).y;
    sr.w = node.bounds.w * xform.scale;
    sr.h = node.bounds.h * xform.scale;
    float cr = node.cornerRadius * xform.scale;

    rl.AddFilledRoundedRect(sr, cr, node.fillColor);

    // Draw border
    float onePixel = 1.0f;
    rl.AddFilledRoundedRect(sr, cr, Color::Black());
    // Inner fill to simulate border
    rl.AddFilledRoundedRect(
        {sr.x + onePixel, sr.y + onePixel,
         sr.w - 2.0f * onePixel, sr.h - 2.0f * onePixel},
        cr - onePixel, node.fillColor);

    // Render shaped text label
    if (m_fontManager && m_fontManager->HasTextFont() && !node.label.empty()) {
        int fontSize = std::max(6, static_cast<int>(12.0f * xform.scale));
        const auto& glyphs = m_fontManager->ShapeAndCacheText(
            node.label.c_str(), fontSize);

        if (!glyphs.empty()) {
            float cursorX = sr.x + cr + 4.0f;
            float cursorY = sr.y + sr.h * 0.5f + static_cast<float>(fontSize) * 0.35f;

            for (const auto& pg : glyphs) {
                if (pg.cacheIndex < 0) {
                    cursorX += pg.xAdvance * xform.scale;
                    continue;
                }
                float gx = cursorX + pg.xOffset * xform.scale;
                float gy = cursorY - pg.yOffset * xform.scale;
                rl.Add(RenderCommand::BlitGlyph(gx, gy, Color::Black(),
                                                 pg.cacheIndex));
                cursorX += pg.xAdvance * xform.scale;
            }
        }
    }
}

void GraphRenderer::RenderConnector(RenderList& rl, const GraphConnector& conn,
                                     const Transform& xform) {
    Vec2 ss = xform.WorldToScreen(conn.start);
    Vec2 se = xform.WorldToScreen(conn.end);
    std::vector<Vec2> sbends;
    for (auto& b : conn.bends) {
        sbends.push_back(xform.WorldToScreen(b));
    }
    float clw = conn.lineWidth * xform.scale;
    float ccr = conn.cornerRadius * xform.scale;

    rl.AddOrthogonalConnector(ss, se, sbends, clw, ccr, conn.color);
}

std::vector<GraphNode> GraphRenderer::DemoNodes() {
    return {
        { {50, 50, 200, 100}, Color{220, 240, 255, 255}, "Node A", 12.0f },
        { {350, 30, 200, 80}, Color{255, 240, 220, 255}, "Node B", 10.0f },
        { {350, 160, 200, 80}, Color{220, 255, 220, 255}, "Node C", 10.0f },
        { {620, 60, 180, 100}, Color{240, 220, 255, 255}, "Node D", 14.0f },
    };
}

std::vector<GraphConnector> GraphRenderer::DemoConnectors() {
    return {
        // A → B: orthogonal with two bends (all axis-aligned)
        {
            {250, 100}, {350, 70},
            {{300, 100}, {300, 70}},
            Color{80, 80, 80, 255}, 3.0f, 8.0f
        },
        // A → C: orthogonal, horizontal-then-vertical
        {
            {150, 150}, {350, 200},
            {{250, 150}, {250, 200}},
            Color{80, 80, 80, 255}, 3.0f, 8.0f
        },
        // B → D: orthogonal, horizontal-then-vertical
        {
            {550, 70}, {620, 110},
            {{585, 70}, {585, 110}},
            Color{120, 120, 120, 255}, 2.5f, 6.0f
        },
        // C → D: orthogonal, vertical-then-horizontal
        {
            {550, 200}, {710, 110},
            {{550, 110}},
            Color{120, 120, 120, 255}, 2.5f, 6.0f
        },
    };
}

} // namespace spike
