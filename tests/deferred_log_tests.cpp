#include "deferred_log.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
using namespace std::chrono_literals;
std::atomic<bool> entered{}, release{};
std::atomic<unsigned> writes{};
void Write(const char *s) noexcept {
    if (std::string_view{s} == "blocked") {
        entered = true;
        while (!release)
            std::this_thread::sleep_for(1ms);
    }
    ++writes;
}
int main() {
    awareness::DeferredLog log(Write);
    if (!log.Push("blocked"))
        return 1;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!entered && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    if (!entered) {
        release = true;
        return 2;
    }
    unsigned accepted{};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i)
        accepted += log.Push("queued");
    const bool bounded = std::chrono::steady_clock::now() - start < 100ms && accepted == 32;
    release = true;
    log.Stop();
    if (!bounded || writes != 33 || log.Push("after stop"))
        return 3;
    std::puts("Deferred logging: nonblocking bounded queue, drain and shutdown passed.");
}
