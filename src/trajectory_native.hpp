#pragma once
#include "settings.hpp"
#include "deferred_log.hpp"
#include "frame_clock.hpp"
#include <awareness/Trajectories.hpp>
#include <Windows.h>
namespace awareness::cs2 {
struct LineupSample {
    Vector3 feet{}, eye{}, angles{};
    std::uint32_t weapon{};
    double time{};
    bool valid{};
};
struct FlightSnapshot {
    LineupSample lineup;
    flight::Prediction prediction;
    flight::Tracers tracers;
    combat::Feedback feedback;
    combat::RecoilSample recoil;
    flight::Ring<combat::Hit, 16> blasts;
    combat::InfernoEvents infernos;
    worldvisuals::Footsteps footsteps;
    std::uint64_t footstepEvents{};
    std::uint64_t infernoEvents{};
    float gameTime{};
    double sampledAt{};
    double predictedAt{};
    std::uint64_t resetSerial{};
    bool hooked{}, collisionReady{};
    std::uint64_t tracerCallbacks{}, acceptedTracers{}, setupSamples{}, recoilWrites{};
    bool viewConnected{}, eventsConnected{}, tracerConnected{}, punchConnected{}, bulletConnected{},
        particleConnected{};
    std::uint64_t bulletCallbacks{}, particleCallbacks{};
    std::uint64_t fireSamples{}, fireEvents{}, impactEvents{}, rejectedTracers{}, recoilReadFailures{};
    HRESULT recoilResult{S_FALSE};
};
HRESULT StartTrajectories() noexcept;
HRESULT StopTrajectories() noexcept;
void ConfigureTrajectories(const VisualOptions &, bool fresh, bool recoilActive) noexcept;
void RefreshTrajectoryInputs() noexcept;
void PauseTrajectories() noexcept;
void CopyTrajectories(FlightSnapshot &, DeferredLog *log = nullptr) noexcept;
} // namespace awareness::cs2
