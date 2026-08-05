// SPDX-License-Identifier: Apache-2.0

#include <graphscore/rendering/graphscore_rendering.hpp>

#include <string>

#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ot.h>
#include <thorvg.h>
#endif

namespace graphscore {
namespace {
constexpr int kRenderingVersion = 1;

#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
// SMuFL (Standard Music Font Layout) code point for the G clef glyph. Any
// font on disk reports a nonzero glyph count; only a font that maps this
// code point is evidence the loaded face is actually a SMuFL music font
// rather than an arbitrary one.
constexpr FT_ULong kSmuflGClefCodepoint = 0xE050;
#endif
}  // namespace

int rendering_version() {
  return kRenderingVersion;
}

bool RenderingBackendAvailable() {
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  return true;
#else
  return false;
#endif
}

RenderingDependencyProof ProveRenderingDependencies(
    const std::string& bravura_font_path) {
  RenderingDependencyProof proof;

#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  // FreeType's own glyph id for the SMuFL G clef, retained so that the
  // HarfBuzz leg below can be cross-validated against it: the two libraries
  // resolve the same code point in the same file through independent cmap
  // implementations, so agreement is far stronger evidence than either
  // reporting a nonzero result alone.
  FT_UInt ft_gclef_gid = 0;

  FT_Library ft_library = nullptr;
  if (FT_Init_FreeType(&ft_library) == 0 && ft_library != nullptr) {
    proof.freetype_initialized = true;

    FT_Face face = nullptr;
    if (FT_New_Face(ft_library, bravura_font_path.c_str(), 0, &face) == 0 &&
        face != nullptr) {
      proof.bravura_face_loaded = true;
      proof.bravura_glyph_count = static_cast<std::int64_t>(face->num_glyphs);
      ft_gclef_gid              = FT_Get_Char_Index(face, kSmuflGClefCodepoint);
      proof.bravura_gclef_glyph_present = ft_gclef_gid != 0;
      FT_Done_Face(face);
    }

    FT_Done_FreeType(ft_library);
  }

  // Shapes the SMuFL G clef through the loaded font and checks the resolved
  // glyph id, not merely the buffer length. Asserting only
  // hb_buffer_get_glyph_infos()'s out-parameter would prove nothing: it is
  // the buffer length, and hb_shape on a one-code-point buffer always yields
  // exactly one glyph -- .notdef (id 0) when nothing maps -- so such a check
  // passes for a stubbed shaper, an unmapped code point, and even a garbage
  // blob with no font tables at all. Bravura does not map U+0041, so shaping
  // "A" could never be strengthened in place; U+E050 is a code point the font
  // actually carries. Requiring the id to be nonzero AND to equal FreeType's
  // independently resolved id fails every one of those cases.
  // hb_ot_font_set_funcs installs the OpenType implementations of the font
  // funcs (advances, extents, nominal glyph). Nominal-glyph lookup resolves
  // through hb_face_t's own OT cmap either way, so this call is not what makes
  // the check above discriminate; it is installed because the font funcs are
  // what later glyph-metrics work will depend on. It is reachable without the
  // deliberately disabled hb-ft integration (HB_HAVE_FREETYPE=OFF, ADR 0002
  // §4).
  hb_blob_t* blob = hb_blob_create_from_file(bravura_font_path.c_str());
  if (hb_blob_get_length(blob) > 0) {
    hb_face_t* hb_face = hb_face_create(blob, 0);
    hb_font_t* font    = hb_font_create(hb_face);
    hb_ot_font_set_funcs(font);

    hb_buffer_t* buffer = hb_buffer_create();
    if (hb_buffer_allocation_successful(buffer) != 0) {
      const hb_codepoint_t gclef =
          static_cast<hb_codepoint_t>(kSmuflGClefCodepoint);
      hb_buffer_add_codepoints(buffer, &gclef, 1, 0, 1);
      hb_buffer_guess_segment_properties(buffer);
      hb_shape(font, buffer, nullptr, 0);

      unsigned int     glyph_count = 0;
      hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
      proof.harfbuzz_buffer_shaped =
          glyph_count == 1 && infos != nullptr && infos[0].codepoint != 0 &&
          infos[0].codepoint == static_cast<hb_codepoint_t>(ft_gclef_gid);
    }
    hb_buffer_destroy(buffer);

    hb_font_destroy(font);
    hb_face_destroy(hb_face);
  }
  hb_blob_destroy(blob);

  if (tvg::Initializer::init(0) == tvg::Result::Success) {
    proof.thorvg_initialized = true;
    tvg::Initializer::term();
  }
#else
  (void)bravura_font_path;
#endif

  return proof;
}

}  // namespace graphscore
