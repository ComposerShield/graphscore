// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include <graphscore/notation/graphscore_notation.hpp>

namespace graphscore {

class Rendering {
 public:
  Rendering() = default;
};

// M05 Phase 1 rendering-dependency bring-up proof-of-life (ADR 0002 §A2,
// §A3, §A4). Confirms that FreeType, HarfBuzz, and ThorVG all initialize,
// that the pinned Bravura SMuFL font loads through FreeType and is
// identifiable as a SMuFL font (not merely any font), and that HarfBuzz
// actually shapes text through the loaded font rather than only allocating
// a buffer. This is dependency bring-up scaffolding only, not the notation
// renderer's own rendering API: `Rendering`, `ProveRenderingDependencies`,
// and this struct are temporary and are removed once `graphscore_rendering`
// implements its real glyph-metrics, text-shaping, and rasterization API
// for `graphscore_notation` (M05's engraving deliverables,
// docs/plan/05-notation-editor.md), not carried forward as permanent public
// surface.
//
// Every field is false/zero when GraphScore is configured with
// -DGRAPHSCORE_BUILD_WRITER=OFF, since no rendering backend is linked in
// that configuration (RenderingBackendAvailable() reports which case
// applies). No third-party type (FreeType, HarfBuzz, or ThorVG) appears
// here or anywhere else in this public header (ADR 0003 §2.2); they are
// confined to rendering.cpp.
struct RenderingDependencyProof {
  bool         freetype_initialized        = false;
  bool         harfbuzz_buffer_shaped      = false;
  bool         thorvg_initialized          = false;
  bool         bravura_face_loaded         = false;
  std::int64_t bravura_glyph_count         = 0;
  bool         bravura_gclef_glyph_present = false;
};

// True when graphscore_rendering was built with its rendering backend
// linked (GRAPHSCORE_BUILD_WRITER=ON at configure time).
bool RenderingBackendAvailable();

// Runs the proof-of-life described above against the font at
// `bravura_font_path`. The caller supplies the path rather than this
// function resolving one itself: graphscore_rendering has no SDL3 edge
// (ADR 0002 §A4), so runtime base-path resolution belongs to
// graphscore_writer_shell in later phases, not here.
RenderingDependencyProof ProveRenderingDependencies(
    const std::string& bravura_font_path);

}  // namespace graphscore
