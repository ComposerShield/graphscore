// SPDX-License-Identifier: Apache-2.0

#include "render_list.hpp"

namespace spike {

void RenderList::AddFilledRoundedRect(Rect r, float radius, Color fill) {
    // Center rect
    float r2 = radius;
    Add(RenderCommand::FillRect(
        {r.x + r2, r.y, r.w - 2.0f * r2, r.h}, fill));
    // Top and bottom edge strips
    Add(RenderCommand::FillRect(
        {r.x + r2, r.y, r.w - 2.0f * r2, r2}, fill));
    Add(RenderCommand::FillRect(
        {r.x + r2, r.y + r.h - r2, r.w - 2.0f * r2, r2}, fill));
    // Left and right edge strips
    Add(RenderCommand::FillRect(
        {r.x, r.y + r2, r2, r.h - 2.0f * r2}, fill));
    Add(RenderCommand::FillRect(
        {r.x + r.w - r2, r.y + r2, r2, r.h - 2.0f * r2}, fill));
    // Four corner circles
    Add(RenderCommand::FillCircle(r.x + r2, r.y + r2, r2, fill));
    Add(RenderCommand::FillCircle(r.x + r.w - r2, r.y + r2, r2, fill));
    Add(RenderCommand::FillCircle(r.x + r2, r.y + r.h - r2, r2, fill));
    Add(RenderCommand::FillCircle(r.x + r.w - r2, r.y + r.h - r2, r2, fill));
}

// Compute the arc angles and true interior center for a 90° orthogonal
// turn fillet of midline radius r.
//
// Geometry:
//   For incoming unit u=(b-a)/|b-a| and outgoing unit v=(c-b)/|c-b|:
//     tangent points:  t0 = b - u*r  (on the incoming segment)
//                      t1 = b + v*r  (on the outgoing segment)
//     interior center: C = b - u*r + v*r
// The arc is drawn from t0 to t1 centered at C with radius r.
// The vectors (t0-C) and (t1-C) are orthogonal; the sweep is ±π/2.
//
// On return, centerOut is set to the true interior fillet center C
// (in the same coordinate space as the bend point b).
// The returned ArcAngle arcs from t0 to t1 in the shortest direction.
static ArcAngle ComputeFilletArc(Vec2 unitIn, Vec2 unitOut, Vec2* centerOut) {
    // Tangent points relative to the bend point b (b not yet known here):
    //   t0_rel = -u  (pointing opposite of incoming direction)
    //   t1_rel = +v  (pointing along outgoing direction)
    // Interior center relative to b:
    //   C_rel = -u + v  (centre = b - u*r + v*r → C/b = -u + v)
    Vec2 t0Rel = {-unitIn.x, -unitIn.y};
    Vec2 t1Rel = {unitOut.x, unitOut.y};
    Vec2 cRel  = {-unitIn.x + unitOut.x, -unitIn.y + unitOut.y};

    // Angles from the interior center C to the tangent points
    float a0 = std::atan2(t0Rel.y - cRel.y, t0Rel.x - cRel.x);
    float a1 = std::atan2(t1Rel.y - cRel.y, t1Rel.x - cRel.x);

    constexpr float k2Pi = 2.0f * 3.14159265358979f;
    // Normalise to [0, 2π)
    if (a0 < 0.0f) a0 += k2Pi;
    if (a1 < 0.0f) a1 += k2Pi;

    // Compute shortest signed sweep — always ±π/2 for orthogonal turns
    float sweep = a1 - a0;
    if (sweep > 3.14159265358979f) {
        sweep -= k2Pi;
    } else if (sweep < -3.14159265358979f) {
        sweep += k2Pi;
    }

    ArcAngle arc;
    arc.start = a0;
    arc.end   = a0 + sweep;

    if (centerOut) *centerOut = cRel;

    return arc;
}

void RenderList::AddOrthogonalConnector(Vec2 start, Vec2 end,
                                         const std::vector<Vec2>& bends,
                                         float lineWidth, float cornerRadius,
                                         Color color) {
    // Build the full point list
    std::vector<Vec2> pts;
    pts.push_back(start);
    for (auto& b : bends) pts.push_back(b);
    pts.push_back(end);

    if (pts.size() < 2) return;

    // ---- Validation ----
    // 1. Reject duplicate consecutive points
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float dx = pts[i + 1].x - pts[i].x;
        float dy = pts[i + 1].y - pts[i].y;
        if (dx * dx + dy * dy < 1e-12f) return; // duplicate/zero-length
    }

    // 2. Verify axis-aligned: each segment must be purely horizontal or vertical
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        Vec2 d = {pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y};
        bool horiz = std::abs(d.y) < 1e-4f;
        bool vert  = std::abs(d.x) < 1e-4f;
        if (!horiz && !vert) return; // non-axis-aligned — reject
    }

    // 3. Compute segment lengths and clamp cornerRadius
    std::vector<float> segLens;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float dx = pts[i + 1].x - pts[i].x;
        float dy = pts[i + 1].y - pts[i].y;
        segLens.push_back(std::sqrt(dx * dx + dy * dy));
    }

    float hw = lineWidth * 0.5f;
    float effectiveCR = cornerRadius;

    // Clamp cornerRadius: for each turn, r + hw ≤ min(adjacent segment length)/2
    // The trimmed segment consumes effectiveCR + hw from each side of the corner.
    for (size_t i = 0; i + 2 < pts.size(); ++i) {
        float maxR = std::min(segLens[i], segLens[i + 1]) * 0.5f - hw;
        if (maxR < 0.0f) maxR = 0.0f;
        if (effectiveCR > maxR) effectiveCR = maxR;
    }
    if (effectiveCR < 0.0f) effectiveCR = 0.0f;

    // ---- Tangent geometry: trim each segment so the midline ends at
    //      distance effectiveCR from each bend point, then connect the
    //      trimmed ends with an annular arc centered at the true interior
    //      fillet center.  That center is offset from the bend point by
    //      C = b - u*effectiveCR + v*effectiveCR.
    //
    //      The arc midline radius = effectiveCR; outer/inner = effectiveCR ± hw.
    //      Tangent points: t0 = b - u*r, t1 = b + v*r, both at distance r
    //      from the true interior center C.

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        Vec2 a = pts[i];
        Vec2 b = pts[i + 1];
        Vec2 dir = {b.x - a.x, b.y - a.y};
        float len = segLens[i];
        if (len <= 1e-6f) continue;

        Vec2 unit = {dir.x / len, dir.y / len};

        // Shrink segment to midline tangency point at each corner.
        float shrinkStart = (i > 0) ? (effectiveCR) : 0.0f;
        float shrinkEnd   = (i + 2 < pts.size()) ? (effectiveCR) : 0.0f;

        // Clamp shrink to segment length (proportional if both ends shrink)
        float totalShrink = shrinkStart + shrinkEnd;
        if (totalShrink > len) {
            shrinkStart = len * (shrinkStart / totalShrink);
            shrinkEnd   = len * (shrinkEnd   / totalShrink);
        }

        Vec2 segStart = {a.x + unit.x * shrinkStart, a.y + unit.y * shrinkStart};
        Vec2 segEnd   = {b.x - unit.x * shrinkEnd, b.y - unit.y * shrinkEnd};

        float remainingLen = len - shrinkStart - shrinkEnd;
        if (remainingLen > 1e-6f) {
            Add(RenderCommand::StrokeLine(
                segStart.x, segStart.y, segEnd.x, segEnd.y,
                lineWidth, color));
        }

        // Draw annulus (stroked arc) at the corner following this segment.
        // The true interior fillet center C is offset from the bend point b:
        //   C = b + cRel * effectiveCR
        // where cRel = -unit + unit2 (relative centre vector).
        if (i + 2 < pts.size()) {
            Vec2 c = pts[i + 2];
            Vec2 d2 = {c.x - b.x, c.y - b.y};
            float len2 = segLens[i + 1];
            if (len2 <= 1e-6f) continue;
            Vec2 unit2 = {d2.x / len2, d2.y / len2};

            // Compute the arc angles and relative center for this 90° turn.
            Vec2 cRel;
            ArcAngle arc = ComputeFilletArc(unit, unit2, &cRel);

            // True interior center = bend point + cRel * effectiveCR
            float cx = b.x + cRel.x * effectiveCR;
            float cy = b.y + cRel.y * effectiveCR;

            // Annulus: midline radius = effectiveCR, thickness = lineWidth
            float outerR = effectiveCR + hw;
            float innerR = std::max(0.0f, effectiveCR - hw);
            Add(RenderCommand::FillArc(
                cx, cy, outerR, innerR, arc, color));
        }
    }
}

void RenderList::AddStaffLines(float x, float y, float width,
                                int numLines, float lineSpacing, Color color) {
    for (int i = 0; i < numLines; ++i) {
        float ly = y + static_cast<float>(i) * lineSpacing;
        Add(RenderCommand::StrokeLine(x, ly, x + width, ly, 1.5f, color));
    }
}

} // namespace spike
