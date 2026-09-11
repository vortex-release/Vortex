#include "profile_io.hpp"
#include <cstdio>
using namespace awareness::profiles;
HMODULE OverlayModule() noexcept {
    return GetModuleHandleW(nullptr);
}
void OverlayLog(const char *) noexcept {}
int main() {
    unsigned failures{};
    const auto check = [&](bool ok, const char *label) {
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", label);
        }
    };
    std::mutex gate;
    std::condition_variable cv;
    bool entered{}, release{};
    State stored;
    unsigned writes{};
    WorkingFile file([&](WorkingFile::Job &job) {
        if (job.action == WorkingFile::Action::Save) {
            if (!writes++) {
                std::unique_lock lock(gate);
                entered = true;
                cv.notify_all();
                cv.wait(lock, [&] { return release; });
            }
            stored = job.state;
        } else
            job.state = stored;
        return true;
    });
    State a;
    a.config.opacity = .2f;
    check(file.Submit({WorkingFile::Action::Save, L"working.ini", a, 1}), "submit first save");
    {
        std::unique_lock lock(gate);
        check(cv.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }), "worker started independently");
    }
    check(file.Submit({WorkingFile::Action::Load, L"working.ini", {}, 2}), "load queues behind save");
    State b;
    b.config.opacity = .7f;
    check(file.Submit({WorkingFile::Action::Save, L"working.ini", b, 3}), "later save queues after load");
    {
        std::lock_guard lock(gate);
        release = true;
    }
    cv.notify_all();
    file.Stop();
    WorkingFile::Result result;
    check(file.Poll(result) && result.success && result.revision == 1, "first completion ordered");
    check(file.Poll(result) && result.success && result.action == WorkingFile::Action::Load &&
              result.state.config.opacity == .2f,
          "Save then Load reads exactly the preceding snapshot");
    check(file.Poll(result) && result.revision == 3 && stored.config.opacity == .7f, "shutdown drains latest save");
    check(!file.Poll(result) && !file.Busy(), "consumed completions release busy state");
    check(!file.Submit({}), "shutdown rejects new work");
    WorkingFile failed([](WorkingFile::Job &) -> bool { throw 1; });
    failed.Submit({WorkingFile::Action::Save, L"failure.ini", a, 9});
    failed.Stop();
    check(failed.Poll(result) && !result.success && result.revision == 9,
          "failed writes retain revision and report failure");
    std::printf("Working profile I/O: %u failures\n", failures);
    return failures ? 1 : 0;
}
