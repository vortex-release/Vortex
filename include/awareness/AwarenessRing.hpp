#pragma once
#include "Math.hpp"
#include <optional>

namespace awareness {
struct AwarenessArrow {
    Vector2 tip, left, right;
};
// A horizontal bearing around the reticle, independent of pitch and perspective division.
// up is {0,0,1} for CS2 or {0,1,0} for the portable demo.
inline std::optional<Vector2> AwarenessDirection(Vector3 origin, Vector3 target, const Matrix4x4 &matrix,
                                                 Vector3 up) noexcept {
    if (!Finite(origin) || !Finite(target) || !Finite(up))
        return {};
    for (const auto &row : matrix.m)
        for (float value : row)
            if (!std::isfinite(value))
                return {};
    const auto dot = [](Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto unit = [&](Vector3 v) -> std::optional<Vector3> {
        const float length = std::sqrt(dot(v, v));
        if (!std::isfinite(length) || length < 1e-5f)
            return {};
        return Vector3{v.x / length, v.y / length, v.z / length};
    };
    const auto vertical = unit(up);
    if (!vertical)
        return {};
    up = *vertical;
    const auto horizontal = [&](Vector3 v) {
        const float height = dot(v, up);
        return Vector3{v.x - up.x * height, v.y - up.y * height, v.z - up.z * height};
    };
    auto right = unit(horizontal({matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]}));
    auto forward = unit(horizontal({matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]}));
    if (!right)
        return {};
    if (!forward) // At a vertical view, retain a stable bearing from the camera's right axis.
        forward = unit(
            {right->y * up.z - right->z * up.y, right->z * up.x - right->x * up.z, right->x * up.y - right->y * up.x});
    else { // Remove projection-center offsets from the right axis.
        const float along = dot(*right, *forward);
        right = unit({right->x - forward->x * along, right->y - forward->y * along, right->z - forward->z * along});
    }
    const auto delta = unit(horizontal(target - origin));
    if (!right || !forward || !delta)
        return {};
    const float side = dot(*delta, *right), ahead = dot(*delta, *forward);
    const float length = std::hypot(side, ahead);
    if (!std::isfinite(length) || length < 1e-5f)
        return {};
    return Vector2{side / length, -ahead / length};
}
inline std::optional<AwarenessArrow> PlaceAwarenessArrow(Vector2 direction, Viewport view, float radius,
                                                         float size) noexcept {
    if (!Valid(view) || !std::isfinite(radius) || !std::isfinite(size) || radius <= 0 || size <= 0)
        return {};
    const float length = std::hypot(direction.x, direction.y);
    if (!std::isfinite(length) || length < 1e-5f)
        return {};
    direction = {direction.x / length, direction.y / length};
    const float scale = (std::min)(view.width, view.height) / 1080.f;
    size *= scale;
    radius = (std::min)(radius * scale, (std::min)(view.width, view.height) * .5f - size - 2);
    if (radius <= size)
        return {};
    const Vector2 center{view.x + view.width * .5f, view.y + view.height * .5f};
    const Vector2 tip{center.x + direction.x * radius, center.y + direction.y * radius};
    const Vector2 base{tip.x - direction.x * size, tip.y - direction.y * size};
    return AwarenessArrow{tip,
                          {base.x - direction.y * size * .5f, base.y + direction.x * size * .5f},
                          {base.x + direction.y * size * .5f, base.y - direction.x * size * .5f}};
}
} // namespace awareness
