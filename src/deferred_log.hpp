#pragma once
#include <array>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
namespace awareness {
// Runtime-owned, bounded diagnostic queue. Rendering never waits for the disk
// or allocates message storage. Best-effort diagnostics may be dropped on contention.
class DeferredLog {
    std::array<std::array<char, 512>, 32> entries_{};
    std::mutex mutex_;
    std::condition_variable ready_;
    std::size_t head_{}, count_{};
    bool stop_{};
    std::thread worker_;

  public:
    explicit DeferredLog(void (*write)(const char *) noexcept)
        : worker_([this, write] {
              for (;;) {
                  std::array<char, 512> line;
                  {
                      std::unique_lock lock(mutex_);
                      ready_.wait(lock, [&] { return stop_ || count_; });
                      if (!count_ && stop_)
                          return;
                      line = entries_[head_];
                      head_ = (head_ + 1) % entries_.size();
                      --count_;
                  }
                  write(line.data());
              }
          }) {}
    DeferredLog(const DeferredLog &) = delete;
    ~DeferredLog() { Stop(); }
    bool Push(const char *text) noexcept {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || stop_ || count_ == entries_.size() || !text)
            return false;
        auto &line = entries_[(head_ + count_) % entries_.size()];
        const auto length = std::min(std::strlen(text), line.size() - 1);
        std::memcpy(line.data(), text, length);
        line[length] = 0;
        ++count_;
        lock.unlock();
        ready_.notify_one();
        return true;
    }
    void Stop() noexcept {
        {
            std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        ready_.notify_one();
        if (worker_.joinable())
            worker_.join();
    }
};
} // namespace awareness
