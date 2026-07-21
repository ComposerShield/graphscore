// SPDX-License-Identifier: Apache-2.0

#include "rasterizer.hpp"
#include <cstring>
#include <algorithm>

namespace spike {

static uint32_t PackColor(Color c) {
    return (static_cast<uint32_t>(c.a) << 24) |
           (static_cast<uint32_t>(c.b) << 16) |
           (static_cast<uint32_t>(c.g) << 8) |
           static_cast<uint32_t>(c.r);
}

static uint32_t BlendOver(uint32_t dst, uint32_t src) {
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0) return dst;
    if (sa == 255) return src;

    uint32_t sr = (src >> 0) & 0xFF;
    uint32_t sg = (src >> 8) & 0xFF;
    uint32_t sb = (src >> 16) & 0xFF;

    uint32_t da = (dst >> 24) & 0xFF;
    uint32_t dr = (dst >> 0) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = (dst >> 16) & 0xFF;

    uint32_t outA = sa + ((da * (255 - sa)) / 255);
    uint32_t outR = (sr * sa + dr * (255 - sa)) / 255;
    uint32_t outG = (sg * sa + dg * (255 - sa)) / 255;
    uint32_t outB = (sb * sa + db * (255 - sa)) / 255;

    return (outA << 24) | (outB << 16) | (outG << 8) | outR;
}

Rasterizer::Rasterizer(int width, int height, const std::vector<GlyphBitmap>& glyphCache)
    : m_width(width), m_height(height),
      m_clip{0, 0, static_cast<float>(width), static_cast<float>(height)},
      m_buffer(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFFFFFFFF),
      m_glyphCache(&glyphCache)
{
    // Glyph cache must never be null — guaranteed by requiring a reference argument.
}

Rasterizer::~Rasterizer() = default;

void Rasterizer::Clear(Color c) {
    uint32_t v = PackColor(c);
    std::fill(m_buffer.begin(), m_buffer.end(), v);
}

void Rasterizer::SetClip(Rect r) {
    m_clip = r.Intersect({0, 0, static_cast<float>(m_width), static_cast<float>(m_height)});
}

void Rasterizer::FillRectPixels(int x0, int y0, int x1, int y1, Color c) {
    int cx0 = std::max(x0, static_cast<int>(m_clip.x));
    int cy0 = std::max(y0, static_cast<int>(m_clip.y));
    int cx1 = std::min(x1, static_cast<int>(m_clip.x + m_clip.w));
    int cy1 = std::min(y1, static_cast<int>(m_clip.y + m_clip.h));

    if (cx0 >= cx1 || cy0 >= cy1) return;

    uint32_t pc = PackColor(c);
    for (int y = cy0; y < cy1; ++y) {
        size_t row = static_cast<size_t>(y) * static_cast<size_t>(m_width);
        for (int x = cx0; x < cx1; ++x) {
            m_buffer[row + static_cast<size_t>(x)] = BlendOver(
                m_buffer[row + static_cast<size_t>(x)], pc);
        }
    }
}

void Rasterizer::FillCirclePixels(float cx, float cy, float r, Color c) {
    int x0 = static_cast<int>(cx - r) - 1;
    int y0 = static_cast<int>(cy - r) - 1;
    int x1 = static_cast<int>(cx + r) + 1;
    int y1 = static_cast<int>(cy + r) + 1;

    int cx0 = std::max(x0, static_cast<int>(m_clip.x));
    int cy0 = std::max(y0, static_cast<int>(m_clip.y));
    int cx1 = std::min(x1, static_cast<int>(m_clip.x + m_clip.w));
    int cy1 = std::min(y1, static_cast<int>(m_clip.y + m_clip.h));

    uint32_t pc = PackColor(c);
    float r2 = r * r;

    for (int y = cy0; y < cy1; ++y) {
        size_t row = static_cast<size_t>(y) * static_cast<size_t>(m_width);
        float dy = static_cast<float>(y) - cy + 0.5f;
        float dy2 = dy * dy;
        for (int x = cx0; x < cx1; ++x) {
            float dx = static_cast<float>(x) - cx + 0.5f;
            if (dx * dx + dy2 <= r2) {
                m_buffer[row + static_cast<size_t>(x)] = BlendOver(
                    m_buffer[row + static_cast<size_t>(x)], pc);
            }
        }
    }
}

void Rasterizer::StrokeLinePixels(float x1, float y1, float x2, float y2,
                                   float width, Color c) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) {
        FillCirclePixels(x1, y1, width * 0.5f, c);
        return;
    }

    float nSteps = len / 1.0f;
    for (float t = 0.0f; t <= nSteps; t += 1.0f) {
        float frac = t / nSteps;
        float px = x1 + dx * frac;
        float py = y1 + dy * frac;
        FillCirclePixels(px, py, width * 0.5f, c);
    }
    FillCirclePixels(x1, y1, width * 0.5f, c);
    FillCirclePixels(x2, y2, width * 0.5f, c);
}

void Rasterizer::FillArcPixels(float cx, float cy, float outerR, float innerR,
                                ArcAngle arc, Color c) {
    int x0 = static_cast<int>(cx - outerR) - 1;
    int y0 = static_cast<int>(cy - outerR) - 1;
    int x1 = static_cast<int>(cx + outerR) + 1;
    int y1 = static_cast<int>(cy + outerR) + 1;

    int cx0 = std::max(x0, static_cast<int>(m_clip.x));
    int cy0 = std::max(y0, static_cast<int>(m_clip.y));
    int cx1 = std::min(x1, static_cast<int>(m_clip.x + m_clip.w));
    int cy1 = std::min(y1, static_cast<int>(m_clip.y + m_clip.h));

    uint32_t pc = PackColor(c);
    float outerR2 = outerR * outerR;
    float innerR2 = innerR * innerR;

    // Normalise arc interval to [0, 2π) and determine shortest sweep.
    constexpr float k2Pi = 2.0f * 3.14159265358979f;
    float a0 = std::fmod(arc.start, k2Pi);
    if (a0 < 0.0f) a0 += k2Pi;
    float a1 = std::fmod(arc.end, k2Pi);
    if (a1 < 0.0f) a1 += k2Pi;

    float sweep = a1 - a0;
    if (sweep > 3.14159265f) sweep -= k2Pi;
    else if (sweep < -3.14159265f) sweep += k2Pi;

    float aStart = a0;
    float aEnd = a0 + sweep;

    for (int y = cy0; y < cy1; ++y) {
        size_t row = static_cast<size_t>(y) * static_cast<size_t>(m_width);
        float dy = static_cast<float>(y) - cy + 0.5f;
        float dy2 = dy * dy;
        for (int x = cx0; x < cx1; ++x) {
            float dx = static_cast<float>(x) - cx + 0.5f;
            float d2 = dx * dx + dy2;
            if (d2 > outerR2 || d2 < innerR2) continue;

            float ang = std::atan2(dy, dx);
            if (ang < 0.0f) ang += k2Pi;

            // Signed sweep containment: normalise (ang - aStart) to [0, 2π).
            // For positive sweep, inside iff diff ≤ sweep.
            // For negative sweep, inside iff (aStart - ang) normalised ≤ -sweep.
            bool inArc = false;
            float diff = ang - aStart;
            if (diff < 0.0f) diff += k2Pi;

            if (sweep >= 0.0f) {
                inArc = (diff <= sweep);
            } else {
                float negDiff = aStart - ang;
                if (negDiff < 0.0f) negDiff += k2Pi;
                inArc = (negDiff <= -sweep);
            }

            if (inArc) {
                m_buffer[row + static_cast<size_t>(x)] = BlendOver(
                    m_buffer[row + static_cast<size_t>(x)], pc);
            }
        }
    }
}

void Rasterizer::BlitGlyphPixels(float x, float y, Color c, int glyphIndex) {
    if (glyphIndex < 0 || static_cast<size_t>(glyphIndex) >= m_glyphCache->size()) {
        std::fprintf(stderr, "Rasterizer: invalid glyph cache index %d (cache size %zu)\n",
                     glyphIndex, m_glyphCache->size());
        return;
    }
    const auto& gb = (*m_glyphCache)[static_cast<size_t>(glyphIndex)];
    if (gb.buffer.empty()) return;

    int gx = static_cast<int>(x) + gb.left;
    int gy = static_cast<int>(y) - gb.top;

    int cx0 = std::max(gx, static_cast<int>(m_clip.x));
    int cy0 = std::max(gy, static_cast<int>(m_clip.y));
    int cx1 = std::min(gx + gb.width, static_cast<int>(m_clip.x + m_clip.w));
    int cy1 = std::min(gy + gb.height, static_cast<int>(m_clip.y + m_clip.h));

    for (int py = cy0; py < cy1; ++py) {
        int srcY = py - gy;
        if (srcY < 0 || srcY >= gb.height) continue;
        size_t srcRow = static_cast<size_t>(srcY) * static_cast<size_t>(gb.width);
        size_t dstRow = static_cast<size_t>(py) * static_cast<size_t>(m_width);

        for (int px = cx0; px < cx1; ++px) {
            int srcX = px - gx;
            if (srcX < 0 || srcX >= gb.width) continue;
            uint8_t alpha = gb.buffer[srcRow + static_cast<size_t>(srcX)];
            if (alpha == 0) continue;

            uint32_t src = PackColor({c.r, c.g, c.b, alpha});
            m_buffer[dstRow + static_cast<size_t>(px)] = BlendOver(
                m_buffer[dstRow + static_cast<size_t>(px)], src);
        }
    }
}

void Rasterizer::Execute(const RenderCommand& cmd) {
    switch (cmd.type) {
    case CmdType::FillRect: {
        auto& fr = std::get<CmdFillRect>(cmd.data);
        FillRectPixels(
            static_cast<int>(fr.rect.x),
            static_cast<int>(fr.rect.y),
            static_cast<int>(fr.rect.x + fr.rect.w),
            static_cast<int>(fr.rect.y + fr.rect.h),
            fr.color);
        break;
    }
    case CmdType::FillCircle: {
        auto& fc = std::get<CmdFillCircle>(cmd.data);
        FillCirclePixels(fc.cx, fc.cy, fc.radius, fc.color);
        break;
    }
    case CmdType::StrokeLine: {
        auto& sl = std::get<CmdStrokeLine>(cmd.data);
        StrokeLinePixels(sl.x1, sl.y1, sl.x2, sl.y2, sl.width, sl.color);
        break;
    }
    case CmdType::BlitGlyph: {
        auto& bg = std::get<CmdBlitGlyph>(cmd.data);
        BlitGlyphPixels(bg.x, bg.y, bg.color, bg.glyphIndex);
        break;
    }
    case CmdType::SetClip: {
        auto& sc = std::get<CmdSetClip>(cmd.data);
        SetClip(sc.rect);
        break;
    }
    case CmdType::FillArc: {
        auto& fa = std::get<CmdFillArc>(cmd.data);
        FillArcPixels(fa.cx, fa.cy, fa.radius, fa.innerRadius, fa.arc, fa.color);
        break;
    }
    }
}

void Rasterizer::Execute(const RenderList& list) {
    for (const auto& cmd : list.Commands()) {
        Execute(cmd);
    }
}

bool Rasterizer::SavePPM(const char* path) const {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    if (std::fprintf(f, "P6\n%d %d\n255\n", m_width, m_height) < 0) {
        std::fclose(f);
        return false;
    }

    std::vector<uint8_t> row(static_cast<size_t>(m_width) * 3);
    for (int y = 0; y < m_height; ++y) {
        size_t off = static_cast<size_t>(y) * static_cast<size_t>(m_width);
        for (int x = 0; x < m_width; ++x) {
            uint32_t px = m_buffer[off + static_cast<size_t>(x)];
            size_t rOff = static_cast<size_t>(x) * 3;
            row[rOff + 0] = static_cast<uint8_t>(px & 0xFF);
            row[rOff + 1] = static_cast<uint8_t>((px >> 8) & 0xFF);
            row[rOff + 2] = static_cast<uint8_t>((px >> 16) & 0xFF);
        }
        if (std::fwrite(row.data(), 1, row.size(), f) != row.size()) {
            std::fclose(f);
            return false;
        }
    }
    return std::fclose(f) == 0;
}

uint64_t Rasterizer::PixelHash() const {
    uint64_t hash = 14695981039346656037ULL;
    for (auto px : m_buffer) {
        hash ^= static_cast<uint64_t>(px);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace spike
