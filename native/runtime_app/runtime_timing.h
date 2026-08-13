#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace runtime_app {

class AbsoluteDeadlineState {
public:
    AbsoluteDeadlineState(
        std::chrono::steady_clock::time_point start,
        std::chrono::steady_clock::duration interval);
    std::chrono::steady_clock::time_point next_deadline() const;
    std::uint64_t advance_after_tick(std::chrono::steady_clock::time_point now);

private:
    std::chrono::steady_clock::time_point next_deadline_;
    std::chrono::steady_clock::duration interval_;
};

enum class PrecisionSchedulerMode { WaitableTimer, LegacyFallback };

class PrecisionTickScheduler {
public:
    explicit PrecisionTickScheduler(unsigned int spin_tail_us = 50);
    ~PrecisionTickScheduler();
    PrecisionTickScheduler(const PrecisionTickScheduler&) = delete;
    PrecisionTickScheduler& operator=(const PrecisionTickScheduler&) = delete;

    void wait_until(std::chrono::steady_clock::time_point deadline);
    PrecisionSchedulerMode mode() const;
    const char* mode_name() const;

private:
    void* timer_ = nullptr;
    unsigned int spin_tail_us_ = 50;
    PrecisionSchedulerMode mode_ = PrecisionSchedulerMode::LegacyFallback;
};

enum class RuntimeThreadPriority {
    Normal,
    AboveNormal,
    Highest,
};

class HighResolutionTimerPeriod {
public:
    explicit HighResolutionTimerPeriod(unsigned int period_ms);
    ~HighResolutionTimerPeriod();

    HighResolutionTimerPeriod(const HighResolutionTimerPeriod&) = delete;
    HighResolutionTimerPeriod& operator=(const HighResolutionTimerPeriod&) = delete;

    unsigned int requested_period_ms() const;
    bool active() const;

private:
    unsigned int requested_period_ms_ = 0;
    bool active_ = false;
};

const char* runtime_thread_priority_name(RuntimeThreadPriority priority);
bool set_current_thread_priority(RuntimeThreadPriority priority);

// Binds the current process to efficiency cores (E-cores) when running on a
// hybrid CPU (Intel 12th-gen+). Pins only the lowest `count` E-cores; every
// remaining E-core stays available to the game. Returns true if the process was
// pinned; false when the platform is unsupported or no distinct E-core class
// was detected.
bool pin_process_to_efficiency_cores(unsigned int count = 0) noexcept;

std::chrono::steady_clock::duration coarse_sleep_duration_until(
    std::chrono::steady_clock::time_point due,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::duration precision_margin = std::chrono::milliseconds(1));

void sleep_until_precise(
    std::chrono::steady_clock::time_point due,
    std::chrono::steady_clock::duration precision_margin = std::chrono::milliseconds(1));

std::chrono::steady_clock::duration vision_service_wait_precision_margin();

} // namespace runtime_app
