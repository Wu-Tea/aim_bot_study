#pragma once

#include <chrono>

namespace runtime_app {

enum class RuntimeThreadPriority {
    Normal,
    AboveNormal,
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

std::chrono::steady_clock::duration coarse_sleep_duration_until(
    std::chrono::steady_clock::time_point due,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::duration precision_margin = std::chrono::milliseconds(1));

void sleep_until_precise(
    std::chrono::steady_clock::time_point due,
    std::chrono::steady_clock::duration precision_margin = std::chrono::milliseconds(1));

std::chrono::steady_clock::duration vision_service_wait_precision_margin();

} // namespace runtime_app
