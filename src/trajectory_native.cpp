#include "trajectory_native.hpp"
#include "world_visuals_reader.hpp"
#include "combat_reader.hpp"
#include "recoil_input.hpp"
#include "cs2_camera.hpp"
#include "local_memory.hpp"
#include "build_verification.hpp"
#include "runtime_support.hpp"
#include <MinHook.h>
#include <Psapi.h>
#include <atomic>
#include <mutex>

namespace awareness::cs2 {
namespace {
namespace abi = trajectory_offsets;
double Seconds() noexcept {
    return FrameSeconds();
}
struct Key {
    std::uint32_t hash{};
    std::int32_t symbol{-1};
    const char *text{};
    explicit Key(const char *name) noexcept : text(name) {
        // CUtlStringToken's case-insensitive Murmur2, independently implemented.
        auto p = reinterpret_cast<const unsigned char *>(name);
        std::size_t left = std::strlen(name);
        std::uint32_t h = 0x31415926u ^ static_cast<std::uint32_t>(left);
        auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32u : static_cast<unsigned>(c); };
        while (left >= 4) {
            std::uint32_t k = lower(p[0]) | (lower(p[1]) << 8) | (lower(p[2]) << 16) | (lower(p[3]) << 24);
            k *= 0x5bd1e995u;
            k ^= k >> 24;
            k *= 0x5bd1e995u;
            h = (h * 0x5bd1e995u) ^ k;
            p += 4;
            left -= 4;
        }
        if (left == 3)
            h ^= lower(p[2]) << 16;
        if (left >= 2)
            h ^= lower(p[1]) << 8;
        if (left) {
            h ^= lower(p[0]);
            h *= 0x5bd1e995u;
        }
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        hash = h ^ (h >> 15);
    }
};
using Setup = void (*)(void *);
using Dispatch = bool (*)(void *, void *);
using Tracer = void (*)(void *);
using BulletPath = void (*)(void *, void *, int, int, Vector3 *, Vector3 *);
struct Pending {
    std::uint32_t handle{};
    Vector3 start{};
    int team{};
    double time{};
};
struct Native {
    std::uintptr_t client{};
    Setup setup{};
    Dispatch dispatch{};
    Tracer tracer{}, particleTracer{};
    BulletPath bulletPath{};
    bool particleLayout{}, bulletLayout{}, particleHook{}, bulletHook{};
    combat::Recoil recoil;
    combat::MouseRecoil mouse;
    std::uint32_t mouseOwner{}, mouseWeapon{}, mouseMode{}, mouseShots{};
    bool viewLayout{}, eventLayout{}, tracerLayout{}, muzzleLayout{}, punchLayout{}, timeLayout{}, sweepLayout{};
    bool viewHook{}, eventHook{}, tracerHook{};
    double nextLog{};
    std::uint32_t previousShots{}, previousOwner{};
    std::atomic<bool> recoilActive{};
    bool havePrediction{};
    flight::Throw previousThrow;
    double previousRecoil{};
    bool installed{};
    std::atomic<bool> available{};
    std::atomic<unsigned> inFlight{};
    std::mutex mutex, setupMutex;
    VisualOptions settings;
    FlightSnapshot output;
    std::array<Pending, 64> pending{};
    flight::Fov fov;
    std::atomic<ULONGLONG> deadline{};
    camera_visuals::PredictionCadence predictionCadence;
} state;
struct Guard {
    Guard() { ++state.inFlight; }
    ~Guard() { --state.inFlight; }
};
template <std::size_t N>
bool Bytes(const Memory &memory, std::uintptr_t at, const unsigned char (&expected)[N]) noexcept {
    std::array<unsigned char, N> got{};
    return memory.Read(at, got) && std::memcmp(got.data(), expected, N) == 0;
}
bool Validate() noexcept {
    LocalMemory local;
    Memory m{&local, LocalMemory::Read};
    const auto engine = GetModuleHandleW(L"engine2.dll");
    MODULEINFO info{};
    if (!engine || !GetModuleInformation(GetCurrentProcess(), engine, &info, sizeof(info)))
        return false;
    const auto build = VerifyEngineBuild(m, reinterpret_cast<std::uintptr_t>(engine), info.SizeOfImage);
    if (build.check != BuildCheck::Verified || build.build != offsets::ExpectedBuild)
        return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!m.Read(state.client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 4096 ||
        !m.Read(state.client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.TimeDateStamp != abi::ClientTimestamp || nt.OptionalHeader.SizeOfImage != abi::ClientImageSize)
        return false;
    state.viewLayout = Bytes(m, state.client + abi::ViewSetup, abi::ViewSetupBytes);
    state.sweepLayout = Bytes(m, state.client + abi::TraceShape, abi::TraceShapeBytes) &&
                        Bytes(m, state.client + abi::TraceFilterInit, abi::TraceFilterInitBytes) &&
                        Bytes(m, state.client + abi::RayHullInit, abi::RayHullInitBytes);
    state.eventLayout = Bytes(m, state.client + abi::EventDispatch, abi::EventDispatchBytes) &&
                        Bytes(m, state.client + abi::EventGetName, abi::EventGetNameBytes) &&
                        Bytes(m, state.client + abi::EventGetInt, abi::EventGetIntBytes) &&
                        Bytes(m, state.client + abi::EventGetFloat, abi::EventGetFloatBytes);
    state.punchLayout = Bytes(m, state.client + abi::PunchAngles, abi::PunchAnglesBytes);
    state.timeLayout = Bytes(m, state.client + abi::EntityTime, abi::EntityTimeBytes);
    state.tracerLayout = Bytes(m, state.client + abi::TracerEffect, abi::TracerEffectBytes);
    state.muzzleLayout = Bytes(m, state.client + abi::TracerStart, abi::TracerStartBytes);
    state.particleLayout = Bytes(m, state.client + abi::ParticleTracer, abi::ParticleTracerBytes);
    state.bulletLayout = Bytes(m, state.client + abi::BulletPath, abi::BulletPathBytes);
    return true;
}
struct TraceContext {
    alignas(16) std::array<std::byte, 80> filter{};
    std::uintptr_t manager{}, padding{};
    float radius{2.02f};
    bool surfacePhysics{true};
};
// Keep SEH in leaf functions, with no C++ objects requiring unwinding.
bool InitializeFilter(TraceContext &ctx, std::uintptr_t pawn) noexcept {
    __try {
        using Init = void *(*)(void *, void *, std::uint64_t, int, std::uint8_t);
        reinterpret_cast<Init>(state.client + abi::TraceFilterInit)(ctx.filter.data(), reinterpret_cast<void *>(pawn),
                                                                    abi::GrenadeMask, abi::GrenadeLayer, 15);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
float SurfaceElasticity(std::uintptr_t entity) noexcept {
    if (!entity)
        return 1;
    LocalMemory local;
    Memory memory{&local, LocalMemory::Read};
    char name[64]{};
    float elasticity{};
    if (EntityName(memory, entity, name) &&
        (!std::strcmp(name, "cs_player_pawn") || !std::strcmp(name, "c_cs_player_for_precache") ||
         !std::strcmp(name, "player")))
        return .3f;
    return memory.Field(entity, offsets::Elasticity, elasticity) && std::isfinite(elasticity) && elasticity > 0 &&
                   elasticity <= 1
               ? elasticity
               : 1;
}
bool Sweep(void *context, Vector3 start, Vector3 end, flight::Collision &output) noexcept {
    auto &ctx = *static_cast<TraceContext *>(context);
    alignas(16) std::byte ray[48]{};
    alignas(16) std::byte result[512]{};
    const Vector3 bounds[2]{{-ctx.radius, -ctx.radius, -ctx.radius}, {ctx.radius, ctx.radius, ctx.radius}};
    __try {
        using Hull = void (*)(void *, const Vector3 *);
        using Trace = bool (*)(void *, void *, const Vector3 &, const Vector3 &, void *, void *);
        reinterpret_cast<Hull>(state.client + abi::RayHullInit)(ray, bounds);
        // false means no hit; it does not mean an unsuccessful sweep.
        reinterpret_cast<Trace>(state.client + abi::TraceShape)(reinterpret_cast<void *>(ctx.manager), ray, start, end,
                                                                ctx.filter.data(), result);
        std::memcpy(&output.end, result + abi::TraceEnd, sizeof(Vector3));
        std::memcpy(&output.normal, result + abi::TraceNormal, sizeof(Vector3));
        std::memcpy(&output.fraction, result + abi::TraceFraction, sizeof(float));
        output.solid = result[abi::TraceStartSolid] != std::byte{};
        std::uintptr_t hitEntity{};
        std::memcpy(&hitEntity, result + 8, sizeof(hitEntity));
        output.elasticity = ctx.surfacePhysics ? SurfaceElasticity(hitEntity) : 1;
        return Finite(output.end) && std::isfinite(output.fraction) && output.fraction >= 0 && output.fraction <= 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool SetFov(std::uintptr_t view, float expected, float desired) noexcept {
    __try {
        auto *p = reinterpret_cast<float *>(view + abi::ViewFov);
        if (*p != expected)
            return false;
        *p = desired;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool SetViewVector(std::uintptr_t address, Vector3 expected, Vector3 desired) noexcept {
    __try {
        if (std::memcmp(reinterpret_cast<void *>(address), &expected, sizeof(expected)))
            return false;
        std::memcpy(reinterpret_cast<void *>(address), &desired, sizeof(desired));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool EnginePunch(std::uintptr_t pawn, combat::RecoilSample &sample) noexcept {
    __try {
        using Punch = Vector3 *(*)(void *, Vector3 *, bool);
        reinterpret_cast<Punch>(state.client + abi::PunchAngles)(reinterpret_cast<void *>(pawn), &sample.punch, true);
        return Finite(sample.punch);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool EngineTime(std::uintptr_t pawn, float &now) noexcept {
    __try {
        using Time = float *(*)(void *, float *);
        reinterpret_cast<Time>(state.client + abi::EntityTime)(reinterpret_cast<void *>(pawn), &now);
        return std::isfinite(now) && now >= 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void SampleInputs(const VisualOptions &settings, bool applyRecoil) {
    const auto now = Seconds();
    LocalMemory local;
    Memory m{&local, LocalMemory::Read};
    std::uintptr_t pawn{}, list{};
    combat::RecoilSample recoil;
    float gameTime{};
    std::uintptr_t global{};
    const bool havePawn = m.Field(state.client, offsets::LocalPawn, pawn) && pawn;
    const bool timeReady =
        (havePawn && state.timeLayout && EngineTime(pawn, gameTime)) ||
        (m.Field(state.client, offsets::GlobalVars, global) && m.Field(global, abi::GlobalCurrentTime, gameTime) &&
         std::isfinite(gameTime) && gameTime >= 0);
    if (timeReady && settings.combat.recoil && havePawn && m.Field(state.client, offsets::EntityList, list) &&
        ReadRecoilMetadata(m, list, pawn, recoil)) {
        recoil.gameTime = gameTime;
        recoil.valid = state.punchLayout && EnginePunch(pawn, recoil);
    }
    {
        std::scoped_lock lock(state.mutex);
        state.output.recoil = recoil;
        state.output.gameTime = gameTime;
        state.output.sampledAt = timeReady ? now : 0;
    }

    {
        std::scoped_lock lock(state.mutex);
        std::uint32_t shots{}, owner{};
        if (havePawn && m.Field(pawn, offsets::ShotsFired, shots) && shots <= 300 && FullHandle(m, pawn, owner)) {
            if (owner == state.previousOwner && shots > state.previousShots)
                state.output.fireSamples += shots - state.previousShots;
            state.previousOwner = owner;
            state.previousShots = shots;
        }
        if (settings.combat.recoil && !recoil.valid)
            ++state.output.recoilReadFailures;
    }
    if (applyRecoil) {
        const float dt = state.previousRecoil > 0 ? static_cast<float>(now - state.previousRecoil) : 0;
        state.previousRecoil = now;
        DWORD foregroundProcess{};
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        const bool active = state.recoilActive && foregroundProcess == GetCurrentProcessId() &&
                            settings.combat.EnabledFor(recoil.weapon);
        auto correction = state.recoil.Update(recoil, settings.combat.Profile(recoil.weapon), dt, active);
        HRESULT result = S_FALSE;
        if (!active || !recoil.valid || !settings.combat.EnabledFor(recoil.weapon) ||
            recoil.owner != state.mouseOwner || recoil.weaponHandle != state.mouseWeapon ||
            settings.combat.recoilInput != state.mouseMode || recoil.shots < state.mouseShots)
            state.mouse.Reset();
        state.mouseOwner = recoil.owner;
        state.mouseWeapon = recoil.weaponHandle;
        state.mouseMode = settings.combat.recoilInput;
        state.mouseShots = recoil.shots;
        if (correction.x || correction.y) {
            if (settings.combat.recoilInput == 0) {
                float sensitivity{};
                if (!ReadMouseSensitivity(m, state.client, pawn, sensitivity))
                    result = E_INVALIDARG;
                else
                    result =
                        state.mouse.Apply(correction, sensitivity, settings.combat.mouseYaw, settings.combat.mousePitch,
                                          [](INPUT &input) { return SendInput(1, &input, sizeof(INPUT)); });
            } else
                for (int attempt = 0; attempt < 2; ++attempt) {
                    NativeViewAngles before;
                    if (!ReadViewAngles(state.client + offsets::ViewAngles, before)) {
                        result = E_ACCESSDENIED;
                        break;
                    }
                    auto desired = TrackingAngles(before);
                    desired.pitch += correction.x;
                    desired.yaw -= correction.y;
                    result = CommitViewAngles(state.client + offsets::ViewAngles, before, camera::Normalize(desired));
                    if (result == S_OK)
                        break;
                }
            if (FAILED(result))
                state.recoil.Restore(correction);
        }
        std::scoped_lock lock(state.mutex);
        state.output.recoilResult = result;
        if (result == S_OK)
            ++state.output.recoilWrites;
    }
}
void AfterSetup(std::uintptr_t view) {
    const auto now = Seconds();
    LocalMemory local;
    Memory m{&local, LocalMemory::Read};
    std::uintptr_t current{}, pawn{}, list{};
    if (!m.Field(state.client, abi::ViewRenderSlot, current) || view != current)
        return;
    const bool fresh = state.available && GetTickCount64() <= state.deadline;
    VisualOptions settings;
    {
        std::scoped_lock lock(state.mutex);
        settings = state.settings;
    }
    if (!fresh) {
        state.fov.Reset();
        state.havePrediction = false;
        state.predictionCadence.Reset();
        return;
    }
    std::uint8_t life{}, scoped{};
    int health{};
    const bool alive = m.Field(state.client, offsets::LocalPawn, pawn) && pawn &&
                       m.Field(pawn, offsets::LifeState, life) && !life && m.Field(pawn, offsets::Health, health) &&
                       health > 0;
    const bool zoomed = alive && m.Field(pawn, offsets::IsScoped, scoped) && scoped;
    float engine{};
    if (m.Field(view, abi::ViewFov, engine)) {
        const float desired = camera_visuals::FrameFov(state.fov, engine, settings.cameraFovEnabled != 0,
                                                       settings.cameraFov, zoomed, settings.cameraVisuals, now);
        if (desired != engine)
            SetFov(view, engine, desired);
    }
    Vector3 origin{}, angles{}, raw{};
    const bool haveView = m.Field(view, abi::ViewOrigin, origin) && PlausiblePosition(origin) &&
                          m.Field(view, abi::ViewAnglesField, angles) && Finite(angles);
    NativeViewAngles rawView;
    const bool haveRaw = ReadViewAngles(state.client + offsets::ViewAngles, rawView);
    if (haveRaw)
        raw = {rawView.pitch, rawView.yaw, rawView.roll};
    LineupSample lineup;
    if (alive && settings.lineups.enabled && haveRaw && m.Field(state.client, offsets::EntityList, list)) {
        std::uintptr_t node{};
        tracking::Sample weapon;
        if (m.Field(pawn, offsets::SceneNode, node) && m.Field(node, offsets::Origin, lineup.feet) &&
            PlausiblePosition(lineup.feet) && PlayerEye(m, pawn, lineup.eye) &&
            ReadTrackingWeapon(m, list, pawn, weapon)) {
            lineup.angles = raw;
            lineup.weapon = weapon.weapon;
            lineup.time = now;
            lineup.valid = true;
        }
    }
    {
        std::scoped_lock lock(state.mutex);
        state.output.lineup = lineup;
    }
    if (alive && haveView && settings.cameraVisuals.removeRecoil && haveRaw) {
        raw.z = 0;
        if (SetViewVector(view + abi::ViewAnglesField, angles, raw))
            angles = raw;
    }
    if (alive && haveView && settings.cameraVisuals.thirdPerson && (!zoomed || settings.cameraVisuals.whileScoped) &&
        state.sweepLayout) {
        TraceContext cameraTrace;
        cameraTrace.radius = 6;
        cameraTrace.surfacePhysics = false;
        std::uintptr_t physics{};
        if (m.Field(state.client, abi::TraceManagerSlot, cameraTrace.manager) && m.Read(cameraTrace.manager, physics) &&
            physics && InitializeFilter(cameraTrace, pawn)) {
            const auto offset = camera_visuals::Offset(angles, settings.cameraVisuals);
            flight::Collision collision;
            if (Sweep(&cameraTrace, origin, origin + offset, collision))
                SetViewVector(view + abi::ViewOrigin, origin,
                              camera_visuals::Clipped(origin, offset, collision.fraction, collision.solid));
        }
    }
    // Keep the input sample taken before setup, without evaluating it a second time.

    flight::Prediction prediction;
    bool collisionReady = false;
    if (state.sweepLayout && settings.grenadePrediction && m.Field(state.client, offsets::EntityList, list) &&
        m.Field(state.client, offsets::LocalPawn, pawn)) {
        Vector3 throwAngles{};
        flight::Throw input;
        TraceContext ctx;
        std::uintptr_t physics{};
        if (m.Field(state.client, abi::TraceManagerSlot, ctx.manager) && m.Read(ctx.manager, physics) && physics &&
            m.Field(state.client, offsets::ViewAngles, throwAngles) && ReadThrow(m, list, pawn, throwAngles, input) &&
            InitializeFilter(ctx, pawn)) {
            const auto &old = state.previousThrow;
            const bool unchanged =
                state.havePrediction && input.type == old.type && Distance(input.eye, old.eye) < .01f &&
                Distance(input.velocity, old.velocity) < .05f && std::abs(input.pitch - old.pitch) < .005f &&
                std::abs(input.yaw - old.yaw) < .005f && std::abs(input.strength - old.strength) < .001f;
            if (state.havePrediction && !state.predictionCadence.Due(now, input.type, input.strength, unchanged))
                return;
            if (!state.havePrediction) {
                state.predictionCadence.Reset();
                state.predictionCadence.Due(now, input.type, input.strength, false);
            }
            state.previousThrow = input;
            state.havePrediction = true;

            prediction = flight::Predict(input, {&ctx, Sweep});
            collisionReady = prediction.valid;
        }
    }
    std::scoped_lock lock(state.mutex);
    state.output.prediction = prediction;
    state.output.predictedAt = now;
    state.output.collisionReady = collisionReady;
    if (!prediction.valid) {
        state.havePrediction = false;
        state.predictionCadence.Reset();
    }
    ++state.output.setupSamples;
}
void SetupHook(void *self) {
    Guard guard;
    std::unique_lock lock(state.setupMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        state.setup(self);
        return;
    }
    try {
        if (state.available && GetTickCount64() <= state.deadline) {
            VisualOptions settings;
            {
                std::scoped_lock data(state.mutex);
                settings = state.settings;
            }
            SampleInputs(settings, true);
        } else {
            state.recoil.Reset();
            state.previousRecoil = 0;
        }
    } catch (...) {
        state.recoil.Reset();
    }
    // Apply kick compensation before the view/projection for this frame is constructed.
    state.setup(self);
    try {
        AfterSetup(reinterpret_cast<std::uintptr_t>(self));
    } catch (...) {
        state.havePrediction = false;
    }
}
bool TracerMuzzle(void *effect, Vector3 &out) noexcept {
    __try {
        using Start = Vector3 *(*)(Vector3 *, void *);
        reinterpret_cast<Start>(state.client + abi::TracerStart)(&out, effect);
        return PlausiblePosition(out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void CaptureTracer(void *effect, bool particle) {
    if (state.available && GetTickCount64() <= state.deadline) {
        try {
            bool enabled{};
            {
                std::scoped_lock lock(state.mutex);
                ++state.output.tracerCallbacks;
                if (particle)
                    ++state.output.particleCallbacks;
                enabled = state.settings.bulletTracers != 0;
            }
            if (enabled) {
                LocalMemory local;
                Memory m{&local, LocalMemory::Read};
                std::uintptr_t list{};
                flight::Shot shot;
                Vector3 muzzle;
                if (m.Field(state.client, offsets::EntityList, list) &&
                    ReadTracerEffect(m, list, reinterpret_cast<std::uintptr_t>(effect), shot,
                                     state.muzzleLayout && TracerMuzzle(effect, muzzle) ? &muzzle : nullptr)) {
                    shot.time = Seconds();
                    std::scoped_lock lock(state.mutex);
                    if (state.output.tracers.Add(shot))
                        ++state.output.acceptedTracers;
                } else {
                    std::scoped_lock lock(state.mutex);
                    ++state.output.rejectedTracers;
                }
            }
        } catch (...) {
        }
    }
}
void TracerHook(void *effect) {
    Guard guard;
    CaptureTracer(effect, false);
    state.tracer(effect);
}
void ParticleTracerHook(void *effect) {
    Guard guard;
    CaptureTracer(effect, true);
    state.particleTracer(effect);
}
void BulletPathHook(void *services, void *weapon, int shotIndex, int mode, Vector3 *start, Vector3 *end) {
    Guard guard;
    if (state.available && GetTickCount64() <= state.deadline) {
        try {
            bool enabled{};
            {
                std::scoped_lock lock(state.mutex);
                ++state.output.bulletCallbacks;
                enabled = state.settings.bulletTracers != 0;
            }
            if (enabled) {
                LocalMemory local;
                Memory m{&local, LocalMemory::Read};
                std::uintptr_t list{};
                flight::Shot shot;
                if (m.Field(state.client, offsets::EntityList, list) &&
                    ReadDirectShot(m, list, reinterpret_cast<std::uintptr_t>(services),
                                   reinterpret_cast<std::uintptr_t>(weapon), reinterpret_cast<std::uintptr_t>(start),
                                   reinterpret_cast<std::uintptr_t>(end), shot)) {
                    shot.time = Seconds();
                    std::scoped_lock lock(state.mutex);
                    if (state.output.tracers.Add(shot))
                        ++state.output.acceptedTracers;
                } else {
                    std::scoped_lock lock(state.mutex);
                    ++state.output.rejectedTracers;
                }
            }
        } catch (...) {
        }
    }
    state.bulletPath(services, weapon, shotIndex, mode, start, end);
}
struct EventData {
    char name[64]{};
    std::uintptr_t controller{}, attacker{}, pawn{};
    int damage{}, hitgroup{}, entityIndex{};
    Vector3 impact{};
};
bool ReadEvent(void *event, EventData &data) noexcept {
    const Key user("userid"), attacker("attacker"), damage("dmg_health"), hitgroup("hitgroup"), x("x"), y("y"), z("z"),
        entityId("entityid");
    __try {
        const auto *table = *reinterpret_cast<const std::uintptr_t **>(event);
        if (reinterpret_cast<std::uintptr_t>(table) != state.client + abi::EventVtable ||
            table[16] != state.client + abi::EventController)
            return false;
        using Name = const char *(*)(void *);
        using Controller = std::uintptr_t (*)(void *, const Key &);
        using Float = float (*)(void *, const Key &, float);
        const char *name = reinterpret_cast<Name>(table[1])(event);
        for (unsigned i = 0; i < sizeof(data.name); ++i) {
            data.name[i] = name[i];
            if (!name[i])
                break;
        }
        data.name[63] = 0;
        if (!std::strcmp(data.name, "weapon_fire") || !std::strcmp(data.name, "bullet_impact")) {
            data.controller = reinterpret_cast<Controller>(table[16])(event, user);
            if (!std::strcmp(data.name, "bullet_impact")) {
                const auto get = reinterpret_cast<Float>(table[9]);
                const float invalid = std::numeric_limits<float>::quiet_NaN();
                data.impact = {get(event, x, invalid), get(event, y, invalid), get(event, z, invalid)};
            }
        }
        if (!std::strcmp(data.name, "player_footstep")) {
            // Dispatch paths may store userid as a controller or pawn.
            // Validate accessors and then revalidate entity ownership before use.
            data.controller = reinterpret_cast<Controller>(table[16])(event, user);
            if (table[17] == state.client + 0x9B4FE0)
                data.pawn = reinterpret_cast<Controller>(table[17])(event, user);
        }
        if (!std::strcmp(data.name, "player_hurt")) {
            if (table[abi::EventIntSlot] != state.client + abi::EventGetInt)
                return false;
            using Int = int (*)(void *, const Key &, int);
            data.controller = reinterpret_cast<Controller>(table[16])(event, user);
            data.attacker = reinterpret_cast<Controller>(table[16])(event, attacker);
            data.damage = reinterpret_cast<Int>(table[abi::EventIntSlot])(event, damage, 0);
            data.hitgroup = reinterpret_cast<Int>(table[abi::EventIntSlot])(event, hitgroup, 0);
        }
        if (!std::strcmp(data.name, "inferno_startburn") || !std::strcmp(data.name, "inferno_expire") ||
            !std::strcmp(data.name, "inferno_extinguish")) {
            if (table[abi::EventIntSlot] != state.client + abi::EventGetInt)
                return false;
            using Int = int (*)(void *, const Key &, int);
            data.entityIndex = reinterpret_cast<Int>(table[abi::EventIntSlot])(event, entityId, 0);
            const auto get = reinterpret_cast<Float>(table[9]);
            const float invalid = std::numeric_limits<float>::quiet_NaN();
            data.impact = {get(event, x, invalid), get(event, y, invalid), get(event, z, invalid)};
        }
        if (!std::strcmp(data.name, "hegrenade_detonate") || !std::strcmp(data.name, "flashbang_detonate")) {
            const auto get = reinterpret_cast<Float>(table[9]);
            const float invalid = std::numeric_limits<float>::quiet_NaN();
            data.impact = {get(event, x, invalid), get(event, y, invalid), get(event, z, invalid)};
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void CaptureEvent(void *event) {
    if (!event || !state.available || GetTickCount64() > state.deadline)
        return;
    EventData data;
    if (!ReadEvent(event, data))
        return;
    const bool reset = !std::strcmp(data.name, "round_start") || !std::strcmp(data.name, "game_newmap") ||
                       !std::strcmp(data.name, "cs_game_disconnected");
    if (reset) {
        std::scoped_lock lock(state.mutex);
        state.pending = {};
        state.output.tracers.Clear();
        state.output.feedback.Clear();
        state.output.blasts.Clear();
        state.output.infernos.Clear();
        state.output.footsteps.Clear();
        state.output.recoil = {};
        state.output.prediction = {};
        ++state.output.resetSerial;
        return;
    }
    if (!std::strcmp(data.name, "inferno_startburn") || !std::strcmp(data.name, "inferno_expire") ||
        !std::strcmp(data.name, "inferno_extinguish")) {
        const bool burning = !std::strcmp(data.name, "inferno_startburn");
        if (data.entityIndex > 0 && data.entityIndex <= static_cast<int>(offsets::EntryMask)) {
            LocalMemory local;
            Memory memory{&local, LocalMemory::Read};
            std::uintptr_t list{};
            std::uint32_t handle{};
            if (memory.Field(state.client, offsets::EntityList, list)) {
                const auto entity = EntityAt(memory, list, data.entityIndex);
                FullHandle(memory, entity, handle);
                if (burning && !PlausiblePosition(data.impact)) {
                    std::uintptr_t scene{};
                    std::uint32_t after{};
                    if (!handle || !memory.Field(entity, offsets::SceneNode, scene) ||
                        !memory.Field(scene, offsets::Origin, data.impact) || !PlausiblePosition(data.impact) ||
                        !FullHandle(memory, entity, after) || after != handle)
                        return;
                }
            }
            if (burning && !PlausiblePosition(data.impact))
                return;
            std::scoped_lock lock(state.mutex);
            state.output.infernos.Update(data.entityIndex, handle, data.impact, Seconds(), burning);
            ++state.output.infernoEvents;
        }
        return;
    }
    if (!std::strcmp(data.name, "player_footstep")) {
        LocalMemory local;
        Memory m{&local, LocalMemory::Read};
        std::uintptr_t list{};
        worldvisuals::Footstep sample;
        bool read = false;
        if (m.Field(state.client, offsets::EntityList, list)) {
            read = ReadFootstep(m, list, data.pawn, Seconds(), sample);
            if (!read) {
                std::uint32_t owner{}, pawnHandle{};
                if (FullHandle(m, data.controller, owner) && EntityAt(m, list, owner) == data.controller &&
                    m.Field(data.controller, offsets::ControllerPawn, pawnHandle))
                    read = ReadFootstep(m, list, EntityAt(m, list, pawnHandle), Seconds(), sample) &&
                           sample.handle == pawnHandle;
            }
        }
        if (read) {
            std::scoped_lock lock(state.mutex);
            if (state.settings.worldVisuals.footsteps && state.output.footsteps.Add(sample))
                ++state.output.footstepEvents;
        }
        return;
    }
    const bool fired = !std::strcmp(data.name, "weapon_fire"), impact = !std::strcmp(data.name, "bullet_impact");
    const bool hurt = !std::strcmp(data.name, "player_hurt");
    if (fired || impact) {
        std::scoped_lock lock(state.mutex);
        if (fired)
            ++state.output.fireEvents;
        else
            ++state.output.impactEvents;
    }
    if (!std::strcmp(data.name, "hegrenade_detonate") || !std::strcmp(data.name, "flashbang_detonate")) {
        if (PlausiblePosition(data.impact)) {
            std::scoped_lock lock(state.mutex);
            if (state.settings.combat.areas && state.settings.combat.blastArea)
                state.output.blasts.Push({0, 1, data.impact, 0, Seconds(), false});
        }
        return;
    }
    if (!fired && !impact && !hurt)
        return;
    LocalMemory local;
    Memory m{&local, LocalMemory::Read};
    std::uintptr_t list{}, identity{}, pawnIdentity{};
    std::uint32_t handle{}, controllerHandle{};
    std::uint8_t team{};
    char name[64]{};
    if (!m.Field(state.client, offsets::EntityList, list) || !EntityName(m, data.controller, name) ||
        std::strcmp(name, "cs_player_controller") || !m.Field(data.controller, offsets::Identity, identity) ||
        !m.Field(identity, 0x10, controllerHandle) || EntityAt(m, list, controllerHandle) != data.controller ||
        !m.Field(data.controller, offsets::ControllerPawn, handle))
        return;
    const auto pawn = EntityAt(m, list, handle);
    std::uint32_t pawnHandle{};
    if (!m.Field(pawn, offsets::Identity, pawnIdentity) || !m.Field(pawnIdentity, 0x10, pawnHandle) ||
        pawnHandle != handle || !m.Field(pawn, offsets::Team, team) || team < 2 || team > 3)
        return;
    if (hurt) {
        std::uintptr_t localPawn{};
        std::uint32_t attackerController{}, attackerPawn{};
        Vector3 position{};
        if (!m.Field(state.client, offsets::LocalPawn, localPawn) || !localPawn ||
            !EntityName(m, data.attacker, name) || std::strcmp(name, "cs_player_controller") ||
            !FullHandle(m, data.attacker, attackerController) ||
            EntityAt(m, list, attackerController) != data.attacker ||
            !m.Field(data.attacker, offsets::ControllerPawn, attackerPawn) ||
            EntityAt(m, list, attackerPawn) != localPawn || !FullHandle(m, localPawn, attackerController) ||
            attackerController != attackerPawn || !ReadHitPosition(m, pawn, handle, data.hitgroup, position))
            return;
        std::array<char, 128> playerName{};
        m.Field(data.controller, offsets::PlayerName, playerName);
        playerName.back() = 0;
        std::scoped_lock lock(state.mutex);
        if (state.settings.combat.hitMarker || state.settings.combat.hitSound || state.settings.combat.damageNumbers ||
            state.settings.combat.hitLog)
            state.output.feedback.Add(handle, position, data.damage, data.hitgroup == 1, Seconds(), playerName.data());
        return;
    }
    Vector3 start{};
    if ((fired || impact) && !PlayerEye(m, pawn, start))
        return;
    const auto now = Seconds();
    std::scoped_lock lock(state.mutex);
    if (!state.settings.bulletTracers)
        return;
    const auto slot = (controllerHandle & offsets::EntryMask);
    if (slot < 1 || slot > 64)
        return;
    auto &pending = state.pending[slot - 1];
    if (fired) {
        pending = {handle, start, team, now};
        return;
    }
    // Multiple impact events from penetration / shotgun pellets keep the same fire origin.
    if (pending.handle == handle && now >= pending.time && now - pending.time <= .35 && PlausiblePosition(data.impact))
        state.output.acceptedTracers +=
            state.output.tracers.Add({pending.start, data.impact, handle & offsets::EntryMask, pending.team, now});
    else if (impact && PlausiblePosition(data.impact))
        state.output.acceptedTracers +=
            state.output.tracers.Add({start, data.impact, handle & offsets::EntryMask, team, now});
}
bool DispatchHook(void *self, void *event) {
    Guard guard;
    try {
        CaptureEvent(event);
    } catch (...) {
    }
    return state.dispatch(self, event);
}
} // namespace
HRESULT StartTrajectories() noexcept {
    if (state.installed)
        return S_OK;
    state.client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!state.client || !Validate()) {
        OverlayLog("Trajectories: game layout could not be verified.");
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    }
    auto install = [](std::uintptr_t address, void *detour, void **original) {
        auto *entry = reinterpret_cast<void *>(address);
        if (MH_CreateHook(entry, detour, original) != MH_OK)
            return false;
        if (MH_EnableHook(entry) == MH_OK)
            return true;
        MH_RemoveHook(entry);
        return false;
    };
    state.viewHook = state.viewLayout && install(state.client + abi::ViewSetup, reinterpret_cast<void *>(&SetupHook),
                                                 reinterpret_cast<void **>(&state.setup));
    state.eventHook =
        state.eventLayout && install(state.client + abi::EventDispatch, reinterpret_cast<void *>(&DispatchHook),
                                     reinterpret_cast<void **>(&state.dispatch));
    state.tracerHook =
        state.tracerLayout && install(state.client + abi::TracerEffect, reinterpret_cast<void *>(&TracerHook),
                                      reinterpret_cast<void **>(&state.tracer));
    state.particleHook = state.particleLayout &&
                         install(state.client + abi::ParticleTracer, reinterpret_cast<void *>(&ParticleTracerHook),
                                 reinterpret_cast<void **>(&state.particleTracer));
    state.bulletHook =
        state.bulletLayout && install(state.client + abi::BulletPath, reinterpret_cast<void *>(&BulletPathHook),
                                      reinterpret_cast<void **>(&state.bulletPath));
    state.installed = state.viewHook || state.eventHook || state.tracerHook || state.particleHook || state.bulletHook;
    state.available = state.installed;
    {
        std::scoped_lock lock(state.mutex);
        state.output.hooked = state.installed;
        state.output.viewConnected = state.viewHook;
        state.output.eventsConnected = state.eventHook;
        state.output.tracerConnected = state.tracerHook;
        state.output.punchConnected = state.punchLayout;
        state.output.bulletConnected = state.bulletHook;
        state.output.particleConnected = state.particleHook;
    }
    char message[240]{};
    std::snprintf(
        message, sizeof(message),
        "Feature hooks: view=%d events=%d tracers=%d punch=%d collision=%d muzzle=%d (independent validation).",
        state.viewHook, state.eventHook, state.tracerHook, state.punchLayout, state.sweepLayout, state.muzzleLayout);
    OverlayLog(message);
    std::snprintf(message, sizeof(message), "Bullet capture: direct weapon path=%d ParticleTracer=%d legacy Tracer=%d.",
                  state.bulletHook, state.particleHook, state.tracerHook);
    OverlayLog(message);
    if (!state.installed)
        return E_FAIL;
    return S_OK;
}
void ConfigureTrajectories(const VisualOptions &settings, bool fresh, bool recoilActive) noexcept {
    if (!state.installed)
        return;
    {
        std::scoped_lock lock(state.mutex);
        if (!fresh || !settings.bulletTracers) {
            state.output.tracers.Clear();
            state.pending = {};
        }
        if (!fresh || !settings.worldVisuals.footsteps)
            state.output.footsteps.Clear();
        if (!fresh || !settings.grenadePrediction)
            state.output.prediction = {};
        if (!fresh) {
            state.output.feedback.Clear();
            state.output.blasts.Clear();
            state.output.recoil = {};
            state.output.sampledAt = 0;
        }
        if (!settings.combat.recoil)
            state.output.recoil = {};
        state.settings = settings;
        state.output.tracers.SetLifetime(settings.paths.shotLifetime);
    }
    state.recoilActive = fresh && recoilActive;
    state.deadline = fresh ? GetTickCount64() + 250 : 0;
}
void RefreshTrajectoryInputs() noexcept {
    Guard guard;
    if (!state.available || GetTickCount64() > state.deadline)
        return;
    std::unique_lock lock(state.setupMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    try {
        VisualOptions settings;
        {
            std::scoped_lock data(state.mutex);
            settings = state.settings;
        }
        if (!state.viewHook)
            SampleInputs(settings, true);
    } catch (...) {
    }
}
void PauseTrajectories() noexcept {
    state.deadline = 0;
}
void CopyTrajectories(FlightSnapshot &out, DeferredLog *logger) noexcept {
    const auto now = Seconds();
    bool log{}, recoilActive{}, fresh{};
    unsigned recoilInput{}, recoilRequested{};
    {
        std::scoped_lock lock(state.mutex);
        state.output.tracers.Expire(now);
        out = state.output;
        log = now >= state.nextLog && (state.settings.bulletTracers || state.settings.combat.recoil);
        if (log) {
            state.nextLog = now + 5;
            recoilInput = state.settings.combat.recoilInput;
            recoilRequested = state.settings.combat.recoil;
            recoilActive = state.recoilActive;
            fresh = GetTickCount64() <= state.deadline;
        }
    }
    // File logging can block. Release the shared event/view snapshot lock before
    // formatting or writing diagnostics so engine callbacks can keep publishing.
    if (log && logger) {
        char message[512]{};
        std::snprintf(message, sizeof(message),
                      "Telemetry: shots=%llu fire=%llu impacts=%llu effects=%llu accepted=%llu rejected=%llu live=%zu; "
                      "recoil valid=%d weapon=%u handle=%08X shots=%u punch=(%.3f,%.3f) input=%u updates=%llu "
                      "failedReads=%llu result=%08X.",
                      out.fireSamples, out.fireEvents, out.impactEvents, out.tracerCallbacks, out.acceptedTracers,
                      out.rejectedTracers, out.tracers.Lines().count, out.recoil.valid, out.recoil.weapon,
                      out.recoil.weaponHandle, out.recoil.shots, out.recoil.punch.x, out.recoil.punch.y, recoilInput,
                      out.recoilWrites, out.recoilReadFailures, static_cast<unsigned>(out.recoilResult));
        logger->Push(message);
        std::snprintf(message, sizeof(message), "Input gate: active=%d fresh=%d requested=%u viewSamples=%llu.",
                      recoilActive, fresh, recoilRequested, out.setupSamples);
        logger->Push(message);
        std::snprintf(message, sizeof(message), "Bullet sources: direct=%llu particle=%llu.", out.bulletCallbacks,
                      out.particleCallbacks);
        logger->Push(message);
    }
    if (now - out.sampledAt > .25) {
        out.recoil = {};
        out.sampledAt = 0;
    }
    if (now - out.predictedAt > .25)
        out.prediction = {};
}
HRESULT StopTrajectories() noexcept {
    state.available = false;
    state.deadline = 0;
    if (!state.installed)
        return S_OK;
    auto *view = reinterpret_cast<void *>(state.client + abi::ViewSetup);
    auto *events = reinterpret_cast<void *>(state.client + abi::EventDispatch);
    auto *tracer = reinterpret_cast<void *>(state.client + abi::TracerEffect);
    auto *particle = reinterpret_cast<void *>(state.client + abi::ParticleTracer);
    auto *bullet = reinterpret_cast<void *>(state.client + abi::BulletPath);
    const auto d = state.particleHook ? MH_DisableHook(particle) : MH_OK;
    const auto e = state.bulletHook ? MH_DisableHook(bullet) : MH_OK;
    const auto a = state.viewHook ? MH_DisableHook(view) : MH_OK;
    const auto b = state.eventHook ? MH_DisableHook(events) : MH_OK;
    const auto c = state.tracerHook ? MH_DisableHook(tracer) : MH_OK;
    if ((a != MH_OK && a != MH_ERROR_DISABLED) || (b != MH_OK && b != MH_ERROR_DISABLED) ||
        (c != MH_OK && c != MH_ERROR_DISABLED) || (d != MH_OK && d != MH_ERROR_DISABLED) ||
        (e != MH_OK && e != MH_ERROR_DISABLED))
        return E_FAIL;
    const auto deadline = GetTickCount64() + 5000;
    while (state.inFlight) {
        if (GetTickCount64() > deadline)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    const auto remove = [](void *entry, bool &installed) {
        if (!installed)
            return true;
        const auto result = MH_RemoveHook(entry);
        if (result != MH_OK && result != MH_ERROR_NOT_CREATED)
            return false;
        installed = false;
        return true;
    };
    // Commit each successful removal. A later failure can then be retried without
    // treating hooks already removed by this attempt as a new shutdown failure.
    bool removed = true;
    removed = remove(view, state.viewHook) && removed;
    removed = remove(events, state.eventHook) && removed;
    removed = remove(tracer, state.tracerHook) && removed;
    removed = remove(particle, state.particleHook) && removed;
    removed = remove(bullet, state.bulletHook) && removed;
    if (!removed)
        return E_FAIL;
    state.installed = false;
    state.mouse.Reset();
    state.fov.Reset();
    state.predictionCadence.Reset();
    state.previousRecoil = 0;
    state.recoil.Reset();
    state.havePrediction = false;
    {
        std::scoped_lock lock(state.mutex);
        state.output = {};
        state.pending = {};
    }
    return S_OK;
}
} // namespace awareness::cs2
