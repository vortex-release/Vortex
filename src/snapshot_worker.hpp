#pragma once
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace awareness {
// One producer owns the reader. Rendering copies a complete publication, never
// waits for a memory scan, and never observes half-written entity records.
template <class Data, class Request> class SnapshotWorker {
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    Request request_{};
    Data published_{};
    std::thread thread_;
    bool stop_{}, active_{};
    unsigned generation_{};
    std::uint64_t serial_{};
    std::chrono::steady_clock::time_point deadline_{};

  public:
    SnapshotWorker() = default;
    SnapshotWorker(const SnapshotWorker &) = delete;
    ~SnapshotWorker() { Stop(); }
    struct NoIdle {
        void operator()() const noexcept {}
    };
    template <class Sample, class Idle = NoIdle> void Start(Sample sample, Idle idle = {}) {
        thread_ = std::thread([this, sample = std::move(sample), idle = std::move(idle)]() mutable {
            auto scratch = std::make_unique<Data>();
            for (;;) {
                Request request;
                unsigned generation;
                {
                    std::unique_lock lock(mutex_);
                    condition_.wait(lock, [&] { return stop_ || active_; });
                    if (stop_)
                        return;
                    if (std::chrono::steady_clock::now() > deadline_) {
                        active_ = false;
                        published_ = {};
                        ++serial_;
                        lock.unlock();
                        idle();
                        continue;
                    }
                    request = request_;
                    generation = generation_;
                }
                const auto next = std::chrono::steady_clock::now() + std::chrono::milliseconds(15);
                sample(request, *scratch, generation);
                {
                    std::unique_lock lock(mutex_);
                    if (stop_)
                        return;
                    // A slow scan must not revive data after the request lease
                    // expired; the idle path clears the previous publication.
                    if (generation == generation_ && std::chrono::steady_clock::now() <= deadline_) {
                        published_ = *scratch;
                        ++serial_;
                    }
                    condition_.wait_until(lock, next, [&] { return stop_ || generation != generation_; });
                    if (stop_)
                        return;
                }
            }
        });
    }
    void Configure(const Request &request) {
        {
            std::lock_guard lock(mutex_);
            request_ = request;
            active_ = true;
            deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        }
        condition_.notify_one();
    }
    void Copy(Data &out) const {
        std::lock_guard lock(mutex_);
        out = published_;
    }
    bool CopyIfNew(Data &out, std::uint64_t &serial) const {
        std::lock_guard lock(mutex_);
        if (serial == serial_)
            return false;
        out = published_;
        serial = serial_;
        return true;
    }
    void Invalidate() {
        {
            std::lock_guard lock(mutex_);
            ++generation_;
            published_ = {};
            ++serial_;
        }
        condition_.notify_one();
    }
    void Stop() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        condition_.notify_one();
        if (thread_.joinable())
            thread_.join();
    }
};
} // namespace awareness
