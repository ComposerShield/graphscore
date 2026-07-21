// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include "render_list.hpp"
#include "font_manager.hpp"

namespace spike {

// SMuFL codepoints used in the spike
namespace SMuFL {
    constexpr uint32_t GClef = 0xE050;
    constexpr uint32_t FClef = 0xE062;
    constexpr uint32_t NoteheadBlack = 0xE0A4;
    constexpr uint32_t NoteheadHalf = 0xE0A3;
    constexpr uint32_t TimeSigCommon = 0xE08A;
    constexpr uint32_t TimeSig4over4 = 0xE084;
    constexpr uint32_t AccidentalSharp = 0xE262;
    constexpr uint32_t AccidentalFlat = 0xE260;
    constexpr uint32_t AccidentalNatural = 0xE261;
    constexpr uint32_t RestQuarter = 0xE4E5;

    // Staff positions (C4 = middle C)
    // On treble clef: E4 = bottom line, F5 = top line
    // Lines: E4, G4, B4, D5, F5
    // Spaces: F4, A4, C5, E5

    // Returns pixel Y offset from staff top for a given diatonic step
    // step 0 = bottom line, each half-step = half a space
    constexpr float StepToYOffset(int step, float lineSpacing) {
        // step is in half-staff-space units from bottom line
        return static_cast<float>(-step) * lineSpacing * 0.5f;
    }
}

class NotationRenderer {
public:
    NotationRenderer(FontManager* fm) : m_fontManager(fm) {}

    // Render a single staff at (x, y) with the given width
    void RenderStaff(RenderList& rl, float x, float y, float width,
                      float lineSpacing, const Transform& xform);

    // Render a treble clef on a staff
    void RenderTrebleClef(RenderList& rl, float x, float y, float lineSpacing);

    // Render a notehead at a given staff position
    void RenderNotehead(RenderList& rl, float x, float staffY,
                         float lineSpacing, int step, bool filled);

    // Render three demonstration staves with active view transform
    void RenderDemoStaves(RenderList& rl, float x, float y, float width,
                           float lineSpacing, const Transform& xform);

private:
    FontManager* m_fontManager;
    int m_clefGlyphIdx = -1;
    int m_noteheadBlackIdx = -1;
    int m_noteheadHalfIdx = -1;
    int m_sharpIdx = -1;
    int m_flatIdx = -1;

    void EnsureGlyphsCached(int pixelSize);
};

} // namespace spike
