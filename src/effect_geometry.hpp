#pragma once
#include <awareness/EffectsApi.hpp>
#include <vector>
#include <span>
namespace awareness {
struct EffectVertex {
    Vector3 position;
    float alpha;
};
struct EffectGeometryData {
    std::vector<Vector3> vertices;
    std::vector<EffectMesh> meshes;
};
inline bool CopyEffectGeometry(const EffectsInput &input, EffectGeometryData &output) {
    if (input.size != sizeof(input) || input.version != EffectsApiVersion || input.vertexCount > MaxEffectVertices ||
        input.meshCount > MaxEntities || input.reversedDepth > 1 || (input.vertexCount && !input.vertices) ||
        (input.meshCount && !input.meshes))
        return false;
    EffectGeometryData next;
    if (input.vertexCount)
        next.vertices.assign(input.vertices, input.vertices + input.vertexCount);
    if (input.meshCount)
        next.meshes.assign(input.meshes, input.meshes + input.meshCount);
    for (const auto &p : next.vertices)
        if (!Finite(p))
            return false;
    std::uint64_t total{};
    for (std::size_t i = 0; i < next.meshes.size(); ++i) {
        const auto &mesh = next.meshes[i];
        if (!mesh.vertexCount || mesh.vertexCount % 3 || mesh.firstVertex > input.vertexCount ||
            mesh.vertexCount > input.vertexCount - mesh.firstVertex)
            return false;
        total += mesh.vertexCount;
        if (total > MaxEffectVertices)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (next.meshes[j].entityId == mesh.entityId)
                return false;
    }
    output = std::move(next);
    return true;
}
inline void AppendEffectGeometry(std::vector<EffectVertex> &out, const EntitySnapshot &e, float alpha,
                                 const EffectGeometryData *data, EffectGeometry /*legacyMode*/, EffectsState &state) {
    if (data)
        for (const auto &mesh : data->meshes)
            if (mesh.entityId == e.id) {
                for (std::uint32_t i = 0; i < mesh.vertexCount; ++i)
                    out.push_back({data->vertices[mesh.firstVertex + i], alpha});
                ++state.meshCount;
                return;
            }
    // An absent model is not a box. Legacy BoundsFallback values also skip.
}
} // namespace awareness
