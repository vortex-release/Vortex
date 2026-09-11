#pragma once
#include "assist_reader.hpp"
#include "assist_input_state.hpp"
#include "window_bridge.hpp"
#include "frame_clock.hpp"
#include <thread>
#include <condition_variable>
#include <mmsystem.h>
namespace awareness::assist {
struct Request {
    Options options;
    Addresses addresses;
    HWND window{};
    ULONGLONG deadline{};
    unsigned trackingKey{};
    bool trackingEnabled{}, enabled{};
};
class Runtime {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    Request request_;
    Diagnostics diagnostics_;
    WindowBridge *bridge_{};
    bool Allowed(const Request &r) const noexcept {
        if (!bridge_ || !bridge_->Attached() || bridge_->Visible() || !r.window || GetForegroundWindow() != r.window)
            return false;
        CURSORINFO cursor{sizeof(cursor)};
        return GetCursorInfo(&cursor) && !(cursor.flags & CURSOR_SHOWING);
    }
    struct Backend {
        Runtime &owner;
        const Request &request;
        const Sample &sample;
        bool Ready() {
            return GetTickCount64() <= request.deadline && owner.Allowed(request) &&
                   !owner.bridge_->AssistKeys().textInput;
        }
        bool Key(unsigned key, bool down) {
            if (down && !Ready())
                return false;
            if (down && key == Space) {
                cs2::LocalMemory local;
                Sample current;
                if (!cs2::ReadAssistSample({&local, cs2::LocalMemory::Read}, request.addresses, current) ||
                    current.owner != sample.owner || !current.grounded || !Movable(current))
                    return false;
            }
            INPUT i{};
            i.type = INPUT_KEYBOARD;
            const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
            if (!scan)
                return false;
            i.ki.wScan = static_cast<WORD>(scan & 0xff);
            i.ki.dwFlags =
                KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP) | ((scan & 0xff00) ? KEYEVENTF_EXTENDEDKEY : 0);
            i.ki.dwExtraInfo = InputTag;
            return SendInput(1, &i, sizeof(i)) == 1;
        }
        bool Mouse(bool down) {
            if (down) {
                if (!Ready())
                    return false;
                cs2::LocalMemory local;
                Sample current;
                if (!cs2::ReadAssistSample({&local, cs2::LocalMemory::Read}, request.addresses, current) ||
                    current.owner != sample.owner || current.weaponHandle != sample.weaponHandle ||
                    current.target != sample.target || !CrosshairEnemy(current) || !current.weaponReady)
                    return false;
            }
            INPUT i{};
            i.type = INPUT_MOUSE;
            i.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            i.mi.dwExtraInfo = InputTag;
            return SendInput(1, &i, sizeof(i)) == 1;
        }
        bool Yaw(const Sample &expected, float yaw) {
            if (!Ready())
                return false;
            cs2::LocalMemory local;
            cs2::Memory m{&local, cs2::LocalMemory::Read};
            std::uintptr_t pawn{};
            std::uint32_t handle{};
            if (!m.Read(request.addresses.pawnSlot, pawn) || !cs2::FullHandle(m, pawn, handle) ||
                handle != expected.owner)
                return false;
            if (std::abs(NormalizeYaw(yaw - expected.yaw)) < .00001f)
                return true;
            return cs2::CommitViewAngles(request.addresses.angles, {expected.pitch, expected.yaw, 0},
                                         {-expected.pitch, yaw}) == S_OK;
        }
    };
    void Run() noexcept {
        Controller controller;
        Request last;
        Sample sample;
        bool precision{};
        while (!stop_) {
            Request r;
            {
                std::scoped_lock lock(mutex_);
                r = request_;
            }
            last = r;
            sample = {};
            const auto keys = bridge_->AssistKeys();
            const bool configured = r.options.shoot || r.options.jumper || r.options.strafer;
            const bool controlsSafe = GetTickCount64() <= r.deadline && Allowed(r) && !keys.textInput;
            bool active = r.enabled && configured && controlsSafe;
            if (active && !precision)
                precision = timeBeginPeriod(1) == TIMERR_NOERROR;
            if (!active && precision) {
                timeEndPeriod(1);
                precision = false;
            }
            if (active) {
                cs2::LocalMemory local;
                active = cs2::ReadAssistSample({&local, cs2::LocalMemory::Read}, r.addresses, sample);
            }
            Backend backend{*this, r, sample};
            controller.Step(r.options, sample, keys, FrameSeconds(), active,
                            r.trackingEnabled && bridge_->BindingHeld(r.trackingKey), backend, controlsSafe);
            bridge_->SuppressAssistRepeats(controller.SuppressedRepeats());
            {
                std::scoped_lock lock(mutex_);
                diagnostics_ = controller.GetStatus();
            }
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(active ? 4 : 20), [&] { return stop_.load(); });
        }
        last.deadline = GetTickCount64() + 100;
        Backend backend{*this, last, sample};
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            controller.Stop(backend, bridge_->AssistKeys(), backend.Ready());
            if (!controller.PendingRelease())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        bridge_->SuppressAssistRepeats(0);
        if (precision)
            timeEndPeriod(1);
    }

  public:
    ~Runtime() { Stop(); }
    void Start(WindowBridge &bridge) {
        Stop();
        bridge_ = &bridge;
        stop_ = false;
        worker_ = std::thread([this] { Run(); });
    }
    void Stop() noexcept {
        stop_ = true;
        wake_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }
    void Configure(const Request &r) {
        std::scoped_lock lock(mutex_);
        request_ = r;
    }
    Diagnostics Status() {
        std::scoped_lock lock(mutex_);
        return diagnostics_;
    }
};
} // namespace awareness::assist
