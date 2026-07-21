// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstddef>

// Private third-party headers — spike-only, not for production public API
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ot.h>

namespace spike {

enum class FontFace : uint8_t {
    Music,  // Bravura SMuFL for music glyphs
    Text,   // Noto Sans for text shaping/rasterization
};

// Composite key for glyph bitmap cache: (FontFace, glyphId, pixelSize).
struct GlyphCacheKey {
    FontFace face;
    unsigned int glyphId;
    int pixelSize;

    bool operator==(const GlyphCacheKey& o) const {
        return face == o.face && glyphId == o.glyphId && pixelSize == o.pixelSize;
    }
};

struct GlyphCacheKeyHash {
    size_t operator()(const GlyphCacheKey& k) const {
        size_t h = static_cast<size_t>(k.face);
        h = h * 31 + static_cast<size_t>(k.glyphId);
        h = h * 31 + static_cast<size_t>(k.pixelSize);
        return h;
    }
};

struct ShapedGlyph {
    uint32_t glyphId;      // HarfBuzz glyph ID (GID)
    uint32_t cluster;
    float xAdvance;
    float yAdvance;
    float xOffset;
    float yOffset;
};

// Composite key for shaping cache: (text, pixelSize).
// Stores owning string for map keys.  Lookups use string_view
// through transparent hash/equal for zero-allocation hits.
struct ShapeCacheKey {
    std::string text;
    int pixelSize;

    bool operator==(const ShapeCacheKey& o) const {
        return pixelSize == o.pixelSize && text == o.text;
    }
};

// Non-owning lookup key for heterogeneous find().
struct ShapeCacheLookupKey {
    std::string_view text;
    int pixelSize;
};

struct ShapeCacheKeyHash {
    using is_transparent = void;

    size_t operator()(const ShapeCacheKey& k) const {
        size_t h = std::hash<std::string>{}(k.text);
        h = h * 31 + static_cast<size_t>(k.pixelSize);
        return h;
    }
    size_t operator()(const ShapeCacheLookupKey& k) const {
        size_t h = std::hash<std::string_view>{}(k.text);
        h = h * 31 + static_cast<size_t>(k.pixelSize);
        return h;
    }
};

struct ShapeCacheKeyEqual {
    using is_transparent = void;

    bool operator()(const ShapeCacheKey& a, const ShapeCacheKey& b) const {
        return a.pixelSize == b.pixelSize && a.text == b.text;
    }
    bool operator()(const ShapeCacheKey& a, const ShapeCacheLookupKey& b) const {
        return a.pixelSize == b.pixelSize && a.text == b.text;
    }
};

class FontManager {
public:
    FontManager();
    ~FontManager();

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    // Load Bravura SMuFL music font from file path
    bool LoadBravura(const char* path);

    // Load a text font for HarfBuzz shaping (system or provided).
    // Uses a distinct FT_Face from the music face so that GIDs from
    // HarfBuzz text shaping are always rasterized through the same face
    // that produced them.  Never shape text with the music face or
    // rasterize music GIDs through the text face.
    bool LoadTextFont(const char* path);

    // Rasterize a music glyph by Unicode SMuFL codepoint.
    // Always uses the Bravura music face.
    int RasterizeGlyph(uint32_t codepoint, int pixelSize);

    // Rasterize a glyph by raw glyph index (GID) from the specified face.
    // This is the correct path for shaped text: HarfBuzz returns GIDs
    // for the text face; FT_Load_Glyph loads from the same face.
    int RasterizeGlyphByIndex(unsigned int glyphIndex, int pixelSize, FontFace face);

    // HarfBuzz text shaping — uses the text face (hb_font) exclusively.
    // Returns shaped glyph info with GIDs from the text face.
    // hb_font_set_scale(upem, upem) → positions are font units.
    // Converted to pixels as: position * (pixelSize / upem).
    std::vector<ShapedGlyph> ShapeText(const char* textUtf8, int pixelSize);

    // Shape text AND rasterize all needed glyphs by GID through the text face.
    // Each glyph's GID from hb_shape is rasterized via FT_Load_Glyph on the
    // text FT_Face — never accidentally through the music face.
    // Positions are returned in pixels.
    struct PositionedGlyph {
        int cacheIndex = -1;
        float xOffset = 0.0f;
        float yOffset = 0.0f;
        float xAdvance = 0.0f;
    };
    // Returns an immutable reference to cached positioned glyphs.
    // Repeated shaping of the same string at the same size returns the
    // same backing data — no copy or allocation.
    const std::vector<PositionedGlyph>& ShapeAndCacheText(const char* textUtf8,
                                                           int pixelSize);

    // Get the glyph cache for the rasterizer
    const std::vector<GlyphBitmap>& GlyphCache() const { return m_glyphCache; }

    // Cache size inspection (for test assertions)
    size_t GlyphCacheSize() const { return m_glyphCache.size(); }
    size_t GlyphIndexMapSize() const { return m_glyphIndexMap.size(); }
    size_t ShapeCacheSize() const { return m_shapeCache.size(); }
    size_t ShapeKeyAllocCount() const { return m_shapeKeyAllocCount; }
    size_t ShapeCacheMissCount() const { return m_shapeCacheMissCount; }

    // Check if Bravura font is loaded
    bool HasBravura() const { return m_bravuraLoaded; }

    // Check if text font is loaded
    bool HasTextFont() const { return m_textLoaded; }

    // Set pixel size for glyph rasterization
    void SetPixelSize(int size) { m_pixelSize = size; }

private:
    // Third-party types (included via private header)
    FT_Library m_ftLib = nullptr;
    FT_Face m_bravuraFace = nullptr;
    FT_Face m_textFace = nullptr;
    hb_font_t* m_hbFont = nullptr;

    bool m_bravuraLoaded = false;
    bool m_textLoaded = false;
    int m_pixelSize = 32;

    std::vector<GlyphBitmap> m_glyphCache;

    // Glyph bitmap cache: (FontFace, glyphId, pixelSize) → index into m_glyphCache.
    // Repeated rasterization of the same glyph at the same size reuses the
    // same cache entry, preventing unbounded growth across frames.
    std::unordered_map<GlyphCacheKey, int, GlyphCacheKeyHash> m_glyphIndexMap;

    // Shaping cache: (text, pixelSize) → pre-computed PositionedGlyph vector.
    // Uses heterogeneous transparent lookup (string_view) — no string
    // allocation on cache hits.  Only allocates on miss.
    std::unordered_map<ShapeCacheKey,
                       std::vector<FontManager::PositionedGlyph>,
                       ShapeCacheKeyHash,
                       ShapeCacheKeyEqual> m_shapeCache;

    // Allocation instrumentation (for test assertions).
    size_t m_shapeKeyAllocCount = 0;   // Incremented when a new key string is stored
    size_t m_shapeCacheMissCount = 0;  // Incremented on cache miss

    // Internal: rasterize a glyph at the given FreeType face.
    int RasterizeGlyphInternal(FT_Face face, unsigned int glyphIndex, int pixelSize);
};

} // namespace spike
