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
    cached.eventFootprint = area.estimatedFootprint && !area.cellCount;
    cached.radius = cached.eventFootprint ? area.radius : area.cellRadius;
    cached.center = area.center;
    cached.cells = area.cells;
    cached.normals = area.cellNormals;
    cached.vertices.clear();
    constexpr unsigned segments = 20;
    const unsigned count = cached.eventFootprint ? 1 : area.cellCount;
    cached.vertices.reserve(count * segments * 3);
    for (unsigned i = 0; i < count; ++i) {
        const auto position = cached.eventFootprint ? area.center : area.cells[i];
        if (!Finite(position))
            continue;
        const auto n = Normal(cached.eventFootprint ? Vector3{0, 0, 1} : area.cellNormals[i]);
        const auto tangent = Normal(Cross(n, std::abs(n.y) > .9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0}));
        const auto bitangent = Cross(n, tangent);
        const auto center = position + flight::Scale(n, std::min(1.2f, cached.radius * .048f));
        auto ring = [&](unsigned j) {
            const float a = j * 6.28318530718f / segments;
            return center + flight::Scale(tangent, std::cos(a) * cached.radius) +
                   flight::Scale(bitangent, std::sin(a) * cached.radius);
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
    stats_ = {};
    combined_.clear();
    if (!options.areas || !options.fireArea || (!options.areaFill && options.areaOutline <= 0))
        return S_FALSE;
    if (!std::isfinite(opacity) || world.areaCount > world.areas.size())
        return E_INVALIDARG;
    for (auto &entry : cache_)
        entry.used = false;
    for (std::size_t i = 0; i < world.areaCount; ++i) {
        const auto &area = world.areas[i];
        if (area.type != AreaType::Fire)
            continue;
        const bool eventFootprint = area.estimatedFootprint && !area.cellCount;
        const float radius = eventFootprint ? area.radius : area.cellRadius;
        if (!area.handle || (!area.cellCount && !eventFootprint) || area.cellCount > area.cells.size() ||
            !std::isfinite(radius) || radius <= 0 || radius > (eventFootprint ? 500.f : 64.f) ||
            !std::isfinite(area.remaining) || !std::isfinite(area.duration) ||
            (area.duration > 0 && area.remaining <= 0) ||
            (eventFootprint && (!Finite(area.center) || area.duration <= 0))) {
            ++stats_.rejected;
            continue;
        }
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
        if (!cached) {
            ++stats_.rejected;
            continue;
        }
        if (cached->handle != area.handle || cached->count != area.cellCount || cached->radius != radius ||
            cached->eventFootprint != eventFootprint ||
            (eventFootprint && std::memcmp(&cached->center, &area.center, sizeof(Vector3))) ||
            std::memcmp(cached->cells.data(), area.cells.data(), area.cellCount * sizeof(Vector3)) ||
            std::memcmp(cached->normals.data(), area.cellNormals.data(), area.cellCount * sizeof(Vector3)))
            Build(*cached, area);
        cached->used = true;
        if (combined_.size() + cached->vertices.size() > MaxEffectVertices) {
            ++stats_.rejected;
            break;
        }
        ++stats_.areas;
        stats_.cells += area.cellCount;
        stats_.eventFootprints += eventFootprint ? 1 : 0;
        const float fade = area.duration > 0 ? std::clamp(area.remaining / .6f, 0.f, 1.f) : 1.f;
        const auto first = combined_.size();
        combined_.insert(combined_.end(), cached->vertices.begin(), cached->vertices.end());
        for (std::size_t j = first; j < combined_.size(); ++j)
            combined_[j].alpha = fade;
    }
    for (auto &entry : cache_)
        if (!entry.used)
            entry.handle = 0;
    stats_.vertices = static_cast<unsigned>(combined_.size());
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
