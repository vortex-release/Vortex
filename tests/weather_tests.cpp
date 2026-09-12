#include "weather_state.hpp"
#include <array>
#include <cstdio>
#include <limits>
using namespace awareness;
using namespace awareness::weather;
namespace {
unsigned checks{}, failures{};
void Check(bool value, const char *why) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", why);
    }
}
struct Fake {
    struct Record {
        Token token;
        Vector3 position;
        bool alive{};
    };
    std::array<Record, 64> records{};
    std::uint64_t epoch{1};
    unsigned allocations{}, moves{}, destroys{}, assets{}, wrongOwnerCalls{};
    bool unreadable{}, failCreate{}, failMove{}, failDestroy{}, pendingCreate{};
    Residency residency{Residency::Ready};
    Residency Asset(std::uint32_t, double) noexcept {
        ++assets;
        return residency;
    }
    Ownership Owns(Token &token) noexcept {
        if (token.epoch != epoch)
            return Ownership::Gone;
        if (unreadable)
            return Ownership::Unreadable;
        for (auto &r : records) {
            if (r.alive && r.token.index == token.index && r.token.epoch == token.epoch &&
                (!token.descriptor || token.descriptor == r.token.descriptor)) {
                token.descriptor = r.token.descriptor;
                return Ownership::Owned;
            }
        }
        return Ownership::Gone;
    }
    bool Create(std::uint32_t, Vector3 position, Token &out) noexcept {
        if (failCreate || allocations >= records.size())
            return false;
        const auto i = allocations++;
        out = {epoch, 0x10000u + i * 0x100u, i, i, !pendingCreate};
        records[i] = {out, position, true};
        if (pendingCreate)
            out.descriptor = 0;
        return true;
    }
    bool Move(Token &token, Vector3 position) noexcept {
        if (Owns(token) != Ownership::Owned) {
            ++wrongOwnerCalls;
            return false;
        }
        if (failMove)
            return false;
        ++moves;
        records[token.index].position = position;
        token.placed = true;
        return true;
    }
    bool Destroy(Token &token) noexcept {
        if (Owns(token) != Ownership::Owned) {
            ++wrongOwnerCalls;
            return false;
        }
        if (failDestroy)
            return false;
        ++destroys;
        records[token.index].alive = false;
        return true;
    }
};
Context Scene(std::uint64_t epoch = 1, Vector3 position = {100, 200, 30}) {
    return {epoch, position, true, true};
}
Options On(unsigned density = 1, unsigned kind = 0) {
    return {1, kind, density};
}
} // namespace
int main() {
    Check(weather::Valid({}) && !Options{}.enabled, "weather defaults off");
    Check(!weather::Valid({2, 0, 0}) && !weather::Valid({1, 3, 0}) && !weather::Valid({1, 0, 3}),
          "invalid configuration rejected");
    for (unsigned i = 0; i < 3; ++i)
        Check(Paths[i] && Paths[i][0], "each weather mode has a stock asset");
    Check(!NeedsTick(false, false, false) && NeedsTick(true, false, false),
          "active weather cannot create or move effects outside network completion");
    Check(NeedsTick(false, true, false) && NeedsTick(true, true, false),
          "retained cleanup progresses after map leave without a network completion stage");
    Check(!NeedsTick(false, true, true) && !NeedsTick(true, true, true),
          "disabled quiescent weather performs no native frame work");
    Check(NeedsTick(true, false, true), "enabling a quiescent system permits its first normal update");
    {
        Controller c;
        Fake f;
        c.Update(Scene(), {}, 1, f);
        Check(c.Empty() && !f.assets && !f.allocations, "disabled weather performs no resource request");
        c.Update(Scene(), On(2), 2, f);
        Check(f.allocations == 1, "dense weather creates only one effect in the first frame");
        for (unsigned i = 0; i < 100; ++i)
            c.Update(Scene(), On(2), 2.001 + i * .001, f);
        Check(f.allocations == 1, "render rate cannot bypass creation cadence");
        c.Update(Scene(), On(2), 2.16, f);
        c.Update(Scene(), On(2), 2.32, f);
        Check(f.allocations == 3 && c.Report().active == 3, "density has a strict three-effect bound");
        for (unsigned i = 0; i < 200; ++i)
            c.Update(Scene(), On(2), 3 + i * .01, f);
        Check(f.allocations == 3 && !f.moves, "stationary camera does not recreate or update control points");
        c.Update(Scene(1, {150, 200, 30}), On(2), 6, f);
        Check(f.moves == 3, "camera movement updates existing effects");
        c.Update(Scene(1, {160, 200, 30}), On(2), 6.001, f);
        Check(f.moves == 3, "movement updates are capped at thirty per second");
        c.Update(Scene(), On(0), 7, f);
        Check(f.destroys == 2 && c.Report().active == 1, "lower density retires surplus effects");
        c.Update(Scene(), {}, 7.001, f);
        Check(c.Empty() && f.destroys == 3, "disable bypasses update cadence and retires all owned effects");
    }
    {
        Controller c;
        Fake f;
        f.residency = Residency::Pending;
        for (unsigned i = 0; i < 40; ++i)
            c.Update(Scene(), On(), i * .01, f);
        Check(!f.allocations && c.Report().status == Status::Preparing,
              "pending async loads never enter particle creation");
        f.residency = Residency::Failed;
        c.Update(Scene(), On(), 1, f);
        Check(!f.allocations && c.Report().status == Status::Retrying, "failed assets have an honest inactive state");
        f.residency = Residency::Ready;
        c.Update(Scene(), On(), 2, f);
        Check(f.allocations == 1, "resource recovery resumes bounded creation");
    }
    {
        Controller c;
        Fake f;
        c.Update(Scene(), On(0), 1, f);
        f.epoch = 2;
        c.Update(Scene(2), On(0), 2, f);
        Check(f.destroys == 0 && f.wrongOwnerCalls == 0 && f.allocations == 2,
              "map generation changes never destroy recycled IDs in a replacement manager");
        f.records[1].token.descriptor += 16;
        c.Update(Scene(2), On(0), 3, f);
        Check(f.destroys == 0 && f.allocations == 3, "same ID with a different descriptor is not owned");
        Check(!f.wrongOwnerCalls, "no mutation targets an unowned effect");
    }
    {
        Controller c;
        Fake f;
        c.Update(Scene(), On(0), 1, f);
        f.unreadable = true;
        c.Update(Scene(), {}, 2, f);
        Check(!c.Empty() && !f.destroys && c.Report().status == Status::Stopping,
              "failed ownership read retains cleanup work without blind destruction");
        f.unreadable = false;
        f.failDestroy = true;
        c.Update(Scene(), {}, 3, f);
        Check(!c.Empty(), "native destruction failure remains retryable");
        f.failDestroy = false;
        c.Update(Scene(), {}, 4, f);
        Check(c.Empty() && f.destroys == 1, "cleanup retries complete after access recovers");
    }
    {
        Controller c;
        Fake f;
        c.Update(Scene(), On(0), 1, f);
        f.failDestroy = true;
        c.Update(Scene(), On(0, 1), 2, f);
        Check(f.allocations == 1, "mode changes cannot overlap new weather with pending cleanup");
        f.failDestroy = false;
        c.Update(Scene(), On(0, 1), 3, f);
        Check(f.allocations == 2 && f.destroys == 1, "mode changes resume after retirement");
        auto absent = Scene();
        absent.originValid = false;
        c.Update(absent, On(0, 1), 4, f);
        Check(c.Empty() && c.Report().status == Status::WaitingForScene,
              "missing scene stops effects without stale positions");
    }
    {
        Controller c;
        Fake f;
        f.failCreate = true;
        c.Update(Scene(), On(0), 1, f);
        Check(c.Report().failures == 1, "creation failures are counted");
        for (unsigned i = 0; i < 100; ++i)
            c.Update(Scene(), On(0), 1.001 + i * .01, f);
        Check(c.Report().failures == 1, "creation failure applies retry backoff");
        f.failCreate = false;
        c.Update(Scene(), On(0), 3.1, f);
        Check(f.allocations == 1, "creation can recover after backoff");
        f.failMove = true;
        c.Update(Scene(1, {300, 200, 30}), On(0), 4, f);
        Check(c.Empty() && f.destroys == 1, "failed control-point updates retire the broken effect");
        f.failMove = false;
        c.Update(Scene(), On(0), .5, f);
        Check(f.allocations == 2, "clock rollback resets cadence rather than permanently pausing weather");
    }
    {
        Controller c;
        Fake f;
        auto bad = Scene();
        bad.origin.x = std::numeric_limits<float>::quiet_NaN();
        c.Update(bad, On(), 1, f);
        Check(!f.allocations && !f.assets, "invalid camera math never reaches the native particle API");
        c.Update(Scene(), On(), std::numeric_limits<double>::infinity(), f);
        Check(!f.allocations, "invalid timing is rejected");
        c.Update(Scene(), {1, 999, 999}, 2, f);
        Check(!f.allocations, "corrupt profiles cannot index the asset array");
    }
    {
        Controller c;
        Fake f;
        f.pendingCreate = true;
        f.unreadable = true;
        c.Update(Scene(), On(0), 1, f);
        Check(f.allocations == 1 && !c.Empty(), "unreadable new descriptor retains its pending ID");
        c.Update(Scene(), On(0), 2, f);
        Check(f.allocations == 1 && !f.moves, "pending ID is not orphaned or replaced");
        f.unreadable = false;
        c.Update(Scene(), On(0), 3, f);
        Check(f.moves == 1 && c.Report().status == Status::Active,
              "pending effect is positioned after recovery even when camera is stationary");
    }
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
