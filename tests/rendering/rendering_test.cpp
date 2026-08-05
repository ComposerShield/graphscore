// SPDX-License-Identifier: Apache-2.0

#include <graphscore/rendering/graphscore_rendering.hpp>

#include <gtest/gtest.h>

namespace {

TEST(RenderingDependencyProofTest, ProvesFreeTypeHarfBuzzThorVGAndBravura) {
  if (!graphscore::RenderingBackendAvailable()) {
    GTEST_SKIP() << "GRAPHSCORE_BUILD_WRITER=OFF: no rendering backend linked";
  }

#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
  const graphscore::RenderingDependencyProof proof =
      graphscore::ProveRenderingDependencies(GRAPHSCORE_BRAVURA_FONT_PATH);

  EXPECT_TRUE(proof.freetype_initialized);
  EXPECT_TRUE(proof.harfbuzz_buffer_shaped);
  EXPECT_TRUE(proof.thorvg_initialized);
  EXPECT_TRUE(proof.bravura_face_loaded);
  EXPECT_GT(proof.bravura_glyph_count, 0);
  // A nonzero glyph count alone passes for any font on disk; the mapped
  // SMuFL G clef code point (U+E050) is evidence the loaded face is
  // actually Bravura (or another SMuFL font), not an arbitrary one.
  EXPECT_TRUE(proof.bravura_gclef_glyph_present);
#else
  FAIL() << "GRAPHSCORE_BUILD_WRITER=ON but GRAPHSCORE_BRAVURA_FONT_PATH was "
            "not defined for this test target";
#endif
}

}  // namespace
