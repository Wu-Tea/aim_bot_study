#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <utility>

namespace runtime_app {

// Diagnostics have no control authority. A full/busy queue drops a diagnostic
// rather than making the input/output owner wait for a pipe or GUI consumer.
class AsyncConsole {
public:
    using Sink = std::function<void(const std::string&)>;
    explicit AsyncConsole(Sink sink = [](const std::string& line) { std::cout << line; })
        : sink_(std::move(sink)), worker_([this] { run(); }) {}
    ~AsyncConsole() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_one();
        worker_.join();
    }
    bool offer(std::string line) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock || stopping_ || count_ == queue_.size()) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        queue_[(read_ + count_) % queue_.size()] = std::move(line);
        ++count_;
        lock.unlock();
        ready_.notify_one();
        return true;
    }
    std::uint64_t dropped() const noexcept { return dropped_.load(); }
private:
    void run() {
        for (;;) {
            std::string line;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || count_ != 0; });
                if (count_ == 0) return;
                line = std::move(queue_[read_]);
                read_ = (read_ + 1) % queue_.size();
                --count_;
            }
            // The potentially blocking sink never owns the producer mutex.
            try { sink_(line); }
            catch (...) { dropped_.fetch_add(1, std::memory_order_relaxed); }
        }
    }
    Sink sink_;
    std::array<std::string, 128> queue_{};
    std::mutex mutex_;
    std::condition_variable ready_;
    std::size_t read_ = 0, count_ = 0;
    bool stopping_ = false;
    std::atomic<std::uint64_t> dropped_{0};
    std::thread worker_;
};

inline AsyncConsole& runtime_console() {
    static AsyncConsole console;
    return console;
}

class AsyncConsoleLine : public std::ostringstream {
public:
    ~AsyncConsoleLine() { runtime_console().offer(str()); }
};

} // namespace runtime_app
