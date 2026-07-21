// SPDX-License-Identifier: Apache-2.0

#include "notation_renderer.hpp"

namespace spike {

void NotationRenderer::EnsureGlyphsCached(int pixelSize) {
    if (m_clefGlyphIdx < 0 && m_fontManager && m_fontManager->HasBravura()) {
        m_clefGlyphIdx = m_fontManager->RasterizeGlyph(SMuFL::GClef, pixelSize);
        m_noteheadBlackIdx = m_fontManager->RasterizeGlyph(SMuFL::NoteheadBlack, pixelSize);
        m_noteheadHalfIdx = m_fontManager->RasterizeGlyph(SMuFL::NoteheadHalf, pixelSize);
        m_sharpIdx = m_fontManager->RasterizeGlyph(SMuFL::AccidentalSharp, pixelSize);
        m_flatIdx = m_fontManager->RasterizeGlyph(SMuFL::AccidentalFlat, pixelSize);
    }
}

void NotationRenderer::RenderStaff(RenderList& rl, float x, float y, float width,
                                    float lineSpacing, const Transform& xform) {
    float sx = xform.WorldToScreen({x, y}).x;
    float sy = xform.WorldToScreen({x, y}).y;
    float sw = width * xform.scale;
    float sls = lineSpacing * xform.scale;

    rl.AddStaffLines(sx, sy, sw, 5, sls, Color::Black());
}

void NotationRenderer::RenderTrebleClef(RenderList& rl, float x, float y,
                                         float lineSpacing) {
    EnsureGlyphsCached(64);
    if (m_clefGlyphIdx < 0) return;
    // Treble clef sits around the G line (2nd line = position 2)
    // The glyph reference point: top of staff minus some offset
    float gy = y - lineSpacing * 2.5f;
    rl.Add(RenderCommand::BlitGlyph(x, gy, Color::Black(), m_clefGlyphIdx));
}

void NotationRenderer::RenderNotehead(RenderList& rl, float x, float staffY,
                                       float lineSpacing, int step, bool filled) {
    EnsureGlyphsCached(32);
    int idx = filled ? m_noteheadBlackIdx : m_noteheadHalfIdx;
    if (idx < 0) return;

    float yOff = SMuFL::StepToYOffset(step, lineSpacing);
    // Notehead is centered on the line/space
    rl.Add(RenderCommand::BlitGlyph(x, staffY + yOff, Color::Black(), idx));
}

void NotationRenderer::RenderDemoStaves(RenderList& rl, float x, float y, float width,
                                         float lineSpacing, const Transform& xform) {
    EnsureGlyphsCached(64);

    auto GX = [&](float wx, float wy) -> Vec2 { return xform.WorldToScreen({wx, wy}); };

    // Staff 1: Treble clef with stepwise ascending notes
    float sy1 = y;
    RenderStaff(rl, x, sy1, width, lineSpacing, xform);
    if (m_clefGlyphIdx >= 0) {
        Vec2 p = GX(x + 5.0f, sy1 - lineSpacing * 2.5f);
        rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), m_clefGlyphIdx));
    }
    for (int i = 0; i < 5; ++i) {
        int step = i * 2;
        float wx = x + 60.0f + static_cast<float>(i) * 30.0f;
        float wy = sy1 + SMuFL::StepToYOffset(step, lineSpacing);
        Vec2 p = GX(wx, wy);
        int idx = m_noteheadBlackIdx;
        if (idx >= 0) rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), idx));
    }

    // Staff 2: Treble clef with a flat and sharp
    float sy2 = y + lineSpacing * 8.0f;
    RenderStaff(rl, x, sy2, width, lineSpacing, xform);
    if (m_clefGlyphIdx >= 0) {
        Vec2 p = GX(x + 5.0f, sy2 - lineSpacing * 2.5f);
        rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), m_clefGlyphIdx));
    }
    if (m_flatIdx >= 0) {
        Vec2 p = GX(x + 55.0f, sy2 + lineSpacing * 0.5f);
        rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), m_flatIdx));
    }
    {
        Vec2 p = GX(x + 70.0f, sy2 + SMuFL::StepToYOffset(2, lineSpacing));
        if (m_noteheadBlackIdx >= 0) rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), m_noteheadBlackIdx));
    }
    if (m_sharpIdx >= 0) {
        Vec2 p = GX(x + 95.0f, sy2 + lineSpacing * 2.0f);
        rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), m_sharpIdx));
    }
    {
        Vec2 p = GX(x + 110.0f, sy2 + SMuFL::StepToYOffset(6, lineSpacing));
        if (m_noteheadBlackIdx >= 0) rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), m_noteheadBlackIdx));
    }

    // Staff 3: Some notes with a half notehead
    float sy3 = y + lineSpacing * 16.0f;
    RenderStaff(rl, x, sy3, width, lineSpacing, xform);
    if (m_clefGlyphIdx >= 0) {
        Vec2 p = GX(x + 5.0f, sy3 - lineSpacing * 2.5f);
        rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), m_clefGlyphIdx));
    }
    for (int i = 0; i < 4; ++i) {
        int steps[4] = {4, 5, 3, 4};
        bool filled[4] = {true, false, true, false};
        float wx = x + 60.0f + static_cast<float>(i) * 30.0f;
        float wy = sy3 + SMuFL::StepToYOffset(steps[i], lineSpacing);
        Vec2 p = GX(wx, wy);
        int idx = filled[i] ? m_noteheadBlackIdx : m_noteheadHalfIdx;
        if (idx >= 0) rl.Add(RenderCommand::BlitGlyph(p.x, p.y, Color::Black(), idx));
    }
}

} // namespace spike
