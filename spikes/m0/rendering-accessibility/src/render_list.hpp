// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include <variant>
#include <vector>

namespace spike {

enum class CmdType : uint8_t {
    FillRect,
    FillCircle,
    StrokeLine,
    BlitGlyph,
    SetClip,
    FillArc,
};

struct CmdFillRect {
    Rect rect;
    Color color;
};

struct CmdFillCircle {
    float cx;
    float cy;
    float radius;
    Color color;
};

struct CmdStrokeLine {
    float x1, y1, x2, y2;
    float width;
    Color color;
};

struct CmdBlitGlyph {
    float x, y;
    Color color;
    int glyphIndex;
};

struct CmdSetClip {
    Rect rect;
};

struct CmdFillArc {
    float cx;
    float cy;
    float radius;
    float innerRadius = 0.0f;  // 0 = filled pie sector; >0 = annulus
    ArcAngle arc;
    Color color;
};

using CmdData = std::variant<CmdFillRect, CmdFillCircle, CmdStrokeLine,
                              CmdBlitGlyph, CmdSetClip, CmdFillArc>;

struct RenderCommand {
    CmdType type;
    CmdData data;

    static RenderCommand FillRect(Rect r, Color c) {
        return {CmdType::FillRect, CmdFillRect{r, c}};
    }

    static RenderCommand FillCircle(float cx, float cy, float radius, Color c) {
        return {CmdType::FillCircle, CmdFillCircle{cx, cy, radius, c}};
    }

    static RenderCommand StrokeLine(float x1, float y1, float x2,
                                     float y2, float w, Color c) {
        return {CmdType::StrokeLine, CmdStrokeLine{x1, y1, x2, y2, w, c}};
    }

    static RenderCommand BlitGlyph(float x, float y, Color c, int idx) {
        return {CmdType::BlitGlyph, CmdBlitGlyph{x, y, c, idx}};
    }

    static RenderCommand SetClip(Rect r) {
        return {CmdType::SetClip, CmdSetClip{r}};
    }

    static RenderCommand FillArc(float cx, float cy, float radius,
                                  float innerRadius, ArcAngle arc, Color c) {
        return {CmdType::FillArc, CmdFillArc{cx, cy, radius, innerRadius, arc, c}};
    }
};

class RenderList {
public:
    void Clear() { m_cmds.clear(); }
    void Add(RenderCommand cmd) { m_cmds.push_back(std::move(cmd)); }
    const std::vector<RenderCommand>& Commands() const { return m_cmds; }

    void AddFilledRoundedRect(Rect r, float radius, Color fill);
    void AddOrthogonalConnector(Vec2 start, Vec2 end,
                                 const std::vector<Vec2>& bends,
                                 float lineWidth, float cornerRadius,
                                 Color color);
    void AddStaffLines(float x, float y, float width,
                        int numLines, float lineSpacing, Color color);

private:
    std::vector<RenderCommand> m_cmds;
};

} // namespace spike
