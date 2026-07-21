// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

namespace spike {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    static constexpr Color White() { return {255, 255, 255, 255}; }
    static constexpr Color Black() { return {0, 0, 0, 255}; }
    static constexpr Color Red() { return {255, 0, 0, 255}; }
    static constexpr Color Blue() { return {0, 0, 255, 255}; }
    static constexpr Color Green() { return {0, 200, 0, 255}; }
    static constexpr Color Grey(uint8_t v) { return {v, v, v, 255}; }
    static constexpr Color Transparent() { return {0, 0, 0, 0}; }
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    bool operator==(Vec2 o) const { return x == o.x && y == o.y; }
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool Contains(Vec2 p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }

    Rect Intersect(Rect o) const {
        float ix = std::max(x, o.x);
        float iy = std::max(y, o.y);
        float iw = std::min(x + w, o.x + o.w) - ix;
        float ih = std::min(y + h, o.y + o.h) - iy;
        if (iw <= 0.0f || ih <= 0.0f) return {0, 0, 0, 0};
        return {ix, iy, iw, ih};
    }

    bool IsEmpty() const { return w <= 0.0f || h <= 0.0f; }
};

struct Transform {
    float scale = 1.0f;
    Vec2 offset;

    Vec2 WorldToScreen(Vec2 world) const {
        return {world.x * scale + offset.x, world.y * scale + offset.y};
    }

    Vec2 ScreenToWorld(Vec2 screen) const {
        return {(screen.x - offset.x) / scale, (screen.y - offset.y) / scale};
    }
};

struct GlyphBitmap {
    std::vector<uint8_t> buffer;
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    int advanceX = 0;
    int advanceY = 0;
};

// Arc angles in radians: startAngle to endAngle, shortest sweep (≤π).
// Angles follow standard math: 0 = +X, increasing CCW.
struct ArcAngle {
    float start = 0.0f;  // radians
    float end = 0.0f;    // radians
};

} // namespace spike
