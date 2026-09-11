#pragma once
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <array>
#include <awareness/InputBinding.hpp>
#include "assist_input_state.hpp"
struct ImGuiIO;
class WindowBridge {
    HWND window_{};
    WNDPROC original_{};
    UINT controlMessage_{};
    UINT_PTR tickTimer_{};
    void (*tick_)(void *) noexcept {};
    void *tickContext_{};
    std::atomic<bool> visible_{true}, attached_{false}, resetInput_{false};
    std::atomic<unsigned> menuToggle_{}, overlayToggle_{};
    std::array<bool, 256> keys_{};
    std::atomic<UINT> overlayKey_{VK_HOME}, alternateKey_{};
    std::atomic<UINT> trackingKey_{VK_RBUTTON}, wheelKey_{};
    std::atomic<ULONGLONG> wheelUntil_{};
    awareness::binding::Capture capture_;
    std::mutex inputMutex_;
    awareness::assist::PhysicalInput physical_;
    std::atomic<unsigned> assistRepeats_{};
    struct Event {
        UINT message;
        WPARAM wparam;
        LPARAM lparam;
    };
    std::vector<Event> events_;
    std::vector<Event> frameEvents_; // Render-thread consumer; capacity is retained across frames.
    bool cursorReleased_{}, oldRelative_{};
    int cursorShowCalls_{};
    RECT oldClip_{};
    void *sdlWindow_{};
    bool(__cdecl *sdl3Set_)(void *, bool){};
    int(__cdecl *sdl2Set_)(int){};
    void ApplyCursor(bool open) noexcept;
    HRESULT DetachOnWindowThread() noexcept;
    static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM) noexcept;

  public:
    void SetTick(void (*callback)(void *) noexcept, void *context) noexcept {
        if (!attached_) {
            tick_ = callback;
            tickContext_ = context;
        }
    }
    HRESULT Attach(HWND) noexcept;
    HRESULT Detach() noexcept;
    void SetVisible(bool open) noexcept;
    bool Visible() const noexcept { return visible_.load(); }
    bool Attached() const noexcept { return attached_.load(); }
    bool TakeMenuToggle() noexcept { return (menuToggle_.exchange(0) & 1) != 0; }
    bool TakeOverlayToggle() noexcept { return (overlayToggle_.exchange(0) & 1) != 0; }
    void SetOverlayKeys(UINT first, UINT second) noexcept {
        overlayKey_ = first;
        alternateKey_ = second;
    }
    void SetTrackingKey(UINT key) noexcept { trackingKey_ = key; }
    void BeginBindCapture() noexcept;
    bool WaitingForBind() noexcept;
    UINT TakeBinding() noexcept;
    bool BindingHeld(UINT key) const noexcept;
    awareness::assist::Keys AssistKeys() noexcept {
        std::scoped_lock lock(inputMutex_);
        return physical_.Snapshot();
    }
    void SuppressAssistRepeats(unsigned mask) noexcept { assistRepeats_ = mask; }
    void FeedInput(ImGuiIO &, float width, float height);
};
