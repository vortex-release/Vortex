#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace awareness {
struct Vector2 { float x{}, y{}; };
struct Vector3 {
    float x{}, y{}, z{};
    Vector3 operator+(Vector3 b) const noexcept { return {x + b.x, y + b.y, z + b.z}; }
    Vector3 operator-(Vector3 b) const noexcept { return {x - b.x, y - b.y, z - b.z}; }
};
// Row-major storage, column-vector multiplication: clip = viewProjection * [x,y,z,1].
// Transpose a DirectXMath row-vector view * projection matrix before publishing it.
struct Matrix4x4 {
    float m[4][4]{};
    static Matrix4x4 Identity() noexcept {
        Matrix4x4 r{};
        for (int i = 0; i < 4; ++i) r.m[i][i] = 1.f;
        return r;
    }
};
struct Viewport { float x{}, y{}, width{}, height{}; };
struct ScreenBox { Vector2 min{}, max{}; };
struct ClipPoint { float x{}, y{}, z{}, w{}; };
inline bool Finite(Vector3 p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline bool Finite(ClipPoint p) noexcept {
    return Finite(Vector3{p.x, p.y, p.z}) && std::isfinite(p.w);
}
inline bool Valid(Viewport v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.width) &&
        std::isfinite(v.height) && std::isfinite(v.x+v.width) && std::isfinite(v.y+v.height) && v.width > 0.f && v.height > 0.f;
}
inline ClipPoint Transform(Vector3 p, const Matrix4x4& v) noexcept {
    return {
        v.m[0][0]*p.x + v.m[0][1]*p.y + v.m[0][2]*p.z + v.m[0][3],
        v.m[1][0]*p.x + v.m[1][1]*p.y + v.m[1][2]*p.z + v.m[1][3],
        v.m[2][0]*p.x + v.m[2][1]*p.y + v.m[2][2]*p.z + v.m[2][3],
        v.m[3][0]*p.x + v.m[3][1]*p.y + v.m[3][2]*p.z + v.m[3][3]};
}
inline Vector2 Project(ClipPoint p, Viewport v) noexcept {
    return {v.x + (p.x / p.w + 1.f) * .5f * v.width,
            v.y + (1.f - p.y / p.w) * .5f * v.height};
}
inline bool WorldToScreen(Vector3 world, const Matrix4x4& matrix, Viewport viewport,
                          Vector2& screen) noexcept {
    const auto p = Transform(world, matrix);
    if (!Valid(viewport) || !Finite(p) || p.w < 1e-4f || p.z < 0.f || p.z > p.w) return false;
    const auto result = Project(p, viewport);
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) return false;
    screen = result; // Outside x/y is allowed; consumers can clip to the viewport.
    return true;
}
inline ClipPoint Lerp(ClipPoint a, ClipPoint b, float t) noexcept {
    return {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t};
}
// Clip against w >= epsilon and Direct3D depth 0 <= z <= w before dividing.
// Clipping the 12 edges keeps boxes visible when some corners cross the near plane.
inline bool ClipDepth(ClipPoint& a, ClipPoint& b) noexcept {
    if (!Finite(a) || !Finite(b)) return false;
    float lo = 0.f, hi = 1.f;
    const float da[3]{a.w - 1e-4f, a.z, a.w - a.z};
    const float db[3]{b.w - 1e-4f, b.z, b.w - b.z};
    for (int i = 0; i < 3; ++i) {
        if (da[i] < 0.f && db[i] < 0.f) return false;
        if ((da[i] < 0.f) != (db[i] < 0.f)) {
            const float t = da[i] / (da[i] - db[i]);
            if (da[i] < 0.f) lo = (std::max)(lo, t); else hi = (std::min)(hi, t);
        }
    }
    if (lo > hi) return false;
    const auto original = a;
    a = Lerp(original, b, lo);
    b = Lerp(original, b, hi);
    return a.w > 0.f && b.w > 0.f;
}
inline bool CalculateBoundingBox(Vector3 origin, Vector3 mins, Vector3 maxs,
                                 const Matrix4x4& matrix, Viewport viewport,
                                 ScreenBox& box) noexcept {
    if (!Valid(viewport) || !Finite(origin) || !Finite(mins) || !Finite(maxs) ||
        mins.x > maxs.x || mins.y > maxs.y || mins.z > maxs.z) return false;
    std::array<ClipPoint, 8> corners{};
    for (int i = 0; i < 8; ++i) {
        corners[i] = Transform(origin + Vector3{(i & 1) ? maxs.x : mins.x,
            (i & 2) ? maxs.y : mins.y, (i & 4) ? maxs.z : mins.z}, matrix);
    }
    const float inf = std::numeric_limits<float>::infinity();
    ScreenBox result{{inf, inf}, {-inf, -inf}};
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit <= 4; bit <<= 1) {
            if (i & bit) continue;
            auto a = corners[i], b = corners[i | bit];
            if (!ClipDepth(a, b)) continue;
            for (auto p : {a, b}) {
                const auto s = Project(p, viewport);
                if (!std::isfinite(s.x) || !std::isfinite(s.y)) return false;
                result.min.x = (std::min)(result.min.x, s.x);
                result.min.y = (std::min)(result.min.y, s.y);
                result.max.x = (std::max)(result.max.x, s.x);
                result.max.y = (std::max)(result.max.y, s.y);
                any = true;
            }
        }
    }
    if (!any || result.max.x < viewport.x || result.min.x > viewport.x + viewport.width ||
        result.max.y < viewport.y || result.min.y > viewport.y + viewport.height) return false;
    result.min.x = std::clamp(result.min.x, viewport.x, viewport.x + viewport.width);
    result.max.x = std::clamp(result.max.x, viewport.x, viewport.x + viewport.width);
    result.min.y = std::clamp(result.min.y, viewport.y, viewport.y + viewport.height);
    result.max.y = std::clamp(result.max.y, viewport.y, viewport.y + viewport.height);
    if (result.max.x - result.min.x < 1.f || result.max.y - result.min.y < 1.f) return false;
    box = result;
    return true;
}
inline float Distance(Vector3 a, Vector3 b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline float DistanceAlpha(float distance, float fadeStart, float maximum) noexcept {
    if (!std::isfinite(fadeStart) || !std::isfinite(maximum) || fadeStart<0.f || maximum<=fadeStart) return 0.f;
    if (!std::isfinite(distance) || distance < 0.f || distance >= maximum) return 0.f;
    if (distance <= fadeStart) return 1.f;
    const float t = std::clamp((distance - fadeStart) / (maximum - fadeStart), 0.f, 1.f);
    return 1.f - t*t*(3.f - 2.f*t);
}
inline float HealthFraction(float health, float maximum) noexcept {
    if (!std::isfinite(health) || !std::isfinite(maximum) || maximum <= 0.f) return 0.f;
    return std::clamp(health / maximum, 0.f, 1.f);
}
} // namespace awareness
