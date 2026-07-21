// SPDX-License-Identifier: Apache-2.0

// HarfBuzz is configured WITHOUT FreeType support (HB_HAVE_FREETYPE=OFF).
// Shaping uses hb_blob_create_from_file + hb_face_create (no FreeType bridge).
// FreeType is used independently for glyph rasterization.
//
// Face-correct invariant: text and music faces are distinct.  GIDs produced
// by HarfBuzz text shaping are rasterized through m_textFace only.  SMuFL
// music glyphs are rasterized through m_bravuraFace only.  The two paths
// never cross.

#include "font_manager.hpp"
#include <cstring>
#include <cstdio>

namespace spike {

FontManager::FontManager() {
    FT_Error err = FT_Init_FreeType(&m_ftLib);
    if (err) {
        std::fprintf(stderr, "FontManager: FT_Init_FreeType failed: %d\n", err);
    }
}

FontManager::~FontManager() {
    if (m_hbFont) hb_font_destroy(m_hbFont);
    if (m_textFace) FT_Done_Face(m_textFace);
    if (m_bravuraFace) FT_Done_Face(m_bravuraFace);
    if (m_ftLib) FT_Done_FreeType(m_ftLib);
}

bool FontManager::LoadBravura(const char* path) {
    if (!m_ftLib) return false;
    FT_Error err = FT_New_Face(m_ftLib, path, 0, &m_bravuraFace);
    if (err) {
        std::fprintf(stderr, "FontManager: FT_New_Face(Bravura) failed: %d\n", err);
        return false;
    }
    m_bravuraLoaded = true;
    return true;
}

bool FontManager::LoadTextFont(const char* path) {
    if (!m_ftLib) return false;

    // Load with FreeType for glyph rasterization — this is the text FT_Face.
    // All GIDs from HarfBuzz text shaping are rasterized through THIS face only.
    FT_Error err = FT_New_Face(m_ftLib, path, 0, &m_textFace);
    if (err) {
        std::fprintf(stderr, "FontManager: FT_New_Face(text) failed: %d\n", err);
        return false;
    }

    // Load with HarfBuzz for text shaping.
    // Uses hb_blob_create_from_file (core HarfBuzz, no FreeType bridge)
    // so that HarfBuzz has direct access to cmap/GSUB/GPOS tables.
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(path);
    if (!blob) {
        std::fprintf(stderr, "FontManager: hb_blob_create_from_file_or_fail failed\n");
        return false;
    }

    hb_face_t* face = hb_face_create(blob, 0);
    hb_blob_destroy(blob); // face retains a reference

    m_hbFont = hb_font_create(face);
    hb_face_destroy(face); // font retains a reference

    // Configure HarfBuzz font with OpenType funcs (not FreeType funcs).
    // The scale is set per-request in ShapeText / ShapeAndCacheText
    // as pixelSize * 64 so that positions are returned in 26.6 pixel units.
    hb_ot_font_set_funcs(m_hbFont);

    m_textLoaded = true;
    return true;
}

int FontManager::RasterizeGlyphInternal(FT_Face face, unsigned int glyphIndex,
                                         int pixelSize) {
    if (!face) return -1;

    // Determine FontFace for cache key lookup.
    FontFace fface;
    if (face == m_bravuraFace) fface = FontFace::Music;
    else if (face == m_textFace) fface = FontFace::Text;
    else return -1;

    // Check glyph bitmap cache first.
    GlyphCacheKey key{fface, glyphIndex, pixelSize};
    auto it = m_glyphIndexMap.find(key);
    if (it != m_glyphIndexMap.end()) {
        return it->second; // Reuse cached bitmap index
    }

    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));

    FT_Error err = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
    if (err) return -1;

    FT_GlyphSlot slot = face->glyph;
    err = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
    if (err) return -1;

    FT_Bitmap* bmp = &slot->bitmap;
    GlyphBitmap gb;
    gb.width = static_cast<int>(bmp->width);
    gb.height = static_cast<int>(bmp->rows);
    gb.left = slot->bitmap_left;
    gb.top = slot->bitmap_top;
    gb.advanceX = static_cast<int>(slot->advance.x >> 6);
    gb.advanceY = static_cast<int>(slot->advance.y >> 6);

    if (bmp->pitch < 0) return -1;

    size_t bufSize = static_cast<size_t>(gb.width) * static_cast<size_t>(gb.height);
    gb.buffer.resize(bufSize);

    if (bmp->pixel_mode == FT_PIXEL_MODE_GRAY && bmp->num_grays == 256) {
        for (size_t row = 0; row < static_cast<size_t>(gb.height); ++row) {
            std::memcpy(gb.buffer.data() + row * static_cast<size_t>(gb.width),
                        bmp->buffer + row * static_cast<size_t>(bmp->pitch),
                        static_cast<size_t>(gb.width));
        }
    } else if (bmp->pixel_mode == FT_PIXEL_MODE_MONO) {
        for (int row = 0; row < gb.height; ++row) {
            for (int col = 0; col < gb.width; ++col) {
                uint8_t byte = bmp->buffer[row * bmp->pitch + col / 8];
                uint8_t bit = (byte >> (7 - (col % 8))) & 1;
                gb.buffer[static_cast<size_t>(row) * static_cast<size_t>(gb.width) +
                          static_cast<size_t>(col)] = bit ? 255 : 0;
            }
        }
    } else {
        return -1;
    }

    int idx = static_cast<int>(m_glyphCache.size());
    m_glyphCache.push_back(std::move(gb));
    m_glyphIndexMap[key] = idx;   // Store in cache map for reuse
    return idx;
}

int FontManager::RasterizeGlyph(uint32_t codepoint, int pixelSize) {
    if (!m_bravuraFace) return -1;

    FT_Set_Pixel_Sizes(m_bravuraFace, 0, static_cast<FT_UInt>(pixelSize));
    FT_UInt glyphIndex = FT_Get_Char_Index(m_bravuraFace, codepoint);
    if (glyphIndex == 0) return -1;

    return RasterizeGlyphInternal(m_bravuraFace, glyphIndex, pixelSize);
}

int FontManager::RasterizeGlyphByIndex(unsigned int glyphIndex, int pixelSize,
                                        FontFace face) {
    FT_Face ftFace = nullptr;
    switch (face) {
    case FontFace::Music:
        if (!m_bravuraFace) return -1;
        ftFace = m_bravuraFace;
        break;
    case FontFace::Text:
        if (!m_textFace) return -1;
        ftFace = m_textFace;
        break;
    }
    if (!ftFace) return -1;
    return RasterizeGlyphInternal(ftFace, glyphIndex, pixelSize);
}

std::vector<ShapedGlyph> FontManager::ShapeText(const char* textUtf8, int pixelSize) {
    std::vector<ShapedGlyph> result;
    if (!m_hbFont || !m_textFace) return result;

    // Use UPEM-based scale for consistent px conversion.
    // hb positions are in 1/64 em; convert with pixelSize/upem.
    int upem = m_textFace->units_per_EM;
    if (upem <= 0) return result;

    unsigned int s = static_cast<unsigned int>(upem);
    hb_font_set_scale(m_hbFont, s, s);

    hb_buffer_t* buf = hb_buffer_create();
    if (!buf) return result;

    hb_buffer_add_utf8(buf, textUtf8, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);

    hb_shape(m_hbFont, buf, nullptr, 0);

    unsigned int glyphCount = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &glyphCount);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &glyphCount);

    // When hb_font_set_scale(upem, upem), positions are in font units.
    // Convert to pixels: position * (pixelSize / upem) — no /64.
    float pxPerEmUnit = static_cast<float>(pixelSize) / static_cast<float>(upem);

    for (unsigned int i = 0; i < glyphCount; ++i) {
        ShapedGlyph sg;
        sg.glyphId = infos[i].codepoint;
        sg.cluster = infos[i].cluster;
        sg.xAdvance = static_cast<float>(pos[i].x_advance) * pxPerEmUnit;
        sg.yAdvance = static_cast<float>(pos[i].y_advance) * pxPerEmUnit;
        sg.xOffset = static_cast<float>(pos[i].x_offset) * pxPerEmUnit;
        sg.yOffset = static_cast<float>(pos[i].y_offset) * pxPerEmUnit;
        result.push_back(sg);
    }

    hb_buffer_destroy(buf);
    return result;
}

const std::vector<FontManager::PositionedGlyph>& FontManager::ShapeAndCacheText(
        const char* textUtf8, int pixelSize) {
    static const std::vector<PositionedGlyph> s_empty;

    if (!m_textFace || !m_hbFont) return s_empty;

    // Heterogeneous transparent lookup via string_view — no allocation.
    ShapeCacheLookupKey lookupKey{std::string_view(textUtf8), pixelSize};
    auto shapeIt = m_shapeCache.find(lookupKey);
    if (shapeIt != m_shapeCache.end()) {
        return shapeIt->second;
    }

    // Cache miss — allocate owning key string, shape, rasterize.
    m_shapeCacheMissCount++;
    auto shaped = ShapeText(textUtf8, pixelSize);
    if (shaped.empty()) return s_empty;

    std::vector<PositionedGlyph> result;

    for (const auto& sg : shaped) {
        PositionedGlyph pg;
        pg.cacheIndex = RasterizeGlyphInternal(m_textFace, sg.glyphId, pixelSize);
        pg.xOffset = sg.xOffset;
        pg.yOffset = sg.yOffset;
        pg.xAdvance = sg.xAdvance;
        result.push_back(pg);
    }

    // Insert owning key — the only place a string is allocated.
    ShapeCacheKey owningKey{std::string(textUtf8), pixelSize};
    m_shapeKeyAllocCount++;
    auto [it, _] = m_shapeCache.emplace(std::move(owningKey), std::move(result));
    return it->second;
}

} // namespace spike
