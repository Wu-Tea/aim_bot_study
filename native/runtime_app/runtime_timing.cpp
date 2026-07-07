#include "runtime_timing.h"

#ifdef _WIN32
#include <Windows.h>
#include <mmsystem.h>
#endif

#include <algorithm>
#include <thread>

namespace runtime_app {
namespace {

#ifdef _WIN32
int native_thread_priority(RuntimeThreadPriority priority) {
    switch (priority) {
    case RuntimeThreadPriority::AboveNormal:
        return THREAD_PRIORITY_ABOVE_NORMAL;
    case RuntimeThreadPriority::Normal:
    default:
        return THREAD_PRIORITY_NORMAL;
    }
}
#endif

} // namespace

HighResolutionTimerPeriod::HighResolutionTimerPeriod(unsigned int period_ms)
    : requested_period_ms_(period_ms) {
#ifdef _WIN32
    if (period_ms > 0u) {
        active_ = timeBeginPeriod(period_ms) == TIMERR_NOERROR;
    }
#else
    active_ = period_ms > 0u;
#endif
}

HighResolutionTimerPeriod::~HighResolutionTimerPeriod() {
#ifdef _WIN32
    if (active_) {
        timeEndPeriod(requested_period_ms_);
    }
#endif
}

unsigned int HighResolutionTimerPeriod::requested_period_ms() const {
    return requested_period_ms_;
}

bool HighResolutionTimerPeriod::active() const {
    return active_;
}

const char* runtime_thread_priority_name(RuntimeThreadPriority priority) {
    switch (priority) {
    case RuntimeThreadPriority::AboveNormal:
        return "above_normal";
    case RuntimeThreadPriority::Normal:
    default:
        return "normal";
    }
}

bool set_current_thread_priority(RuntimeThreadPriority priority) {
#ifdef _WIN32
    return SetThreadPriority(GetCurrentThread(), native_thread_priority(priority)) != 0;
#else
    (void)priority;
    return true;
#endif
}

std::chrono::steady_clock::duration coarse_sleep_duration_until(
    std::chrono::steady_clock::time_point due,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::duration precision_margin) {
    if (due <= now) {
        return std::chrono::steady_clock::duration::zero();
    }
    const auto remaining = due - now;
    if (remaining <= precision_margin) {
        return std::chrono::steady_clock::duration::zero();
    }
    return remaining - precision_margin;
}

void sleep_until_precise(
    std::chrono::steady_clock::time_point due,
    std::chrono::steady_clock::duration precision_margin) {
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= due) {
            return;
        }
        const auto coarse_sleep = coarse_sleep_duration_until(due, now, precision_margin);
        if (coarse_sleep > std::chrono::steady_clock::duration::zero()) {
            std::this_thread::sleep_for(coarse_sleep);
        } else {
            std::this_thread::yield();
        }
    }
}

std::chrono::steady_clock::duration vision_service_wait_precision_margin() {
    return std::chrono::milliseconds(3);
}

} // namespace runtime_app
