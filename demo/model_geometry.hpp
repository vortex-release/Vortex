#pragma once
#include <awareness/EffectsApi.hpp>
#include <vector>
#include <numbers>

namespace awareness::demo {
// Geometry owned by the simulated demo scene. CS2 never uses this model.
struct ModelGeometry {
    std::vector<Vector3> vertices;
    std::vector<EffectMesh> meshes;
};
inline ModelGeometry MakeModelGeometry(const FrameSnapshot &frame, float seconds = 0) {
    ModelGeometry result;
    const auto ellipsoid = [&](Vector3 center, Vector3 radius) {
        constexpr int rings = 8, sides = 12;
        const auto vertex = [&](int row, int column) {
            const float latitude = std::numbers::pi_v<float> * static_cast<float>(row) / rings;
            const float longitude = 2 * std::numbers::pi_v<float> * static_cast<float>(column) / sides;
            return center + Vector3{radius.x * std::sin(latitude) * std::cos(longitude), radius.y * std::cos(latitude),
                                    radius.z * std::sin(latitude) * std::sin(longitude)};
        };
        for (int r = 0; r < rings; ++r)
            for (int c = 0; c < sides; ++c) {
                for (auto point : {vertex(r, c), vertex(r + 1, c), vertex(r, c + 1), vertex(r, c + 1), vertex(r + 1, c),
                                   vertex(r + 1, c + 1)})
                    result.vertices.push_back(point);
            }
    };
    const auto limb = [&](Vector3 a, Vector3 b, float ra, float rb) {
        const Vector3 axis = b - a;
        const float length = Distance(a, b);
        if (length <= .001f)
            return;
        const Vector3 up{axis.x / length, axis.y / length, axis.z / length};
        // Cross with Z (all demo limbs have a substantial X/Y component).
        const float plane = std::sqrt(up.x * up.x + up.y * up.y);
        const Vector3 right{up.y / plane, -up.x / plane, 0};
        const Vector3 forward{-up.z * right.y, up.z * right.x, up.x * right.y - up.y * right.x};
        const auto point = [&](Vector3 center, float radius, int side) {
            const float angle = 2 * std::numbers::pi_v<float> * static_cast<float>(side) / 12;
            return center + Vector3{radius * (right.x * std::cos(angle) + forward.x * std::sin(angle)),
                                    radius * (right.y * std::cos(angle) + forward.y * std::sin(angle)),
                                    radius * (right.z * std::cos(angle) + forward.z * std::sin(angle))};
        };
        for (int i = 0; i < 12; ++i) {
            const Vector3 p = point(a, ra, i), q = point(a, ra, i + 1), r = point(b, rb, i), s = point(b, rb, i + 1);
            for (auto vertex : {p, r, q, q, r, s, a, q, p, b, r, s})
                result.vertices.push_back(vertex);
        }
        ellipsoid(a, {ra, ra, ra});
        ellipsoid(b, {rb, rb, rb});
    };
    for (std::uint32_t i = 0; i < frame.entityCount && i < MaxEntities; ++i) {
        const auto &entity = frame.entities[i];
        if (!entity.valid || entity.health <= 0 || entity.dormant || !Finite(entity.origin))
            continue;
        if (result.vertices.size() + 14544 > MaxEffectVertices)
            break;
        const auto first = static_cast<std::uint32_t>(result.vertices.size());
        const auto p = [&](float x, float y, float z = 0) { return entity.origin + Vector3{x, y, z}; };
        const float stride = .075f * std::sin(seconds * 3 + static_cast<float>(i));
        ellipsoid(p(0, 1.69f), {.105f, .14f, .10f});
        limb(p(0, 1.49f), p(0, 1.60f), .063f, .06f);
        ellipsoid(p(0, 1.25f), {.22f, .32f, .13f});
        ellipsoid(p(0, .91f), {.17f, .18f, .12f});
        limb(p(-.21f, 1.43f), p(-.34f, 1.13f, -.055f), .082f, .067f);
        limb(p(-.34f, 1.13f, -.055f), p(-.02f, 1.18f, -.24f), .067f, .047f);
        limb(p(.21f, 1.43f), p(.32f, 1.10f, -.04f), .082f, .067f);
        limb(p(.32f, 1.10f, -.04f), p(.07f, 1.20f, -.17f), .067f, .047f);
        limb(p(-.10f, .89f), p(-.13f, .49f, -stride), .11f, .077f);
        limb(p(-.13f, .49f, -stride), p(-.16f, .12f, stride), .077f, .056f);
        limb(p(.10f, .89f), p(.13f, .49f, stride), .11f, .077f);
        limb(p(.13f, .49f, stride), p(.17f, .12f, -stride), .077f, .056f);
        ellipsoid(p(-.16f, .065f, -.045f + stride), {.075f, .065f, .14f});
        ellipsoid(p(.17f, .065f, -.045f - stride), {.075f, .065f, .14f});
        result.meshes.push_back({entity.id, first, static_cast<std::uint32_t>(result.vertices.size()) - first});
    }
    return result;
}
} // namespace awareness::demo
