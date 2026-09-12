#include "camera_visuals.hpp"
#include "scene_style.hpp"
#include "snapshot_worker.hpp"
#include <atomic>
#include <cstdio>
#include <future>
#include <limits>
using namespace awareness;
namespace {
unsigned checks{}, failures{};
void Check(bool ok, const char *why) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", why);
    }
}
bool Near(float a, float b) {
    return std::abs(a - b) < .001f;
}
struct CopyGate {
    std::atomic<bool> block{true};
    std::promise<void> entered;
    std::shared_future<void> release;
};
struct Publication {
    unsigned value{};
    std::array<unsigned, 64> cells{};
    CopyGate *gate{};
    Publication() = default;
    Publication(const Publication &) = default;
    Publication &operator=(const Publication &other) {
        if (other.gate && other.gate->block.exchange(false)) {
            other.gate->entered.set_value();
            other.gate->release.wait();
        }
        value = other.value;
        cells = other.cells;
        gate = other.gate;
        return *this;
    }
};
} // namespace
int main() {
    camera_visuals::Options options;
    Check(camera_visuals::Valid(options) && !options.thirdPerson && !options.viewmodelEnabled && !options.hideScoped,
          "camera effects default off with valid settings");
    const auto back = camera_visuals::Offset({0, 0, 0}, options);
    Check(Near(back.x, -100) && Near(back.y, 0) && Near(back.z, 8), "camera arm uses Source pitch and yaw axes");
    options.shoulder = 20;
    const auto right = camera_visuals::Offset({0, 90, 0}, options);
    Check(Near(right.x, -20) && Near(right.y, -100) && Near(right.z, 8), "shoulder offset rotates with the view");
    const auto pitched = camera_visuals::Offset({30, 0, 0}, options);
    Check(Near(pitched.z, 58), "looking down moves the rear camera above the eye");
    const auto clear = camera_visuals::Clipped({10, 20, 30}, {-100, 0, 0}, 1, false);
    Check(Near(clear.x, -90) && Near(clear.y, 20) && Near(clear.z, 30),
          "unobstructed camera uses full requested distance");
    const auto wall = camera_visuals::Clipped({10, 20, 30}, {-100, 0, 0}, .4f, false);
    Check(Near(wall.x, -28), "wall contact pulls the camera two units in front of the hit");
    const auto solid = camera_visuals::Clipped({10, 20, 30}, {-100, 0, 0}, .4f, true);
    Check(Near(solid.x, 10), "solid start keeps camera at the valid eye origin");
    const auto invalid =
        camera_visuals::Clipped({10, 20, 30}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, .4f, false);
    Check(Near(invalid.x, 10), "invalid trace geometry cannot poison the camera origin");
    {
        flight::Fov smoother;
        camera_visuals::Options fov;
        camera_visuals::FrameFov(smoother, 90, true, 120, false, fov, 1);
        Check(camera_visuals::FrameFov(smoother, 90, true, 120, false, fov, 1.1) > 100,
              "ordinary camera FOV transitions smoothly");
        Check(Near(camera_visuals::FrameFov(smoother, 30, true, 120, true, fov, 1.2), 30),
              "ordinary FOV never replaces native scope zoom when scoped override is off");
        fov.scopedFovEnabled = 1;
        fov.scopedFov = 45;
        Check(Near(camera_visuals::FrameFov(smoother, 30, true, 120, true, fov, 1.3), 45),
              "scoped override uses its independent FOV");
        Check(Near(camera_visuals::FrameFov(smoother, 90, true, 120, false, fov, 1.4), 90),
              "unscoping starts from engine FOV instead of carrying zoom into normal view");
    }
    {
        camera_visuals::PredictionCadence cadence;
        Check(cadence.Due(1, flight::Utility::HE, 1, false), "new grenade predicts immediately");
        Check(!cadence.Due(1.01, flight::Utility::HE, 1, false), "moving preview obeys 30Hz cap");
        Check(cadence.Due(1.04, flight::Utility::HE, 1, true), "stationary preview enters slower cadence");
        Check(!cadence.Due(1.05, flight::Utility::HE, 1, true), "stationary preview does not resweep per frame");
        Check(cadence.Due(1.06, flight::Utility::HE, 1, false),
              "motion resumes immediately without waiting the stationary 100ms interval");
        Check(!cadence.Due(1.065, flight::Utility::HE, 1, true) && !cadence.Due(1.07, flight::Utility::HE, 1, false),
              "alternating equal and changed polls cannot bypass moving preview rate cap");
        Check(cadence.Due(1.071, flight::Utility::Smoke, 1, false),
              "switching grenade types bypasses old preview schedule");
        Check(cadence.Due(1.072, flight::Utility::Smoke, .5f, false), "changing throw strength updates immediately");
        Check(cadence.Due(.5, flight::Utility::Smoke, .5f, false), "clock rollback resets preview cadence");
        Check(!cadence.Due(-1, flight::Utility::Smoke, .5f, false), "negative preview time is rejected");
    }
    {
        camera_visuals::Options vm;
        Vector3 offsets{1, 2, 3};
        float fov = 64;
        Check(!camera_visuals::ViewmodelOutputs(vm, false, offsets, fov) && Near(offsets.x, 1) && Near(fov, 64),
              "disabled viewmodel controls preserve original function outputs");
        vm.viewmodelEnabled = 1;
        vm.viewmodelOffset = {2, -3, 1};
        vm.viewmodelFov = 100;
        Check(camera_visuals::ViewmodelOutputs(vm, false, offsets, fov) && Near(offsets.x, 2) && Near(offsets.y, -3) &&
                  Near(fov, 100),
              "viewmodel overrides modify all three offsets and independent FOV");
        vm.hideScoped = 1;
        Check(camera_visuals::ViewmodelOutputs(vm, true, offsets, fov) && Near(offsets.z, -64) && Near(fov, 1),
              "scoped hiding takes precedence over configured viewmodel offsets");
        Check(camera_visuals::ViewmodelOutputs(vm, false, offsets, fov) && Near(offsets.z, 1) && Near(fov, 100),
              "unscoping restores configured frame-local outputs");
        vm.viewmodelOffset.x = std::numeric_limits<float>::infinity();
        Check(!camera_visuals::ViewmodelOutputs(vm, false, offsets, fov) && Near(offsets.z, 1) && Near(fov, 100),
              "invalid viewmodel settings cannot write temporary outputs");
    }
    {
        scene::Options scene;
        std::array<std::uint8_t, 4> color{200, 100, 50, 73};
        Check(scene::Tint(color, scene::Gains(scene)) == color, "disabled scene tint preserves draw color");
        scene.enabled = 1;
        scene.brightness = .5f;
        scene.tint = {.5f, 1, 0, .2f};
        const auto tinted = scene::Tint(color, scene::Gains(scene));
        Check(tinted == std::array<std::uint8_t, 4>{50, 50, 0, 73},
              "scene RGB gains preserve original draw transparency");
        scene.brightness = 2;
        scene.tint = {1, 1, 1, 1};
        Check(scene::Tint(color, scene::Gains(scene)) == std::array<std::uint8_t, 4>{255, 200, 100, 73},
              "bright scene colors saturate without integer wrap");
        scene.brightness = std::numeric_limits<float>::quiet_NaN();
        Check(scene::Gains(scene) == 0 && scene::Tint(color, scene::Gains(scene)) == color,
              "invalid scene settings fail closed");
    }
    {
        SnapshotWorker<Publication, unsigned> worker;
        auto gate = std::make_shared<CopyGate>();
        std::promise<void> release;
        gate->release = release.get_future().share();
        auto entered = gate->entered.get_future();
        worker.Start([gate](unsigned value, Publication &out, unsigned) {
            out.value = value;
            out.cells.fill(value);
            out.gate = gate.get();
        });
        worker.Configure(7);
        const bool publishing = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        Check(publishing, "worker reaches controlled publication-copy contention");
        Publication out;
        out.value = 99;
        out.cells.fill(99);
        std::uint64_t serial = 123;
        auto exchange = std::async(std::launch::async, [&] { return worker.TryExchange(9, out, serial); });
        const bool immediate = exchange.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
        Check(immediate, "TryExchange never queues behind a publication copy");
        release.set_value();
        const bool exchanged = exchange.get();
        Check(!exchanged && out.value == 99 && serial == 123,
              "contended exchange preserves the complete prior snapshot and serial");
        for (int i = 0; i < 100; ++i) {
            worker.TryExchange(9, out, serial);
            if (out.value == 9)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(out.value == 9, "next successful exchange renews request and receives its publication");
        for (auto cell : out.cells)
            Check(cell == out.value, "publication transfer never tears entity records");
        worker.Stop();
        worker.CopyIfNew(out, serial);
        const auto oldSerial = serial;
        out.value = 55;
        Check(worker.TryExchange(9, out, serial) && serial == oldSerial && out.value == 55,
              "unchanged publication avoids redundant full snapshot copy");
        worker.Invalidate();
        Check(worker.TryExchange(9, out, serial) && serial != oldSerial && out.value == 0,
              "invalidation transfers the cleared publication through TryExchange");
    }
    std::printf("Camera visual checks: %u checks, %u failures; offline fixtures only.\n", checks, failures);
    return failures ? 1 : 0;
}
