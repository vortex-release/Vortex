#pragma once
#include "combat_features.hpp"
#include "entity_effects.hpp"
#include <cstring>
namespace awareness::combat {
struct AreaRenderStats {
    unsigned areas{}, cells{}, eventFootprints{}, rejected{}, vertices{};
};
// Cell-derived geometry is cached independently of camera movement and frame-rate.
class AreaRenderer {
    struct Cached {
        std::uint32_t handle{}, count{};
        float radius{};
        Vector3 center{};
        bool eventFootprint{};
        std::array<Vector3, 64> cells{}, normals{};
        std::vector<EffectVertex> vertices;
        bool used{};
    };
    std::array<Cached, 64> cache_;
    std::vector<EffectVertex> combined_;
    EntityEffects renderer_;
    unsigned rebuilds_{};
    AreaRenderStats stats_;
    void Build(Cached &, const Area &);

  public:
    HRESULT Render(ID3D11Device *, ID3D11DeviceContext *, ID3D11RenderTargetView *, const D3D11_TEXTURE2D_DESC &,
                   ID3D11DepthStencilView *, bool, const Matrix4x4 &, Viewport, const WorldSnapshot &, const Options &,
                   float opacity, EffectsState &);
    unsigned GeometryRebuilds() const noexcept { return rebuilds_; }
    const AreaRenderStats &State() const noexcept { return stats_; }
};
} // namespace awareness::combat
