#pragma once
#include "assist_reader.hpp"
#include "assist_input_state.hpp"
#include "assist_timing.hpp"
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
    bool trackingEnabled{}, recoilEnabled{}, enabled{};
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
        const Keys *plannedKeys{};
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
                if (!cs2::ReadAssistSample({&local, cs2::LocalMemory::Read}, request.addresses, current, false) ||
                    current.owner != sample.owner || !current.grounded || !Movable(current) ||
                    !owner.bridge_->AssistKeys().Held(Space))
                    return false;
            }
            if (down && (key == A || key == D)) {
                const auto physical = owner.bridge_->AssistKeys();
                cs2::LocalMemory local;
                Sample current;
                if (!request.enabled || request.options.strafeMode != 1 || !physical.Held(Space) || physical.Held(A) ||
                    physical.Held(D) || physical.Held(LeftMouse) ||
                    (request.options.strafeWalkPause && physical.WalkingHeld()) ||
                    !cs2::ReadAssistSample({&local, cs2::LocalMemory::Read}, request.addresses, current, false) ||
                    current.owner != sample.owner || !IsAirborne(current))
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
                    current.target != sample.target || !CrosshairEnemy(current) || !current.weaponReady ||
                    current.frozen || (request.options.scopeOnly && !current.scoped) ||
                    owner.bridge_->AssistKeys().Held(LeftMouse))
                    return false;
            }
            INPUT i{};
            i.type = INPUT_MOUSE;
            i.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            i.mi.dwExtraInfo = InputTag;
            return SendInput(1, &i, sizeof(i)) == 1;
        }
        bool Pistol(bool down) {
            if (down) {
                if (!Ready() || !request.enabled || !request.options.autoPistol ||
                    !owner.bridge_->AssistKeys().Held(LeftMouse))
                    return false;
                cs2::LocalMemory local;
                Sample current;
                if (!cs2::ReadAssistSample({&local, cs2::LocalMemory::Read}, request.addresses, current) ||
                    current.owner != sample.owner || current.weaponHandle != sample.weaponHandle ||
                    !SemiAutomaticPistol(current.weapon) || !current.weaponReady || current.frozen)
                    return false;
            }
            INPUT i{};
            i.type = INPUT_MOUSE;
            i.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            i.mi.dwExtraInfo = InputTag;
            return SendInput(1, &i, sizeof(i)) == 1;
        }
        bool RestorePrimary() {
            if (!Ready() || !owner.bridge_->AssistKeys().Held(LeftMouse))
                return false;
            INPUT i{};
            i.type = INPUT_MOUSE;
            i.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            i.mi.dwExtraInfo = InputTag;
            return SendInput(1, &i, sizeof(i)) == 1;
        }
        bool RestoreJump() {
            if (!Ready() || !owner.bridge_->AssistKeys().Held(Space))
                return false;
            INPUT i{};
            i.type = INPUT_KEYBOARD;
            i.ki.wScan = static_cast<WORD>(MapVirtualKeyW(Space, MAPVK_VK_TO_VSC));
            i.ki.dwFlags = KEYEVENTF_SCANCODE;
            i.ki.dwExtraInfo = InputTag;
            return i.ki.wScan && SendInput(1, &i, sizeof(i)) == 1;
        }
        YawResult Yaw(const Sample &expected, float yaw) {
            if (!Ready())
                return YawResult::Yielded;
            const auto physical = owner.bridge_->AssistKeys();
            if (physical.Held(A) == physical.Held(D) || physical.Held(LeftMouse) ||
                (request.options.strafeWalkPause && physical.WalkingHeld()) ||
                (request.trackingEnabled && owner.bridge_->BindingHeld(request.trackingKey)) ||
                (plannedKeys && (physical.Held(A) != plannedKeys->Held(A) || physical.Held(D) != plannedKeys->Held(D) ||
                                 physical.Held(W) != plannedKeys->Held(W) || physical.Held(S) != plannedKeys->Held(S))))
                return YawResult::Yielded;
            cs2::LocalMemory local;
            cs2::Memory m{&local, cs2::LocalMemory::Read};
            Sample current;
            if (!cs2::ReadAssistSample(m, request.addresses, current, false) || current.owner != expected.owner ||
                !IsAirborne(current) || !current.anglesKnown || !current.velocityKnown)
                return YawResult::Yielded;
            const float turn = NormalizeYaw(yaw - expected.yaw);
            if (std::abs(turn) < .00001f)
                return YawResult::Unchanged;
            // Keep the actual raw yaw as CAS expected bits, including unwrapped
            // engine angles. Host input winning this race is a yield, not a turn.
            if (std::abs(NormalizeYaw(current.yaw - expected.yaw)) > .35f ||
                std::abs(current.pitch - expected.pitch) > .35f)
                return YawResult::Yielded;
            const auto result = cs2::CommitViewAngles(request.addresses.angles, {current.pitch, current.yaw, 0},
                                                      {-current.pitch, NormalizeYaw(current.yaw + turn)});
            return result == S_OK                              ? YawResult::Applied
                   : result == HRESULT_FROM_WIN32(ERROR_RETRY) ? YawResult::Yielded
                                                               : YawResult::Failed;
        }
    };
    void Run() noexcept {
        Controller controller;
        RecoilActivity recoilActivity;
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
            const bool configured = r.options.shoot || r.options.jumper || r.options.strafer || r.options.autoPistol;
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
                const bool readCombat =
                    r.options.shoot || r.options.autoPistol || (r.options.strafer && r.recoilEnabled);
                cs2::ReadAssistSample({&local, cs2::LocalMemory::Read}, r.addresses, sample, readCombat);
            }
            Backend backend{*this, r, sample, &keys};
            const auto now = FrameSeconds();
            const bool recoilBusy = recoilActivity.Update(sample, now, active && r.recoilEnabled);
            controller.Step(r.options, sample, keys, now, active,
                            (r.trackingEnabled && bridge_->BindingHeld(r.trackingKey)) || recoilBusy, backend,
                            controlsSafe);
            bridge_->SuppressAssistRepeats(controller.SuppressedRepeats());
            {
                std::scoped_lock lock(mutex_);
                diagnostics_ = controller.GetStatus();
            }
            std::unique_lock lock(mutex_);
            const auto pollMs = PollIntervalMs(active, sample.valid, r.options, keys);
            wake_.wait_for(lock, std::chrono::milliseconds(pollMs), [&] { return stop_.load(); });
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
