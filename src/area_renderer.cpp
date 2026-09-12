#include "area_renderer.hpp"
namespace awareness::combat {
namespace {
Vector3 Cross(Vector3 a, Vector3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vector3 Normal(Vector3 v) noexcept {
    const float length = std::hypot(v.x, v.y, v.z);
    return Finite(v) && length > .001f ? flight::Scale(v, 1.f / length) : Vector3{0, 0, 1};
}
} // namespace
void AreaRenderer::Build(Cached &cached, const Area &area) {
    cached.handle = area.handle;
    cached.count = area.cellCount;
    cached.radius = area.cellRadius;
    cached.cells = area.cells;
    cached.normals = area.cellNormals;
    cached.vertices.clear();
    constexpr unsigned segments = 20;
    cached.vertices.reserve(area.cellCount * segments * 3);
    for (unsigned i = 0; i < area.cellCount; ++i) {
        if (!Finite(area.cells[i]))
            continue;
        const auto n = Normal(area.cellNormals[i]);
        const auto tangent = Normal(Cross(n, std::abs(n.y) > .9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0}));
        const auto bitangent = Cross(n, tangent);
        const auto center = area.cells[i] + flight::Scale(n, std::min(1.2f, area.cellRadius * .048f));
        auto ring = [&](unsigned j) {
            const float a = j * 6.28318530718f / segments;
            return center + flight::Scale(tangent, std::cos(a) * area.cellRadius) +
                   flight::Scale(bitangent, std::sin(a) * area.cellRadius);
        };
        for (unsigned j = 0; j < segments; ++j) {
            cached.vertices.push_back({center, 1});
            cached.vertices.push_back({ring(j), 1});
            cached.vertices.push_back({ring(j + 1), 1});
        }
    }
    ++rebuilds_;
}
HRESULT AreaRenderer::Render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                             const D3D11_TEXTURE2D_DESC &desc, ID3D11DepthStencilView *depth, bool reverse,
                             const Matrix4x4 &matrix, Viewport view, const WorldSnapshot &world, const Options &options,
                             float opacity, EffectsState &state) {
    state = {};
    combined_.clear();
    if (!options.areas || !options.fireArea || (!options.areaFill && options.areaOutline <= 0))
        return S_FALSE;
    if (!std::isfinite(opacity) || world.areaCount > world.areas.size())
        return E_INVALIDARG;
    for (auto &entry : cache_)
        entry.used = false;
    for (std::size_t i = 0; i < world.areaCount; ++i) {
        const auto &area = world.areas[i];
        if (area.type != AreaType::Fire || !area.handle || !area.cellCount || area.cellCount > area.cells.size() ||
            !std::isfinite(area.cellRadius) || area.cellRadius <= 0 || area.cellRadius > 64 ||
            !std::isfinite(area.remaining) || (area.duration > 0 && area.remaining <= 0))
            continue;
        Cached *cached{};
        for (auto &entry : cache_)
            if (entry.handle == area.handle) {
                cached = &entry;
                break;
            }
        if (!cached)
            for (auto &entry : cache_)
                if (!entry.used &&
                    (!entry.handle || std::none_of(world.areas.begin(), world.areas.begin() + world.areaCount,
                                                   [&](const Area &a) { return a.handle == entry.handle; }))) {
                    cached = &entry;
                    break;
                }
        if (!cached)
            continue;
        if (cached->handle != area.handle || cached->count != area.cellCount || cached->radius != area.cellRadius ||
            std::memcmp(cached->cells.data(), area.cells.data(), area.cellCount * sizeof(Vector3)) ||
            std::memcmp(cached->normals.data(), area.cellNormals.data(), area.cellCount * sizeof(Vector3)))
            Build(*cached, area);
        cached->used = true;
        if (combined_.size() + cached->vertices.size() > MaxEffectVertices)
            break;
        const float fade = area.duration > 0 ? std::clamp(area.remaining / .6f, 0.f, 1.f) : 1.f;
        const auto first = combined_.size();
        combined_.insert(combined_.end(), cached->vertices.begin(), cached->vertices.end());
        for (std::size_t j = first; j < combined_.size(); ++j)
            combined_[j].alpha = fade;
    }
    for (auto &entry : cache_)
        if (!entry.used)
            entry.handle = 0;
    EffectsConfiguration config;
    config.materialEnabled = options.areaFill;
    config.glowEnabled = options.areaOutline > 0;
    config.materialColor = options.fireColor;
    config.materialColor.a *= std::clamp(opacity, 0.f, 1.f);
    config.glowColor = options.fireColor;
    config.glowColor.a = std::min(1.f, options.fireColor.a * 3) * std::clamp(opacity, 0.f, 1.f);
    config.glowWidth = std::clamp(options.areaOutline * (options.areaGlow ? 2.f : 1.f), 1.f, 8.f);
    // MAX coverage resolves all overlapping cells before alpha composition: each
    // surface pixel blends once, while disconnected clusters retain their gaps.
    return renderer_.Render(device, context, target, desc, depth, reverse, matrix, view, combined_, config, state,
                            true);
}
} // namespace awareness::combat
