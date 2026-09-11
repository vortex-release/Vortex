#pragma once
#include "cs2_reader.hpp"
#include "frame_clock.hpp"
#include "build_verification.hpp"
#include "cs2_camera.hpp"
#include "cs2_glow.hpp"
#include "kill_events.hpp"
#include "combat_reader.hpp"
#include "spectator_reader.hpp"
#include "latency_reader.hpp"
#include "world_visuals_reader.hpp"
#include <awareness/AddressDiscovery.hpp>
#include <Windows.h>
#include <memory>

namespace awareness::cs2 {
struct Status {
    char message[160]{"Waiting for CS2 data"};
    std::uint32_t gameBuild{}, expectedBuild{offsets::ExpectedBuild};
    bool usingPatterns{}, ready{};
    bool buildVerified{};
    std::uint32_t rawBuildValue{};
    std::uintptr_t buildRva{};
    ReadReport reads{};
};
class Source {
    HMODULE client_{}, engine_{};
    Globals globals_{};
    std::uintptr_t viewAngles_{};
    ULONGLONG nextAttempt_{};
    bool discovered_{}, buildChecked_{};
    Status status_{};
    ULONGLONG sampledAt_{};
    ModelHighlight highlight_;
    mutable LocalMemory sampleMemory_;
    bool replayActive_{};
    ProjectileTracker projectiles_;
    WorldReader world_;
    DroppedReader dropped_;
    std::unique_ptr<combat::GhostHistory> replayHistory_{std::make_unique<combat::GhostHistory>()};

  public:
    ~Source();
    bool ClearHighlight() noexcept;
    void UpdateHighlight(const FrameSnapshot &, const Configuration &, const EffectsConfiguration &, bool,
                         EffectsState &) noexcept;
    void Rescan() noexcept {
        ClearHighlight();
        projectiles_.Reset();
        world_.Reset();
        dropped_.Reset();
        ClearGhosts();
        sampleMemory_.Reset();
        discovered_ = false;
        buildChecked_ = false;
        nextAttempt_ = 0;
        sampledAt_ = 0;
        globals_ = {};
        viewAngles_ = 0;
        status_ = {};
    }
    bool Update(FrameSnapshot &, TargetBone selected = TargetBone::Head) noexcept;
    bool ReadPreview(const FrameSnapshot &, PreviewPose &) const noexcept;
    bool ReadFlight(ProjectileFrame &out) noexcept {
        out = {};
        if (!status_.ready || !status_.buildVerified || status_.gameBuild != offsets::ExpectedBuild)
            return false;
        Memory memory{&sampleMemory_, LocalMemory::Read};
        std::uintptr_t list{};
        return memory.Read(globals_.entitySlot, list) && projectiles_.Update(memory, list, FrameSeconds(), out);
    }
    bool ReadWorld(float gameTime, combat::WorldSnapshot &out, const combat::InfernoEvents &events = {}) noexcept {
        out = {};
        if (!status_.ready || !status_.buildVerified || status_.gameBuild != offsets::ExpectedBuild)
            return false;
        Memory m{&sampleMemory_, LocalMemory::Read};
        std::uintptr_t list{}, pawn{};
        m.Read(globals_.localPawnSlot, pawn);
        return m.Read(globals_.entitySlot, list) && world_.Update(m, list, pawn, gameTime, FrameSeconds(), out, events);
    }
    void ReadTracking(const FrameSnapshot &frame, tracking::Sample &out, bool velocities) const noexcept {
        out = {};
        if (!status_.ready || !status_.buildVerified)
            return;
        Memory m{&sampleMemory_, LocalMemory::Read};
        std::uintptr_t list{}, pawn{};
        if (!m.Read(globals_.entitySlot, list) || !m.Read(globals_.localPawnSlot, pawn) ||
            !ReadTrackingWeapon(m, list, pawn, out)) {
            out = {};
            return;
        }
        out.time = FrameSeconds();
        if (!velocities)
            return;
        for (unsigned i = 0; i < frame.entityCount && i < MaxEntities; ++i) {
            const auto &e = frame.entities[i];
            auto &motion = out.motion[i];
            const auto target = EntityAt(m, list, e.id);
            std::uint32_t after{};
            motion.id = e.id;
            motion.valid = e.valid && !e.dormant && e.health > 0 && FullHandle(m, target, motion.handle) &&
                           (motion.handle & offsets::EntryMask) == e.id &&
                           m.Field(target, offsets::AbsVelocity, motion.velocity) && Finite(motion.velocity) &&
                           FullHandle(m, target, after) && after == motion.handle;
        }
    }
    bool ReadDropped(worldvisuals::Drops &out) noexcept {
        out = {};
        if (!status_.ready || !status_.buildVerified)
            return false;
        Memory m{&sampleMemory_, LocalMemory::Read};
        std::uintptr_t list{};
        return m.Read(globals_.entitySlot, list) && dropped_.Update(m, list, FrameSeconds(), out);
    }
    void ClearGhosts() noexcept {
        if (replayActive_)
            replayHistory_->Clear();
        replayActive_ = false;
    }
    void ReadGhosts(const FrameSnapshot &frame, combat::ReplayFrame &out, double now, float duration) noexcept {
        auto &history = *replayHistory_;
        out.Clear();
        if (!status_.ready || !status_.buildVerified || status_.gameBuild != offsets::ExpectedBuild) {
            ClearGhosts();
            return;
        }
        Memory m{&sampleMemory_, LocalMemory::Read};
        std::uintptr_t list{};
        if (!m.Read(globals_.entitySlot, list)) {
            ClearGhosts();
            return;
        }
        replayActive_ = true;
        history.Begin();
        for (std::uint32_t i = 0; i < frame.entityCount && i < MaxEntities; ++i) {
            const auto &e = frame.entities[i];
            if (e.id == frame.localEntityId || !e.valid || e.dormant || e.health <= 0)
                continue;
            const auto pawn = EntityAt(m, list, e.id);
            std::uintptr_t scene{};
            std::uint32_t handle{}, after{};
            PreviewPose pose;
            if (FullHandle(m, pawn, handle) && (handle & offsets::EntryMask) == e.id &&
                m.Field(pawn, offsets::SceneNode, scene) && ReadPreviewPose(m, scene, e, pose) &&
                FullHandle(m, pawn, after) && after == handle)
                history.Add(handle, pose, now, duration);
        }
        history.End();
        combat::BuildReplay(history, now, duration, out);
    }
    int ReadPing() const noexcept {
        if (!status_.ready || !status_.buildVerified)
            return -1;
        Memory m{&sampleMemory_, LocalMemory::Read};
        std::uintptr_t controller{}, after{};
        if (!m.Read(globals_.localControllerSlot, controller))
            return -1;
        const int ping = ReadControllerPing(m, controller);
        return m.Read(globals_.localControllerSlot, after) && after == controller ? ping : -1;
    }
    bool ReadSpectators(SpectatorFrame &out) const noexcept {
        out = {};
        if (!status_.ready || !status_.buildVerified)
            return false;
        Memory m{&sampleMemory_, LocalMemory::Read};
        std::uintptr_t list{}, controller{}, pawn{};
        return m.Read(globals_.entitySlot, list) && m.Read(globals_.localControllerSlot, controller) &&
               m.Read(globals_.localPawnSlot, pawn) && cs2::ReadSpectators(m, list, controller, pawn, out);
    }
    bool ReadKills(KillSample &sample) const noexcept {
        sample = {};
        if (!status_.ready || !status_.buildVerified || status_.gameBuild != offsets::ExpectedBuild)
            return false;
        return ReadKillSample({&sampleMemory_, LocalMemory::Read}, globals_, sample);
    }
    bool ReadCamera(NativeViewAngles &value) const noexcept {
        return status_.ready && status_.buildVerified && status_.gameBuild == offsets::ExpectedBuild &&
               ReadViewAngles(viewAngles_, value);
    }
    HRESULT ApplyCamera(NativeViewAngles expected, camera::Angles desired) const noexcept {
        if (!status_.ready || !status_.buildVerified || status_.gameBuild != offsets::ExpectedBuild)
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        return CommitViewAngles(viewAngles_, expected, desired);
    }
    std::uintptr_t MatrixAddress() const noexcept { return globals_.matrix; }
    std::uintptr_t AnglesAddress() const noexcept { return viewAngles_; }
    std::uintptr_t LocalControllerSlot() const noexcept { return globals_.localControllerSlot; }
    std::uintptr_t LocalPawnSlot() const noexcept { return globals_.localPawnSlot; }
    std::uintptr_t EntitySlot() const noexcept { return globals_.entitySlot; }
    const Status &GetStatus() const noexcept { return status_; }
    ULONGLONG SampledAt() const noexcept { return sampledAt_; }
};
} // namespace awareness::cs2
