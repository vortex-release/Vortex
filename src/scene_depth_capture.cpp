#include "scene_depth_capture.hpp"
#include "native_model_mask.hpp"
extern "C" void STDMETHODCALLTYPE VortexDrawIndexedInstancedShim(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
namespace awareness::scene_depth {
namespace {
using Microsoft::WRL::ComPtr;
using SetTargets = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                             ID3D11DepthStencilView *);
using SetTargetsUav = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *,
                                                ID3D11DepthStencilView *, UINT, UINT,
                                                ID3D11UnorderedAccessView *const *, const UINT *);
using ClearDepth = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11DepthStencilView *, UINT, FLOAT, UINT8);
using DrawIndexed = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, INT);
using Draw = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT);
using DrawIndexedInstanced = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
using DrawInstanced = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
using DrawAuto = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *);
using DrawIndirect = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11Buffer *, UINT);
using SetDepth = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11DepthStencilState *, UINT);
using QueryCall = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11Asynchronous *);
using SetViewports = void(STDMETHODCALLTYPE *)(ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);
struct Candidate {
    ComPtr<ID3D11DepthStencilView> view;
    ComPtr<ID3D11Texture2D> texture;
    float clear{1};
    unsigned clears{}, drawStart{};
    bool drawn{}, frozen{};
};
struct State {
    std::recursive_mutex mutex;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::array<void *, 14> addresses{};
    std::array<bool, 14> hooked{};
    SetTargets set{};
    SetTargetsUav setUav{};
    ClearDepth clear{};
    DrawIndexed drawIndexed{};
    Draw draw{};
    DrawIndexedInstanced drawIndexedInstanced{};
    DrawInstanced drawInstanced{};
    DrawAuto drawAuto{};
    DrawIndirect drawIndexedIndirect{}, drawIndirect{};
    SetDepth setDepth{};
    SetViewports setViewports{};
    QueryCall beginQuery{}, endQuery{};
    std::atomic<ID3D11DeviceContext *> watched{};
    std::atomic<bool> witnessEligible{}, observing{};
    std::atomic<unsigned> drawSerial{}, inFlight{};
    std::array<Candidate, 8> candidates;

    ComPtr<ID3D11DepthStencilState> previousDepth;
    bool previousDepthKnown{};
    Candidate *bound{}, *selected{};
    ComPtr<ID3D11Texture2D> copy;
    ComPtr<ID3D11DepthStencilView> view;
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    UINT width{}, height{}, samples{}, quality{};
    D3D11_COMPARISON_FUNC comparison{D3D11_COMPARISON_LESS};
    bool depthEnabled{}, viewportValid{}, paused{true}, fresh{}, reversed{}, frozen{}, ready{};
    unsigned copies{}, bindings{}, clears{}, firstDraw{};
} state;
struct Guard {
    Guard() { state.inFlight.fetch_add(1, std::memory_order_acquire); }
    ~Guard() { state.inFlight.fetch_sub(1, std::memory_order_release); }
};
bool Observing(ID3D11DeviceContext *context) noexcept {
    return !native_mask::Internal() && state.observing.load(std::memory_order_acquire) &&
           context == state.watched.load(std::memory_order_relaxed);
}
void Eligibility() {
    const auto *c = state.bound;
    const bool reverse = c && c->clear == 0;
    const bool comparison =
        state.comparison == D3D11_COMPARISON_EQUAL ||
        (reverse ? (state.comparison == D3D11_COMPARISON_GREATER || state.comparison == D3D11_COMPARISON_GREATER_EQUAL)
                 : (state.comparison == D3D11_COMPARISON_LESS || state.comparison == D3D11_COMPARISON_LESS_EQUAL));
    state.witnessEligible.store(state.ready && !state.paused && !state.frozen && c && c->clears && !c->frozen &&
                                    state.depthEnabled && state.viewportValid && comparison,
                                std::memory_order_relaxed);
}
Candidate *Find(ID3D11DepthStencilView *depth, bool create) {
    if (!depth)
        return nullptr;
    for (auto &c : state.candidates)
        if (c.view.Get() == depth)
            return &c;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    depth->GetResource(&resource);
    if (FAILED(resource.As(&texture)))
        return nullptr;
    for (auto &c : state.candidates)
        if (c.texture.Get() == texture.Get())
            return &c;
    if (!create)
        return nullptr;
    D3D11_TEXTURE2D_DESC d{};
    D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
    texture->GetDesc(&d);
    depth->GetDesc(&vd);
    const bool view = d.SampleDesc.Count == 1
                          ? (vd.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D && vd.Texture2D.MipSlice == 0)
                          : vd.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DMS;
    if (d.Width != state.width || d.Height != state.height || d.SampleDesc.Count > 16 || !d.SampleDesc.Count ||
        d.ArraySize != 1 || d.MipLevels != 1 || !view ||
        (vd.Format != DXGI_FORMAT_D32_FLOAT && vd.Format != DXGI_FORMAT_D24_UNORM_S8_UINT &&
         vd.Format != DXGI_FORMAT_D16_UNORM && vd.Format != DXGI_FORMAT_D32_FLOAT_S8X24_UINT))
        return nullptr;
    for (auto &c : state.candidates)
        if (!c.texture) {
            c.view = depth;
            c.texture = texture;
            return &c;
        }
    return nullptr;
}
// Never retain color views: a swap-chain RTV reference would block ResizeBuffers.
bool EligibleTarget(UINT count, ID3D11RenderTargetView *const *targets) {
    if (!count || !targets || !targets[0])
        return false;

    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    targets[0]->GetResource(&resource);
    if (FAILED(resource.As(&texture)))
        return false;
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    const bool color = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                       d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                       d.Format == DXGI_FORMAT_R11G11B10_FLOAT || d.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
    const bool eligible =
        color && d.Width == state.width && d.Height == state.height && d.SampleDesc.Count <= 16 && d.ArraySize == 1;

    return eligible;
}
void Consider(Candidate *c) {
    if (!c || !c->clears || c->frozen || state.frozen)
        return;
    if (c == state.bound && state.drawSerial.load(std::memory_order_relaxed) != c->drawStart)
        c->drawn = true;
    if (c->drawn) {
        state.selected = c;
        state.reversed = c->clear == 0;
    }
}
void CopySelected() {
    if (!state.selected || !state.selected->drawn || state.fresh)
        return;
    const auto &source = *state.selected;
    D3D11_TEXTURE2D_DESC d{};
    source.texture->GetDesc(&d);
    if (!state.copy || state.format != d.Format || state.samples != d.SampleDesc.Count ||
        state.quality != d.SampleDesc.Quality) {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11DepthStencilView> view;
        D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
        source.view->GetDesc(&vd);
        vd.Flags = 0;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.CPUAccessFlags = d.MiscFlags = 0;
        if (FAILED(state.device->CreateTexture2D(&d, nullptr, &texture)) ||
            FAILED(state.device->CreateDepthStencilView(texture.Get(), &vd, &view)))
            return;
        state.copy = texture;
        state.view = view;
        state.format = d.Format;
        state.samples = d.SampleDesc.Count;
        state.quality = d.SampleDesc.Quality;
    }
    state.context->CopyResource(state.copy.Get(), source.texture.Get());
    state.fresh = true;
    ++state.copies;
}
void DepthState(ID3D11DepthStencilState *depth) {
    if (state.previousDepthKnown && state.previousDepth.Get() == depth) {
        Eligibility();
        return;
    }
    state.previousDepth = depth;
    state.previousDepthKnown = true;
    D3D11_DEPTH_STENCIL_DESC d{};
    if (depth)
        depth->GetDesc(&d);
    else {
        d.DepthEnable = TRUE;
        d.DepthFunc = D3D11_COMPARISON_LESS;
    }
    state.depthEnabled = d.DepthEnable != FALSE;
    state.comparison = d.DepthFunc;
    Eligibility();
}
void ViewportState(UINT count, const D3D11_VIEWPORT *v) {
    state.viewportValid = count == 1 && v && v[0].TopLeftX == 0 && v[0].TopLeftY == 0 &&
                          std::abs(v[0].Width - state.width) < .5f && std::abs(v[0].Height - state.height) < .5f &&
                          v[0].MinDepth == 0 && v[0].MaxDepth == 1;
    Eligibility();
}
void AfterBind(ID3D11DeviceContext *context, UINT count, ID3D11RenderTargetView *const *targets,
               ID3D11DepthStencilView *depth) {
    if (context != state.context.Get() || state.paused)
        return;
    state.bound = EligibleTarget(count, targets) ? Find(depth, true) : nullptr;
    if (state.bound) {
        state.bound->drawStart = state.drawSerial.load(std::memory_order_relaxed);
        ++state.bindings;
    }
    Eligibility();
}
void Witness(ID3D11DeviceContext *context, UINT count) noexcept {
    if (!native_mask::Internal() && count >= 3 && state.witnessEligible.load(std::memory_order_relaxed) &&
        context == state.watched.load(std::memory_order_relaxed))
        state.drawSerial.fetch_add(1, std::memory_order_relaxed);
}
void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext *c, UINT n, UINT s, INT b) {
    Guard g;
    native_mask::Observe(c, {native_mask::DrawKind::Indexed, {n, s, static_cast<UINT>(b)}});
    Witness(c, n);
    state.drawIndexed(c, n, s, b);
}
void STDMETHODCALLTYPE DrawHook(ID3D11DeviceContext *c, UINT n, UINT s) {
    Guard g;
    native_mask::Observe(c, {native_mask::DrawKind::Vertices, {n, s}});
    Witness(c, n);
    state.draw(c, n, s);
}
extern "C" void STDMETHODCALLTYPE VortexDrawIndexedInstancedDispatch(ID3D11DeviceContext *c, UINT n, UINT i, UINT s,
                                                                     INT b, UINT f, std::uintptr_t caller,
                                                                     std::uintptr_t payload) {
    Guard g;
    native_mask::Observe(c, {native_mask::DrawKind::IndexedInstanced, {n, i, s, static_cast<UINT>(b), f}}, caller,
                         payload);
    if (i)
        Witness(c, n);
    state.drawIndexedInstanced(c, n, i, s, b, f);
}
void STDMETHODCALLTYPE DrawInstancedHook(ID3D11DeviceContext *c, UINT n, UINT i, UINT s, UINT f) {
    Guard g;
    native_mask::Observe(c, {native_mask::DrawKind::Instanced, {n, i, s, f}});
    if (i)
        Witness(c, n);
    state.drawInstanced(c, n, i, s, f);
}
void STDMETHODCALLTYPE DrawAutoHook(ID3D11DeviceContext *c) {
    Guard g;
    native_mask::Observe(c, {native_mask::DrawKind::Auto});
    Witness(c, 3);
    state.drawAuto(c);
}
void STDMETHODCALLTYPE DrawIndexedIndirectHook(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    Guard g;
    native_mask::Observe(c, {native_mask::DrawKind::IndexedIndirect, {o}, b});
    Witness(c, 3);
    state.drawIndexedIndirect(c, b, o);
}
void STDMETHODCALLTYPE DrawIndirectHook(ID3D11DeviceContext *c, ID3D11Buffer *b, UINT o) {
    Guard g;
    native_mask::Observe(c, {native_mask::DrawKind::Indirect, {o}, b});
    Witness(c, 3);
    state.drawIndirect(c, b, o);
}
void STDMETHODCALLTYPE BeginQueryHook(ID3D11DeviceContext *c, ID3D11Asynchronous *q) {
    Guard guard;
    native_mask::Query(c, q, true);
    state.beginQuery(c, q);
}
void STDMETHODCALLTYPE EndQueryHook(ID3D11DeviceContext *c, ID3D11Asynchronous *q) {
    Guard guard;
    state.endQuery(c, q);
    native_mask::Query(c, q, false);
}
void STDMETHODCALLTYPE SetHook(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *r,
                               ID3D11DepthStencilView *d) {
    Guard g;
    if (!Observing(c)) {
        state.set(c, n, r, d);
        return;
    }
    std::lock_guard lock(state.mutex);
    if (c == state.context.Get() && !state.paused)
        Consider(state.bound);
    state.set(c, n, r, d);
    AfterBind(c, n, r, d);
}
void STDMETHODCALLTYPE UavHook(ID3D11DeviceContext *c, UINT n, ID3D11RenderTargetView *const *r,
                               ID3D11DepthStencilView *d, UINT f, UINT k, ID3D11UnorderedAccessView *const *u,
                               const UINT *v) {
    Guard g;
    if (!Observing(c) || n == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
        state.setUav(c, n, r, d, f, k, u, v);
        return;
    }
    std::lock_guard lock(state.mutex);
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL && c == state.context.Get() && !state.paused)
        Consider(state.bound);
    state.setUav(c, n, r, d, f, k, u, v);
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        AfterBind(c, n, r, d);
}
void STDMETHODCALLTYPE ClearHook(ID3D11DeviceContext *c, ID3D11DepthStencilView *d, UINT f, FLOAT value,
                                 UINT8 stencil) {
    Guard g;
    if (!Observing(c) || !(f & D3D11_CLEAR_DEPTH)) {
        state.clear(c, d, f, value, stencil);
        return;
    }
    std::lock_guard lock(state.mutex);
    if (c == state.context.Get() && !state.paused && (f & D3D11_CLEAR_DEPTH))
        if (auto *candidate = Find(d, true)) {
            ++state.clears;
            if (candidate->clears) {
                Consider(candidate);
                if (state.selected == candidate) {
                    CopySelected();
                    if (state.fresh) {
                        state.frozen = true;
                        state.observing.store(false, std::memory_order_release);
                    }
                }
                candidate->frozen = true;
            }
            if (value == 0 || value == 1) {
                candidate->clear = value;
                ++candidate->clears;
                candidate->drawStart = state.drawSerial.load(std::memory_order_relaxed);
            } else
                candidate->frozen = true;
            Eligibility();
        }
    state.clear(c, d, f, value, stencil);
}
void STDMETHODCALLTYPE DepthHook(ID3D11DeviceContext *c, ID3D11DepthStencilState *d, UINT ref) {
    Guard g;
    if (!Observing(c)) {
        state.setDepth(c, d, ref);
        return;
    }
    std::lock_guard lock(state.mutex);
    state.setDepth(c, d, ref);
    if (c == state.context.Get() && !state.paused)
        DepthState(d);
}
void STDMETHODCALLTYPE ViewportsHook(ID3D11DeviceContext *c, UINT n, const D3D11_VIEWPORT *v) {
    Guard g;
    if (!Observing(c)) {
        state.setViewports(c, n, v);
        return;
    }
    std::lock_guard lock(state.mutex);
    state.setViewports(c, n, v);
    if (c == state.context.Get() && !state.paused)
        ViewportState(n, v);
}
} // namespace
HRESULT Start(ID3D11Device *device, ID3D11DeviceContext *context, UINT width, UINT height) noexcept {
    if (!device || !context || !width || !height || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return E_INVALIDARG;
    try {
        auto **table = *reinterpret_cast<void ***>(context);
        const std::array<void *, 14> addresses{table[33], table[34], table[53], table[12], table[13],
                                               table[20], table[21], table[38], table[39], table[40],
                                               table[36], table[44], table[27], table[28]};
        bool changed{};
        {
            std::lock_guard lock(state.mutex);
            changed = std::any_of(state.hooked.begin(), state.hooked.end(), [](bool value) { return value; }) &&
                      addresses != state.addresses;
        }
        // D3D11 can specialize its context vtable after the first resource use,
        // and a replacement device can use another implementation. Rebind only
        // at this frame boundary, after draining the previous callbacks.
        if (changed) {
            const auto stopped = Stop();
            if (FAILED(stopped))
                return stopped;
        }
        std::unique_lock lock(state.mutex);
        if (state.context.Get() != context || state.width != width || state.height != height) {
            state.paused = true;
            state.observing.store(false, std::memory_order_release);
            state.previousDepthKnown = false;
            state.previousDepth.Reset();
            state.witnessEligible = false;
            state.candidates = {};
            state.bound = state.selected = nullptr;
            state.copy.Reset();
            state.view.Reset();
            state.fresh = false;
        }
        state.device = device;
        state.context = context;
        state.watched = context;
        state.width = width;
        state.height = height;
        if (state.ready)
            return S_OK;
        state.addresses = addresses;
        void *hooks[]{reinterpret_cast<void *>(&SetHook),
                      reinterpret_cast<void *>(&UavHook),
                      reinterpret_cast<void *>(&ClearHook),
                      reinterpret_cast<void *>(&DrawIndexedHook),
                      reinterpret_cast<void *>(&DrawHook),
                      reinterpret_cast<void *>(&VortexDrawIndexedInstancedShim),
                      reinterpret_cast<void *>(&DrawInstancedHook),
                      reinterpret_cast<void *>(&DrawAutoHook),
                      reinterpret_cast<void *>(&DrawIndexedIndirectHook),
                      reinterpret_cast<void *>(&DrawIndirectHook),
                      reinterpret_cast<void *>(&DepthHook),
                      reinterpret_cast<void *>(&ViewportsHook),
                      reinterpret_cast<void *>(&BeginQueryHook),
                      reinterpret_cast<void *>(&EndQueryHook)};
        void **originals[]{reinterpret_cast<void **>(&state.set),
                           reinterpret_cast<void **>(&state.setUav),
                           reinterpret_cast<void **>(&state.clear),
                           reinterpret_cast<void **>(&state.drawIndexed),
                           reinterpret_cast<void **>(&state.draw),
                           reinterpret_cast<void **>(&state.drawIndexedInstanced),
                           reinterpret_cast<void **>(&state.drawInstanced),
                           reinterpret_cast<void **>(&state.drawAuto),
                           reinterpret_cast<void **>(&state.drawIndexedIndirect),
                           reinterpret_cast<void **>(&state.drawIndirect),
                           reinterpret_cast<void **>(&state.setDepth),
                           reinterpret_cast<void **>(&state.setViewports),
                           reinterpret_cast<void **>(&state.beginQuery),
                           reinterpret_cast<void **>(&state.endQuery)};
        const auto rollback = [&] {
            lock.unlock();
            const auto cleanup = Stop();
            return FAILED(cleanup) ? cleanup : E_FAIL;
        };
        for (std::size_t i = 0; i < state.hooked.size(); ++i)
            if (!state.hooked[i]) {
                if (MH_CreateHook(state.addresses[i], hooks[i], originals[i]) != MH_OK)
                    return rollback();
                // Track creation before activation so rollback also owns disabled
                // hooks. MinHook freezes all process threads for each ApplyQueued;
                // enabling fourteen hooks separately produced fourteen global pauses.
                state.hooked[i] = true;
            }
        for (std::size_t i = 0; i < state.hooked.size(); ++i)
            if (state.hooked[i] && MH_QueueEnableHook(state.addresses[i]) != MH_OK)
                return rollback();
        if (MH_ApplyQueued() != MH_OK)
            return rollback();
        state.ready = true;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
Snapshot BeginOverlay() noexcept {
    state.observing.store(false, std::memory_order_release);
    std::lock_guard lock(state.mutex);
    state.witnessEligible = false;
    if (!state.paused && state.ready) {
        Consider(state.bound);
        CopySelected();
    }
    state.paused = true;
    return {state.fresh ? state.view : nullptr,
            state.reversed,
            state.copies,
            state.bindings,
            state.clears,
            state.drawSerial.load(std::memory_order_relaxed) - state.firstDraw};
}
void EndOverlay() noexcept {
    std::lock_guard lock(state.mutex);
    state.fresh = state.frozen = false;
    state.copies = state.bindings = state.clears = 0;
    state.firstDraw = state.drawSerial.load(std::memory_order_relaxed);
    state.bound = state.selected = nullptr;
    state.candidates = {};
    state.paused = !state.ready;
    state.previousDepthKnown = false;
    if (!state.ready)
        return;
    ComPtr<ID3D11DepthStencilState> ds;
    UINT ref{};
    state.context->OMGetDepthStencilState(&ds, &ref);
    DepthState(ds.Get());
    UINT n = 1;
    D3D11_VIEWPORT v{};
    state.context->RSGetViewports(&n, &v);
    ViewportState(n, &v);
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    state.context->OMGetRenderTargets(1, &target, &depth);
    auto *rt = target.Get();
    AfterBind(state.context.Get(), 1, &rt, depth.Get());
    state.observing.store(true, std::memory_order_release);
}
void DiscardFrame() noexcept {
    native_mask::Discard();
    state.observing.store(false, std::memory_order_release);
    std::lock_guard lock(state.mutex);
    state.witnessEligible = false;
    state.paused = true;
    state.fresh = state.frozen = false;
    state.copies = state.bindings = state.clears = 0;
    state.firstDraw = state.drawSerial.load(std::memory_order_relaxed);
    state.bound = state.selected = nullptr;
    state.candidates = {};
}
HRESULT Stop() noexcept {
    native_mask::Discard();
    state.observing.store(false, std::memory_order_release);
    {
        std::lock_guard lock(state.mutex);
        state.paused = true;
        state.ready = false;
        state.witnessEligible = false;
    }
    bool queued{};
    for (std::size_t i = 0; i < state.hooked.size(); ++i)
        if (state.hooked[i]) {
            if (MH_QueueDisableHook(state.addresses[i]) != MH_OK)
                return E_FAIL;
            queued = true;
        }
    if (queued && MH_ApplyQueued() != MH_OK)
        return E_FAIL;
    const auto deadline = GetTickCount64() + 5000;
    while (state.inFlight) {
        if (GetTickCount64() > deadline)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    std::lock_guard lock(state.mutex);
    for (std::size_t i = 0; i < state.hooked.size(); ++i)
        if (state.hooked[i]) {
            if (MH_RemoveHook(state.addresses[i]) != MH_OK)
                return E_FAIL;
            state.hooked[i] = false;
        }
    state.watched = nullptr;
    state.previousDepthKnown = false;
    state.previousDepth.Reset();
    state.context.Reset();
    state.device.Reset();
    state.candidates = {};
    state.bound = state.selected = nullptr;
    state.copy.Reset();
    state.view.Reset();
    state.fresh = false;
    return S_OK;
}
} // namespace awareness::scene_depth
