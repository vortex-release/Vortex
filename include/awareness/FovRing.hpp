#pragma once
#include "Math.hpp"
#include <optional>
#include <numbers>
namespace awareness {
struct FovRing {
    Vector2 center, radius;
};
// Project the circular angular cone, including off-center and asymmetric projections.
// The configured tracking FOV is a HALF angle about the camera view axis.
inline std::optional<FovRing> ProjectFovRing(const Matrix4x4 &m, Viewport v, float degrees) noexcept {
    if (!std::isfinite(degrees) || degrees <= 0 || degrees >= 90 || v.width <= 0 || v.height <= 0)
        return {};
    const Vector3 z{m.m[3][0], m.m[3][1], m.m[3][2]};
    const auto dot = [](Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const float length = std::sqrt(dot(z, z));
    if (!std::isfinite(length) || length < 1e-6f)
        return {}; // Orthographic cameras have no angular ring.
    const Vector3 axis{z.x / length, z.y / length, z.z / length};
    const Vector3 x{m.m[0][0], m.m[0][1], m.m[0][2]}, y{m.m[1][0], m.m[1][1], m.m[1][2]};
    const float dx = dot(x, axis), dy = dot(y, axis);
    const float tangent = std::tan(degrees * std::numbers::pi_v<float> / 180.f);
    FovRing result{{v.x + v.width * .5f * (1 + dx / length), v.y + v.height * .5f * (1 - dy / length)},
                   {v.width * .5f * std::sqrt((std::max)(0.f, dot(x, x) - dx * dx)) / length * tangent,
                    v.height * .5f * std::sqrt((std::max)(0.f, dot(y, y) - dy * dy)) / length * tangent}};
    if (!std::isfinite(result.center.x) || !std::isfinite(result.center.y) || !std::isfinite(result.radius.x) ||
        !std::isfinite(result.radius.y) || result.radius.x < .01f || result.radius.y < .01f)
        return {};
    // If the whole viewport is inside the cone, its boundary is correctly offscreen.
    bool contains = true;
    for (int i = 0; i < 4; ++i) {
        const float a = (v.x + ((i & 1) ? v.width : 0) - result.center.x) / result.radius.x;
        const float b = (v.y + ((i & 2) ? v.height : 0) - result.center.y) / result.radius.y;
        contains &= a * a + b * b < 1;
    }
    return contains ? std::nullopt : std::optional{result};
}
} // namespace awareness
