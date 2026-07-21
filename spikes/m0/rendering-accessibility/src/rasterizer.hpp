// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include "render_list.hpp"
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstddef>

namespace spike {

class Rasterizer {
public:
    // Construction requires a non-null glyph cache reference.
    // The cache must outlive the Rasterizer.
    Rasterizer(int width, int height, const std::vector<GlyphBitmap>& glyphCache);
    ~Rasterizer();

    int Width() const { return m_width; }
    int Height() const { return m_height; }
    const uint32_t* Pixels() const { return m_buffer.data(); }
    uint32_t* Pixels() { return m_buffer.data(); }
    size_t PixelCount() const { return m_buffer.size(); }

    void Clear(Color c);

    // Execute a single command
    void Execute(const RenderCommand& cmd);

    // Execute a whole render list
    void Execute(const RenderList& list);

    // Save as PPM (P6 binary format)
    bool SavePPM(const char* path) const;

    // Compute a simple hash of the pixel buffer for golden tests
    uint64_t PixelHash() const;

    // Set current clip rect (in pixel coordinates)
    void SetClip(Rect r);

    // Access the glyph cache (read-only, for test inspection)
    const std::vector<GlyphBitmap>& GlyphCache() const { return *m_glyphCache; }

private:
    void FillRectPixels(int x0, int y0, int x1, int y1, Color c);
    void FillCirclePixels(float cx, float cy, float r, Color c);
    void StrokeLinePixels(float x1, float y1, float x2, float y2,
                           float width, Color c);
    void FillArcPixels(float cx, float cy, float outerR, float innerR,
                        ArcAngle arc, Color c);
    void BlitGlyphPixels(float x, float y, Color c, int glyphIndex);

    int m_width;
    int m_height;
    Rect m_clip;
    std::vector<uint32_t> m_buffer;

    // Glyph cache — non-null pointer injected at construction.
    // The cache must outlive the Rasterizer.
    const std::vector<GlyphBitmap>* m_glyphCache;
};

} // namespace spike
