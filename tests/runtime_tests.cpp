#include "snapshot_worker.hpp"
#include "recoil_input.hpp"
#include "presets.hpp"
#include <atomic>
#include <cstdio>
using namespace awareness;
int main() {
    int failures{};
    const auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", why);
        }
    };
    combat::MouseRecoil mouse;
    LONG x{}, y{};
    unsigned calls{};
    auto send = [&](INPUT &input) {
        check(input.type == INPUT_MOUSE && (input.mi.dwFlags & MOUSEEVENTF_MOVE), "uses standard mouse input");
        x += input.mi.dx;
        y += input.mi.dy;
        ++calls;
        return UINT{1};
    };
    for (int i = 0; i < 100; ++i)
        check(SUCCEEDED(mouse.Apply({.0022f, 0, 0}, 1, .022f, .022f, send)), "fractional correction accepted");
    check(y == -10 && x == 0 && calls == 10, "small corrections accumulate without frame-rate loss");
    mouse.Reset();
    x = y = 0;
    check(mouse.Apply({.22f, .44f, 0}, 1, .022f, .022f, send) == S_OK && x == 20 && y == -10,
          "pitch and yaw counter recoil");
    mouse.Reset();
    x = y = 0;
    check(FAILED(mouse.Apply({.22f, 0, 0}, 1, .022f, .022f, [](INPUT &) { return UINT{0}; })),
          "blocked input is reported");
    check(mouse.Apply({.22f, 0, 0}, 1, .022f, .022f, send) == S_OK && y == -10,
          "retry does not double failed correction");
    check(FAILED(mouse.Apply({1, 0, 0}, 0, .022f, .022f, send)), "invalid sensitivity cannot generate input");
    for (auto kind : {Preset::Signature, Preset::Focus, Preset::Broadcast}) {
        Configuration c;
        VisualOptions v;
        TrackingConfiguration t;
        EffectsConfiguration e;
        c.worldUnitsPerMeter = 39.3700787f;
        t.hotkey = VK_XBUTTON1;
        std::strcpy(v.killSoundPath, "personal.wav");
        ApplyPreset(kind, c, v, t, e);
        check(ValidConfiguration(c) && ValidVisualOptions(v) && ValidTrackingConfiguration(t) &&
                  ValidEffectsConfiguration(e),
              "complete presets satisfy every configuration validator");
        check(!v.backgroundEnabled && !v.backgroundPath[0] && !v.labelBackground,
              "presets have no image or label background");
        check(t.hotkey == VK_XBUTTON1 && c.worldUnitsPerMeter == 39.3700787f &&
                  !std::strcmp(v.killSoundPath, "personal.wav"),
              "personal bindings, sound and coordinate scale are preserved");
        if (kind == Preset::Focus)
            check(!e.materialEnabled && !e.glowEnabled && !v.paths.shotGlow, "focus removes expensive extras");
        if (kind == Preset::Broadcast)
            check(!t.enabled && !v.combat.recoil, "observer preset disables input control");
    }
    struct Data {
        unsigned value{};
        std::array<unsigned, 64> cells{};
    };
    struct Request {
        unsigned value{};
    };
    SnapshotWorker<Data, Request> worker;
    std::mutex gate;
    std::condition_variable cv;
    bool entered{}, release{};
    worker.Start([&](const Request &request, Data &data, unsigned generation) {
        if (!generation) {
            std::unique_lock lock(gate);
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return release; });
        }
        data.value = request.value;
        data.cells.fill(request.value);
    });
    worker.Configure({1});
    {
        std::unique_lock lock(gate);
        check(cv.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }), "sampler entered");
    }
    Data out;
    const auto start = std::chrono::steady_clock::now();
    worker.Copy(out);
    check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50) && out.value == 0,
          "render copy never waits for a blocked memory scan");
    worker.Invalidate();
    worker.Configure({2});
    {
        std::lock_guard lock(gate);
        release = true;
    }
    cv.notify_all();
    for (int i = 0; i < 100; ++i) {
        worker.Copy(out);
        check(out.value != 1, "invalidation discards an in-flight old generation");
        if (out.value == 2)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(out.value == 2, "worker publishes new request after rescan");
    for (int i = 0; i < 50; ++i) {
        worker.Configure({static_cast<unsigned>(i + 3)});
        worker.Copy(out);
        for (auto cell : out.cells)
            check(cell == out.value, "publication has no torn records");
    }
    worker.Stop();
    std::uint64_t serial{};
    check(worker.CopyIfNew(out, serial), "first serial read copies the final publication");
    const auto unchanged = out.value;
    out.value = 999;
    check(!worker.CopyIfNew(out, serial) && out.value == 999, "unchanged publication skips the memory copy");
    worker.Invalidate();
    check(worker.CopyIfNew(out, serial) && out.value == 0, "invalidation is visible to serial-based readers");
    (void)unchanged;
    SnapshotWorker<Data, Request> expired;
    bool cleaned = false;
    expired.Start(
        [](const Request &, Data &data, unsigned) {
            data.value = 77;
            data.cells.fill(77);
            // Model a stalled reader which returns after the producer's idle deadline.
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        },
        [&] {
            std::lock_guard lock(gate);
            cleaned = true;
            cv.notify_all();
        });
    expired.Configure({});
    {
        std::unique_lock lock(gate);
        check(cv.wait_for(lock, std::chrono::seconds(2), [&] { return cleaned; }),
              "idle cleanup runs even when a sample spans the deadline");
    }
    expired.Copy(out);
    check(out.value == 0, "expired scan cannot publish stale data after its request lease");
    serial = 0;
    check(expired.CopyIfNew(out, serial) && out.value == 0,
          "idle transition publishes an empty snapshot to serial-based readers");
    expired.Stop();
    std::printf("Runtime checks: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
