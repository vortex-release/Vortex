#pragma once
#include "trajectory_style.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
namespace awareness::flight {
struct DepthStrokeVertex {
    ClipPoint position;
    Color color;
    float edge{}, along{}, halfWidth{}, glow{};
};
struct RenderStatus {
    bool depthAvailable{};
    unsigned vertices{}, drawCalls{};
    HRESULT result{S_FALSE};
};
// Owns bounded, reusable geometry/resources. Call inside the host-isolated context state.
class DepthRenderer {
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_, hidden_;
    Ptr<ID3D11InputLayout> layout_;
    Ptr<ID3D11Buffer> buffer_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11BlendState> blend_;
    Ptr<ID3D11DepthStencilState> front_, back_, frontReverse_, backReverse_;
    std::vector<DepthStrokeVertex> vertices_;
    unsigned capacity_{};
    HRESULT Initialize(ID3D11Device *);

  public:
    static constexpr unsigned MaxVertices = 196608;
    void Clear() noexcept { vertices_.clear(); }
    bool Stroke(Vector3, Vector3, const Matrix4x4 &, Viewport, Color, Color, float, float, float = 0, float = 0);
    HRESULT Render(ID3D11Device *, ID3D11DeviceContext *, ID3D11RenderTargetView *, ID3D11DepthStencilView *, bool,
                   Viewport, const Trails &, const Prediction &, const Tracers &, bool, bool, bool, Shots,
                   std::uint32_t, int, const Matrix4x4 &, double, float, const PathStyle &, RenderStatus &);
    std::span<const DepthStrokeVertex> Vertices() const noexcept { return vertices_; }
};
bool MatchingSceneDepth(ID3D11Device *, ID3D11RenderTargetView *, ID3D11DepthStencilView *) noexcept;
} // namespace awareness::flight
