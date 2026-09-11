#pragma once
#include "profile_store.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>

namespace awareness::profiles {
// One ordered queue keeps a manual Save followed by Load deterministic while
// serialization, disk flushes and INI reads stay outside Present.
class WorkingFile {
  public:
    enum class Action { Save, Load, Reset };
    struct Job {
        Action action{};
        std::wstring path;
        State state;
        std::uint64_t revision{};
    };
    struct Result : Job {
        bool success{};
    };
    using Perform = std::function<bool(Job &)>;

  private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Job> pending_;
    std::deque<Result> finished_;
    std::thread worker_;
    bool stopping_{}, running_{};
    Perform perform_;
    static bool Disk(Job &job) {
        auto &s = job.state;
        if (job.action == Action::Save)
            return SaveSettings(job.path, s.config, s.visual, s.tracking, s.effects);
        const auto units = s.config.worldUnitsPerMeter;
        s = {};
        s.config.worldUnitsPerMeter = units;
        return LoadSettings(job.path, s.config, s.visual, &s.tracking, &s.effects);
    }
    void Run() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
                if (pending_.empty())
                    return;
                job = std::move(pending_.front());
                pending_.pop_front();
                running_ = true;
            }
            bool success{};
            try {
                success = perform_(job);
            } catch (...) {
                success = false;
            }
            {
                std::lock_guard lock(mutex_);
                finished_.push_back({std::move(job), success});
                running_ = false;
            }
            changed_.notify_all();
        }
    }

  public:
    explicit WorkingFile(Perform perform = Disk) : perform_(std::move(perform)) {}
    ~WorkingFile() { Stop(); }
    WorkingFile(const WorkingFile &) = delete;
    bool Submit(Job job) {
        std::lock_guard lock(mutex_);
        if (stopping_ || pending_.size() + finished_.size() >= 16)
            return false;
        if (!worker_.joinable())
            worker_ = std::thread([this] { Run(); });
        // Keep the latest pending save; never coalesce across an intervening load.
        if (job.action == Action::Save && !pending_.empty() && pending_.back().action == Action::Save)
            pending_.back() = std::move(job);
        else
            pending_.push_back(std::move(job));
        changed_.notify_one();
        return true;
    }
    bool Busy() {
        std::lock_guard lock(mutex_);
        return running_ || !pending_.empty() || !finished_.empty();
    }
    bool Poll(Result &out) {
        std::lock_guard lock(mutex_);
        if (finished_.empty())
            return false;
        out = std::move(finished_.front());
        finished_.pop_front();
        return true;
    }
    void Stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }
};
} // namespace awareness::profiles
