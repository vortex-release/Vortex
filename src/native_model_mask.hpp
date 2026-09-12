#pragma once
#include <awareness/EffectsApi.hpp>
#include <d3d11.h>
#include <array>
#include <cstdint>
namespace awareness::native_mask {
enum class DrawKind { Indexed, Vertices, IndexedInstanced, Instanced, Auto, IndexedIndirect, Indirect };
struct Draw {
    DrawKind kind{};
    std::array<UINT, 5> args{};
    ID3D11Buffer *indirect{};
};
// Native scene scope; does not retain the packet, entity, mesh or GPU bindings.
class Scope {
    std::uint32_t previous_{};

  public:
    explicit Scope(std::uint32_t color) noexcept;
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};
bool Internal() noexcept;
bool Enabled() noexcept;
std::uint32_t SelectedColor() noexcept;
std::uint64_t Generation() noexcept;
void Forget(std::uintptr_t command) noexcept;
// Queued annotations contain only numeric command identity, draw arguments and color.
bool Annotate(std::uintptr_t command, const std::array<UINT, 5> &, std::uint32_t color) noexcept;
void Observe(ID3D11DeviceContext *, const Draw &, std::uintptr_t returnAddress = 0,
             std::uintptr_t replayPayload = 0) noexcept;
void SetReplaySite(std::uintptr_t returnAddress) noexcept;
void Query(ID3D11DeviceContext *, ID3D11Asynchronous *, bool begin) noexcept;
// Called inside the isolated overlay context state, after current scene depth resolve.
// Composites the completed native mask and clears/arms the next frame. Owns no backbuffer refs.
HRESULT Render(ID3D11Device *, ID3D11DeviceContext *, ID3D11RenderTargetView *, const D3D11_TEXTURE2D_DESC &,
               ID3D11DepthStencilView *, bool reversed, bool enabled, EffectsState &) noexcept;
void Discard() noexcept;
// Call after all draw/native callbacks have been disabled and drained.
void Release() noexcept;
struct Diagnostics {
    unsigned captured{}, queued{}, matched{}, dropped{};
};
Diagnostics GetDiagnostics() noexcept;
} // namespace awareness::native_mask
