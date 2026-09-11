#pragma once
#include "cs2_reader.hpp"
#include "local_memory.hpp"
#include <Windows.h>
#include <atomic>
#include <array>
#include <cstdint>

namespace awareness::session {
struct ReadyGate {
    std::uintptr_t object{};
    std::uint32_t stamp{};
    std::uint64_t firstSeen{};
    bool attempted{};
    void Reset() noexcept { *this = {}; }
    bool Poll(bool enabled, std::uintptr_t owner, std::uint32_t started, int remaining, int phase,
              std::uint64_t now) noexcept {
        if (!enabled || !owner || !started || remaining <= 0 || remaining > 60 || phase < 0 || phase >= 2) {
            Reset();
            return false;
        }
        if (object != owner || stamp != started || now < firstSeen) {
            object = owner;
            stamp = started;
            firstSeen = now;
            attempted = false;
        }
        if (attempted || now - firstSeen < 1000)
            return false;
        attempted = true;
        return true;
    }
};
enum class Status : unsigned { Off, Waiting, Ready, Accepted, Unavailable, Failed };
inline const char *StatusText(Status s) noexcept {
    switch (s) {
    case Status::Off:
        return "Off";
    case Status::Waiting:
        return "Waiting for a match";
    case Status::Ready:
        return "Match found";
    case Status::Accepted:
        return "Match accepted";
    case Status::Unavailable:
        return "Unavailable on this game build";
    case Status::Failed:
        return "Accept failed - use the game button";
    }
    return "Off";
}
class Tools {
    std::atomic<bool> awake_{}, accept_{};
    std::atomic<Status> status_{Status::Off};
    std::atomic<unsigned> accepted_{};
    std::atomic<bool> powerActive_{};
    HANDLE power_{};
    ReadyGate gate_;
    std::uintptr_t client_{};
    bool checked_{}, valid_{};
    static bool CallRemaining(std::uintptr_t function, std::uintptr_t object, int &value) noexcept {
        __try {
            value = reinterpret_cast<int (*)(void *)>(function)(reinterpret_cast<void *>(object));
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    static bool CallAccept(std::uintptr_t function) noexcept {
        __try {
            return reinterpret_cast<bool (*)(void *, const char *)>(function)(nullptr, "");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    bool Validate(const cs2::Memory &m) noexcept {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if (!m.Read(client_, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
            dos.e_lfanew > 0x100000 || !m.Field(client_, dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.FileHeader.TimeDateStamp != 1788980830 ||
            nt.OptionalHeader.SizeOfImage != 41803776)
            return false;
        // Build 14181: registered GameStateAPI methods, verified against the installed PE.
        constexpr std::array<unsigned char, 12> timeCode{0x40, 0x53, 0x48, 0x83, 0xec, 0x20,
                                                         0x48, 0x8b, 0xd9, 0x48, 0x83, 0xc1};
        constexpr std::array<unsigned char, 9> acceptCode{0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda};
        auto foundTime = timeCode;
        auto foundAccept = acceptCode;
        return m.Field(client_, 0xf3e810, foundTime) && foundTime == timeCode &&
               m.Field(client_, 0xf77bb0, foundAccept) && foundAccept == acceptCode;
    }
    void PowerTick() noexcept {
        if (!awake_) {
            if (power_) {
                PowerClearRequest(power_, PowerRequestSystemRequired);
                CloseHandle(power_);
                power_ = nullptr;
            }
            powerActive_ = false;
            return;
        }
        if (!power_) {
            wchar_t reason[] = L"Vortex keeps this CS2 session awake";
            REASON_CONTEXT context{};
            context.Version = POWER_REQUEST_CONTEXT_VERSION;
            context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
            context.Reason.SimpleReasonString = reason;
            power_ = PowerCreateRequest(&context);
            if (power_ == INVALID_HANDLE_VALUE)
                power_ = nullptr;
            powerActive_ = power_ && PowerSetRequest(power_, PowerRequestSystemRequired);
        }
    }

  public:
    Tools() = default;
    ~Tools() {
        awake_ = false;
        PowerTick();
    }
    void Configure(bool keepAwake, bool autoAccept) noexcept {
        awake_ = keepAwake;
        accept_ = autoAccept;
    }
    Status GetStatus() const noexcept { return status_; }
    unsigned Accepted() const noexcept { return accepted_; }
    bool PowerActive() const noexcept { return powerActive_; }
    static void TickCallback(void *context) noexcept { static_cast<Tools *>(context)->Tick(); }
    void Tick() noexcept {
        PowerTick();
        if (!accept_) {
            gate_.Reset();
            status_ = Status::Off;
            return;
        }
        cs2::LocalMemory local;
        const cs2::Memory m{&local, cs2::LocalMemory::Read};
        if (!checked_) {
            client_ = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
            if (!client_)
                return;
            checked_ = true;
            valid_ = Validate(m);
        }
        if (!valid_) {
            status_ = Status::Unavailable;
            return;
        }
        std::uintptr_t api{}, vtable{}, ready{};
        std::uint32_t stamp{};
        int remaining{}, phase{};
        if (!m.Field(client_, 0x23e5fd8, api) || !m.Read(api, vtable) || vtable != client_ + 0x1ba65a8 ||
            !m.Field(api, 0x24, stamp) || !m.Field(client_, 0x23e3b40, ready) || !m.Field(ready, 0xa8, phase)) {
            gate_.Reset();
            status_ = Status::Waiting;
            return;
        }
        if (!CallRemaining(client_ + 0xf3e810, api, remaining)) {
            valid_ = false;
            status_ = Status::Unavailable;
            return;
        }
        if (remaining <= 0 || remaining > 60) {
            gate_.Reset();
            status_ = Status::Waiting;
            return;
        }
        if (phase >= 2) {
            status_ = Status::Accepted;
            return;
        }
        if (!gate_.Poll(true, ready, stamp, remaining, phase, GetTickCount64())) {
            if (!gate_.attempted)
                status_ = Status::Ready;
            return;
        }
        // Recheck the singleton before the registered ready API changes its state.
        std::uintptr_t current{};
        if (!m.Field(client_, 0x23e3b40, current) || current != ready)
            return;
        if (CallAccept(client_ + 0xf77bb0)) {
            ++accepted_;
            status_ = Status::Accepted;
        } else
            status_ = Status::Failed;
    }
};
} // namespace awareness::session
