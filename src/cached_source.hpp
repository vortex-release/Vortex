#pragma once
#include "settings.hpp"
#include "cs2_source.hpp"
#include "cs2_model_draw.hpp"
#include "snapshot_worker.hpp"
#include "spectator_reader.hpp"
#include "assist_reader.hpp"
#include "sky_tint.hpp"
namespace awareness::cs2 {
class CachedSource {
    struct Request {
        TargetBone bone{TargetBone::Head};
        bool badge{};
        bool preview{}, projectiles{}, world{}, ghosts{}, kills{}, models{}, spectators{}, dropped{}, velocities{},
            footsteps{}, skeletons{};
        float gameTime{}, ghostDuration{.18f};
        Configuration config;
        styling::Sky sky;
        ULONGLONG skyDeadline{};
        combat::InfernoEvents infernos;
    };
    struct Publication {
        FrameSnapshot frame;
        Status status;
        ProjectileFrame projectiles;
        combat::WorldSnapshot world;
        combat::ReplayFrame ghosts;
        PreviewPose preview;
        skeleton::Frame skeletons;
        SpectatorFrame spectators;
        tracking::Sample tracking;
        worldvisuals::Drops dropped;
        worldvisuals::Footsteps footsteps;
        std::uint64_t footstepCount{};
        KillSample kills;
        model::Targets targets;
        ULONGLONG sampledAt{};
        bool valid{}, killReady{};
        float readMs{};
        int ping{-1};
        SkyStatus sky;
        std::uintptr_t matrix{}, angles{}, entitySlot{}, localPawnSlot{}, localControllerSlot{};
    };
    Source source_;
    SnapshotWorker<Publication, Request> worker_;
    Publication current_;
    Request request_;
    ModelHighlight highlight_;
    SkyTint sky_;
    std::mutex skyMutex_;
    unsigned generation_{~0u};
    std::uint64_t publicationSerial_{};
    double nextSpectators_{};
    SpectatorFrame spectators_;
    worldvisuals::Drops dropped_;
    double nextDrops_{};
    double nextPing_{};
    int ping_{-1};

  public:
    CachedSource() {
        worker_.Start(
            [this](const Request &request, Publication &out, unsigned generation) {
                if (generation_ != generation) {
                    source_.Rescan();
                    generation_ = generation;
                    ResetInPlace(out);
                    nextPing_ = 0;
                    ping_ = -1;
                    nextSpectators_ = 0;
                    spectators_ = {};
                    dropped_ = {};
                    nextDrops_ = 0;
                }
                const auto start = FrameSeconds();
                out.valid = source_.Update(out.frame, request.bone);
                out.status = source_.GetStatus();
                out.sampledAt = source_.SampledAt();
                out.matrix = source_.MatrixAddress();
                out.angles = source_.AnglesAddress();
                out.entitySlot = source_.EntitySlot();
                out.localPawnSlot = source_.LocalPawnSlot();
                out.localControllerSlot = source_.LocalControllerSlot();
                {
                    std::scoped_lock lock(skyMutex_);
                    LocalMemory local;
                    GlowAccess access{{&local, LocalMemory::Read}};
                    std::uintptr_t list{};
                    const bool ready = request.config.enabled && out.valid && out.status.buildVerified &&
                                       GetTickCount64() <= request.skyDeadline &&
                                       access.memory.Read(out.entitySlot, list);
                    out.sky = sky_.Update(access, list, request.sky, start, ready);
                }
                out.ping = -1;
                out.preview = {};
                out.skeletons = {};
                out.tracking = {};
                out.dropped = {};
                out.footsteps.Clear();
                out.footstepCount = 0;
                out.projectiles = {};
                out.world.Clear();
                out.spectators = {};
                out.killReady = false;
                out.targets.count = 0;
                if (out.valid) {
                    if (request.badge) {
                        if (start >= nextPing_) {
                            ping_ = source_.ReadPing();
                            nextPing_ = start + .5;
                        }
                        out.ping = ping_;
                    } else {
                        ping_ = -1;
                        nextPing_ = 0;
                    }
                    source_.ReadTracking(out.frame, out.tracking, request.velocities);
                    if (request.footsteps)
                        source_.ReadFootsteps(out.frame, out.footsteps, out.footstepCount);
                    else
                        source_.ClearFootsteps();
                    if (request.dropped) {
                        if (start >= nextDrops_) {
                            source_.ReadDropped(dropped_);
                            nextDrops_ = start + .05;
                        }
                        out.dropped = dropped_;
                    } else {
                        dropped_ = {};
                        nextDrops_ = 0;
                    }
                    if (request.spectators) {
                        if (start >= nextSpectators_) {
                            source_.ReadSpectators(spectators_);
                            nextSpectators_ = start + .2;
                        }
                        out.spectators = spectators_;
                    } else {
                        spectators_ = {};
                        nextSpectators_ = 0;
                    }
                    if (request.skeletons)
                        source_.ReadSkeletons(out.frame, request.config, out.skeletons);
                    if (request.preview)
                        source_.ReadPreview(out.frame, out.preview);
                    if (request.projectiles)
                        source_.ReadFlight(out.projectiles);
                    if (request.world)
                        source_.ReadWorld(request.gameTime, out.world, request.infernos);
                    if (request.ghosts)
                        source_.ReadGhosts(out.frame, out.ghosts, start, request.ghostDuration);
                    else {
                        out.ghosts.Clear();
                        source_.ClearGhosts();
                    }
                    if (request.kills)
                        out.killReady = source_.ReadKills(out.kills);
                    if (request.models) {
                        LocalMemory local;
                        Memory m{&local, LocalMemory::Read};
                        std::uintptr_t list{};
                        if (m.Read(out.entitySlot, list))
                            out.targets = model::CollectTargets(m, list, out.frame, request.config);
                    }
                } else {
                    ping_ = -1;
                    nextPing_ = 0;
                    out.ghosts.Clear();
                    source_.ClearGhosts();
                    source_.ClearFootsteps();
                    dropped_ = {};
                    nextDrops_ = 0;
                    spectators_ = {};
                    nextSpectators_ = 0;
                }
                out.readMs = static_cast<float>((FrameSeconds() - start) * 1000);
            },
            [this] {
                ClearSky();
                source_.ClearGhosts();
                source_.ClearFootsteps();
            });
    }
    ~CachedSource() {
        worker_.Stop();
        ClearSky();
        ClearHighlight();
    }
    void Configure(const Configuration &c, const VisualOptions &v, const EffectsConfiguration &e, bool preview) {
        request_.sky = v.sky;
        request_.skyDeadline = GetTickCount64() + 125;
        request_.badge = c.enabled && v.sessionBadge;
        request_.config = c;
        request_.preview = preview;
        request_.skeletons = c.enabled && v.skeleton.enabled;
        request_.projectiles = c.enabled && v.grenadeTrails;
        request_.world = c.enabled && (v.combat.bombTimer || v.combat.areas || v.combat.utilityTimers);
        request_.ghosts = c.enabled && v.combat.ghosts;
        request_.ghostDuration = v.combat.ghostDuration;
        request_.kills = v.killSoundEnabled;
        request_.dropped = c.enabled && v.worldVisuals.dropped;
        request_.footsteps = c.enabled && v.worldVisuals.footsteps;
        request_.velocities = v.trackingProfiles.compensation;
        request_.spectators = c.enabled && v.spectators;
        request_.models = c.enabled && (e.materialEnabled || e.glowEnabled);
    }
    bool Update(FrameSnapshot &frame, TargetBone bone = TargetBone::Head) {
        request_.bone = bone;
        worker_.TryExchange(request_, current_, publicationSerial_);
        frame = current_.frame;
        // A current camera matrix is essential: reusing a 15ms-old projection
        // would make otherwise cached world positions swim during mouse movement.
        if (current_.valid && current_.status.buildVerified) {
            LocalMemory local;
            if (!LocalMemory::Read(&local, current_.matrix, &frame.viewProjection, sizeof(frame.viewProjection)) ||
                !CameraFromMatrix(frame.viewProjection, frame.cameraOrigin))
                return false;
            for (const auto &row : frame.viewProjection.m)
                for (float value : row)
                    if (!std::isfinite(value))
                        return false;
        }
        return current_.valid;
    }
    void Rescan() {
        ClearSky();
        ClearHighlight();
        worker_.Invalidate();
        ResetInPlace(current_);
    }
    assist::Addresses AssistAddresses() const {
        if (!current_.valid || !current_.status.buildVerified)
            return {};
        return {current_.entitySlot, current_.localPawnSlot, current_.localControllerSlot, current_.angles};
    }
    bool ClearSky() {
        std::scoped_lock lock(skyMutex_);
        LocalMemory local;
        return sky_.Clear({{&local, LocalMemory::Read}});
    }
    unsigned SkyCount() const { return current_.sky.applied; }
    unsigned SkyFailures() const { return current_.sky.failed; }
    int Ping() const { return current_.ping; }
    const tracking::Sample &Tracking() const { return current_.tracking; }
    const worldvisuals::Drops &Dropped() const { return current_.dropped; }
    const worldvisuals::Footsteps &Footsteps() const { return current_.footsteps; }
    auto FootstepCount() const { return current_.footstepCount; }
    bool TrackingWeaponCurrent() const {
        LocalMemory local;
        Memory m{&local, LocalMemory::Read};
        std::uintptr_t list{}, pawn{};
        tracking::Sample sample;
        return current_.valid && current_.status.buildVerified && m.Read(current_.entitySlot, list) &&
               m.Read(current_.localPawnSlot, pawn) && ReadTrackingWeapon(m, list, pawn, sample) &&
               sample.owner == current_.tracking.owner && sample.weaponHandle == current_.tracking.weaponHandle &&
               sample.weapon == current_.tracking.weapon;
    }
    const Status &GetStatus() const { return current_.status; }
    ULONGLONG SampledAt() const { return current_.sampledAt; }
    float ReadMilliseconds() const { return current_.readMs; }
    const SpectatorFrame &Spectators() const { return current_.spectators; }
    const model::Targets &ModelTargets() const { return current_.targets; }
    const skeleton::Frame &Skeletons() const { return current_.skeletons; }
    bool ReadPreview(const FrameSnapshot &, PreviewPose &out) const {
        out = current_.preview;
        return out.valid;
    }
    bool ReadFlight(ProjectileFrame &out) const {
        out = current_.projectiles;
        return current_.valid;
    }
    bool ReadWorld(float time, combat::WorldSnapshot &out, const combat::InfernoEvents &events = {}) {
        request_.infernos = events;
        request_.gameTime = time;
        out = current_.world;
        return current_.valid;
    }
    void ReadGhosts(const FrameSnapshot &, combat::ReplayFrame &out, double, float) const { out = current_.ghosts; }
    bool ReadKills(KillSample &out) const {
        out = current_.kills;
        return current_.killReady;
    }
    bool ReadCamera(NativeViewAngles &out) const {
        return current_.valid && current_.status.buildVerified && ReadViewAngles(current_.angles, out);
    }
    HRESULT ApplyCamera(NativeViewAngles expected, camera::Angles desired) const {
        return current_.valid && current_.status.buildVerified ? CommitViewAngles(current_.angles, expected, desired)
                                                               : HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    bool ClearHighlight() noexcept {
        LocalMemory local;
        return highlight_.Clear({{&local, LocalMemory::Read}});
    }
    void UpdateHighlight(const FrameSnapshot &frame, const Configuration &config, const EffectsConfiguration &effects,
                         bool fresh, EffectsState &output) {
        LocalMemory local;
        const GlowAccess access{{&local, LocalMemory::Read}};
        std::uintptr_t list{};
        const bool ready = fresh && current_.valid && current_.status.buildVerified &&
                           access.memory.Read(current_.entitySlot, list) && list;
        const auto result = highlight_.Update(access, list, frame, config, effects, ready);
        output = {};
        output.meshCount = result.applied;
        output.status = !config.enabled || !(effects.materialEnabled || effects.glowEnabled) ? EffectsStatus::Disabled
                        : result.applied ? EffectsStatus::NativeReady
                                         : EffectsStatus::NoGeometry;
        output.result = result.applied ? S_OK : S_FALSE;
        if (output.status == EffectsStatus::NativeReady && effects.visibility != EffectVisibility::AlwaysVisible) {
            output.status = EffectsStatus::NativeSingleColor;
            output.result = S_FALSE;
        }
        if (!result.restored || (result.failed && !result.applied)) {
            output.status = EffectsStatus::Failed;
            output.result = E_ACCESSDENIED;
        }
    }
};
} // namespace awareness::cs2
