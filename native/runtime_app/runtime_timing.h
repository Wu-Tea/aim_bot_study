#pragma once

#include <chrono>

namespace runtime_app {

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

std::chrono::steady_clock::duration coarse_sleep_duration_until(
    std::chrono::steady_clock::time_point due,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::duration precision_margin = std::chrono::milliseconds(1));

void sleep_until_precise(std::chrono::steady_clock::time_point due);

} // namespace runtime_app
