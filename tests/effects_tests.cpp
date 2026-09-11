#include "entity_effects.hpp"
#include "../demo/model_geometry.hpp"
#include <cstdio>
#include <stdexcept>
#include <numeric>
#include <cstring>
using namespace awareness;
using Microsoft::WRL::ComPtr;
int main() {
    int failures{};
    const auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL %s\n", why);
        }
    };
    const auto require = [](HRESULT hr) {
        if (FAILED(hr))
            throw std::runtime_error("D3D11 test fixture failed");
    };
    try {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                                  nullptr, &context));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 96;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> color, depth;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11DepthStencilView> dsv;
        require(device->CreateTexture2D(&desc, nullptr, &color));
        require(device->CreateRenderTargetView(color.Get(), nullptr, &target));
        auto depthDesc = desc;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        require(device->CreateTexture2D(&depthDesc, nullptr, &depth));
        require(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv));
        const auto read = [&](ID3D11Texture2D *texture) {
            D3D11_TEXTURE2D_DESC d{};
            texture->GetDesc(&d);
            d.BindFlags = 0;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> stage;
            require(device->CreateTexture2D(&d, nullptr, &stage));
            context->CopyResource(stage.Get(), texture);
            D3D11_MAPPED_SUBRESOURCE map{};
            require(context->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &map));
            std::vector<unsigned char> pixels(96 * 96 * 4);
            for (unsigned y = 0; y < 96; ++y)
                std::memcpy(pixels.data() + y * 96 * 4, static_cast<unsigned char *>(map.pData) + y * map.RowPitch,
                            96 * 4);
            context->Unmap(stage.Get(), 0);
            return pixels;
        };
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, .5f, 37);
        const auto originalDepth = read(depth.Get());
        Matrix4x4 matrix{};
        for (int i = 0; i < 4; ++i)
            matrix.m[i][i] = 1;
        const Viewport viewport{0, 0, 96, 96};
        std::vector<EffectVertex> vertices;
        const auto quad = [&](float x0, float x1, float z) {
            Vector3 p[4]{{x0, -.4f, z}, {x0, .4f, z}, {x1, -.4f, z}, {x1, .4f, z}};
            for (int i : {0, 1, 2, 1, 3, 2})
                vertices.push_back({p[i], 1});
        };
        quad(-.8f, -.2f, .2f);
        quad(.2f, .8f, .7f);
        EntityEffects renderer;
        EffectsConfiguration config;
        EffectsState state;
        config.materialEnabled = 1;
        config.materialColor = {1, 0, 0, 1};
        const auto render = [&](ID3D11DepthStencilView *view, bool reversed = false) {
            const float clear[4]{};
            context->ClearRenderTargetView(target.Get(), clear);
            state = {};
            const auto hr = renderer.Render(device.Get(), context.Get(), target.Get(), desc, view, reversed, matrix,
                                            viewport, vertices, config, state);
            check(SUCCEEDED(hr), "effect shaders compile and rendering succeeds");
            context->OMSetRenderTargets(0, nullptr, nullptr);
            return read(color.Get());
        };
        const auto pixel = [](const auto &p, int x, int y) { return p[(y * 96 + x) * 4]; };
        auto all = render(dsv.Get());
        check(pixel(all, 24, 48) > 240 && pixel(all, 72, 48) > 240, "always-visible fills front and behind geometry");
        check(renderer.StencilAllocations() == 1, "simple fill creates one private stencil surface");
        const auto stableVertices = vertices;
        vertices.insert(vertices.end(), stableVertices.begin(), stableVertices.end());
        config.materialColor.a = .25f;
        auto once = render(dsv.Get());
        check(pixel(once, 24, 48) > 55 && pixel(once, 24, 48) < 70,
              "stencil prevents double blending of overlapping faces");
        render(dsv.Get());
        check(renderer.StencilAllocations() == 1, "steady-state fill reuses stencil resource");
        vertices = stableVertices;
        config.materialColor.a = 1;
        config.visibility = EffectVisibility::OccludedOnly;
        auto occluded = render(dsv.Get());
        check(pixel(occluded, 24, 48) == 0 && pixel(occluded, 72, 48) > 240,
              "standard depth renders behind geometry only");
        auto reverse = render(dsv.Get(), true);
        check(pixel(reverse, 24, 48) > 240 && pixel(reverse, 72, 48) == 0,
              "reverse Z uses the inverse depth comparison");
        auto missing = render(nullptr);
        check(state.status == EffectsStatus::DepthUnavailable &&
                  std::accumulate(missing.begin(), missing.end(), 0u) == 0,
              "occluded-only fails closed without scene depth");
        const auto originalVertices = vertices;
        // One continuous surface crosses behind an obstruction: both colors must appear on that surface.
        vertices = {{{-.8f, -.4f, .2f}, 1}, {{-.8f, .4f, .2f}, 1}, {{.8f, -.4f, .8f}, 1},
                    {{-.8f, .4f, .2f}, 1},  {{.8f, .4f, .8f}, 1},  {{.8f, -.4f, .8f}, 1}};
        config.visibility = EffectVisibility::TwoColor;
        config.materialColor = {1, 1, 1, 1};
        config.glowColor = {.65f, .1f, .95f, 1};
        const auto green = [](const auto &p, int x, int y) { return p[(y * 96 + x) * 4 + 1]; };
        auto split = render(dsv.Get());
        check(green(split, 24, 48) > 240 && green(split, 72, 48) < 40 && pixel(split, 72, 48) > 150,
              "one partially obstructed model fills white in front and purple behind");
        auto splitReverse = render(dsv.Get(), true);
        check(green(splitReverse, 24, 48) < 40 && green(splitReverse, 72, 48) > 240,
              "two-color fill supports reverse depth");
        quad(.1f, .7f, .2f);
        auto overlap = render(dsv.Get());
        check(green(overlap, 65, 48) > 240, "visible geometry takes precedence over overlapping hidden geometry");
        vertices.clear();
        quad(-.8f, .8f, .5f);
        auto equal = render(dsv.Get());
        check(green(equal, 48, 48) > 240, "depth-equal surface stays visible rather than self-occluding");
        config.materialColor.a = .25f;
        auto alpha = render(dsv.Get());
        check(green(alpha, 48, 48) > 55 && green(alpha, 48, 48) < 70, "visible opacity independently controls fill");
        auto splitMissing = render(nullptr);
        check(state.status == EffectsStatus::DepthUnavailable &&
                  std::accumulate(splitMissing.begin(), splitMissing.end(), 0u) == 0,
              "two colors cannot guess visibility when depth is absent");
        vertices = originalVertices;
        config.materialColor = {1, 0, 0, 1};
        config.visibility = EffectVisibility::AlwaysVisible;
        config.materialEnabled = 0;
        config.glowEnabled = 1;
        config.glowColor = {1, 0, 0, 1};
        config.glowWidth = 3;
        auto outline = render(nullptr);
        check(pixel(outline, 24, 48) == 0 && pixel(outline, 8, 48) > 0,
              "glow is an outer boundary with no interior fill");
        const auto sum = [](const auto &p) {
            std::uint64_t result{};
            for (std::size_t i = 0; i < p.size(); i += 4)
                result += p[i];
            return result;
        };
        config.glowWidth = 10;
        auto thick = render(nullptr);
        check(sum(thick) > sum(outline) * 2, "thickness expands the glowing boundary");
        config.glowColor.a = .25f;
        auto transparent = render(nullptr);
        check(sum(transparent) < sum(thick) * .3, "glow opacity controls compositing");
        config.glowEnabled = 0;
        auto disabled = render(nullptr);
        check(state.status == EffectsStatus::Disabled && sum(disabled) == 0, "toggles bypass GPU output");
        check(originalDepth == read(depth.Get()), "all passes preserve every scene depth and stencil byte");
        config.materialEnabled = 1;
        auto multisampled = desc;
        multisampled.SampleDesc.Count = 4;
        check(renderer.Render(device.Get(), context.Get(), target.Get(), multisampled, nullptr, false, matrix, viewport,
                              vertices, config, state) == S_FALSE &&
                  state.status == EffectsStatus::UnsupportedTarget,
              "unsupported MSAA target is explicit");
        check(renderer.Render(nullptr, context.Get(), target.Get(), desc, nullptr, false, matrix, viewport, vertices,
                              config, state) == E_INVALIDARG,
              "missing device is rejected before graphics access");
        auto malformed = vertices;
        malformed.pop_back();
        check(renderer.Render(device.Get(), context.Get(), target.Get(), desc, nullptr, false, matrix, viewport,
                              malformed, config, state) == E_INVALIDARG,
              "incomplete triangles are rejected before buffer upload");
        auto zeroViewport = viewport;
        zeroViewport.width = 0;
        check(renderer.Render(device.Get(), context.Get(), target.Get(), desc, nullptr, false, matrix, zeroViewport,
                              vertices, config, state) == E_INVALIDARG,
              "empty viewport is rejected before division and drawing");
        // The geometry interface copies caller data, preserves exact triangle shapes, and rejects invalid batches
        // atomically.
        Vector3 triangle[]{{-.6f, -.6f, .2f}, {0, .6f, .2f}, {.6f, -.6f, .2f}};
        EffectMesh mesh{42, 0, 3};
        EffectsInput input;
        input.vertices = triangle;
        input.vertexCount = 3;
        input.meshes = &mesh;
        input.meshCount = 1;
        EffectGeometryData data;
        check(CopyEffectGeometry(input, data), "valid mesh batch accepted");
        triangle[0].x = 99;
        check(data.vertices[0].x == -.6f, "caller mesh storage is copied");
        mesh.vertexCount = 4;
        check(!CopyEffectGeometry(input, data) && data.meshes[0].vertexCount == 3,
              "invalid triangles rejected atomically");
        EntitySnapshot entity;
        entity.id = 42;
        entity.origin = {0, 0, 0};
        entity.mins = {-.8f, -.8f, .1f};
        entity.maxs = {.8f, .8f, .3f};
        vertices.clear();
        state = {};
        AppendEffectGeometry(vertices, entity, 1, &data, EffectGeometry::BoundsFallback, state);
        check(vertices.size() == 3 && state.meshCount == 1 && !state.boundsCount,
              "supplied exact mesh takes precedence over bounds");
        auto exact = render(nullptr);
        check(pixel(exact, 48, 48) > 240 && pixel(exact, 24, 24) == 0, "rasterization respects exact triangle shape");
        vertices.clear();
        state = {};
        AppendEffectGeometry(vertices, entity, 1, nullptr, EffectGeometry::BoundsFallback, state);
        check(vertices.empty() && !state.boundsCount, "legacy bounds mode never produces a glowing box");
        vertices.clear();
        AppendEffectGeometry(vertices, entity, 1, nullptr, EffectGeometry::MeshOnly, state);
        check(vertices.empty(), "mesh-only never fabricates silhouettes");
        FrameSnapshot pose;
        pose.entityCount = 1;
        pose.entities[0] = entity;
        pose.entities[0].valid = 1;
        pose.entities[0].health = 100;
        pose.entities[0].origin = {0, -.9f, .5f};
        const auto model = demo::MakeModelGeometry(pose);
        EffectGeometryData modelData{model.vertices, model.meshes};
        state = {};
        vertices.clear();
        AppendEffectGeometry(vertices, pose.entities[0], 1, &modelData, EffectGeometry::MeshOnly, state);
        config.materialEnabled = config.glowEnabled = 1;
        config.materialColor = config.glowColor = {1, 1, 1, 1};
        config.glowWidth = 1;
        auto silhouette = render(nullptr);
        check(pixel(silhouette, 48, 31) > 240, "combined character effect fills the torso");
        check(pixel(silhouette, 48, 85) == 0 && pixel(silhouette, 30, 8) == 0,
              "character silhouette preserves leg gaps and empty bounding-box corners");
        context->ClearState();
        ComPtr<ID3D11Device> replacement;
        ComPtr<ID3D11DeviceContext> replacementContext;
        require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                  &replacement, nullptr, &replacementContext));
        device = replacement;
        context = replacementContext;
        target.Reset();
        color.Reset();
        require(device->CreateTexture2D(&desc, nullptr, &color));
        require(device->CreateRenderTargetView(color.Get(), nullptr, &target));
        auto recovered = render(nullptr);
        check(pixel(recovered, 48, 31) > 240,
              "device replacement recreates effects resources even when target dimensions are unchanged");
        context->ClearState();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    std::printf("DX11 material / depth / glow / mesh tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
