#include "runtime_timing.h"

#ifdef _WIN32
#include <Windows.h>
#include <mmsystem.h>
#endif

#include <algorithm>
#include <thread>
#include <vector>

namespace runtime_app {
namespace {

#ifdef _WIN32
int native_thread_priority(RuntimeThreadPriority priority) {
    switch (priority) {
    case RuntimeThreadPriority::Highest:
        return THREAD_PRIORITY_HIGHEST;
    case RuntimeThreadPriority::AboveNormal:
        return THREAD_PRIORITY_ABOVE_NORMAL;
    case RuntimeThreadPriority::Normal:
    default:
        return THREAD_PRIORITY_NORMAL;
    }
}

constexpr DWORD kCreateWaitableTimerHighResolution = 0x00000002;

// Returns the affinity mask covering the lowest `count` efficiency cores
// (E-cores), or 0 when the CPU is uniform (no distinct E-core class) or the
// topology is unavailable. EfficiencyClass is a relative ranking: a higher value
// means more performance, so E-cores carry the minimum EfficiencyClass value.
DWORD_PTR efficiency_core_affinity_mask(unsigned int count) {
    DWORD byte_count = 0;
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore, nullptr, &byte_count) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return 0;
    }
    std::vector<BYTE> buffer(byte_count);
    auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
        buffer.data());
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore, first, &byte_count)) {
        return 0;
    }

    std::vector<std::pair<BYTE, DWORD_PTR>> cores;
    for (DWORD offset = 0;
         offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= byte_count;) {
        const auto* entry = reinterpret_cast<
            const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore &&
            entry->Processor.GroupCount > 0) {
            cores.emplace_back(
                entry->Processor.EfficiencyClass,
                entry->Processor.GroupMask[0].Mask);
        }
        if (entry->Size == 0) break;
        offset += entry->Size;
    }

    if (cores.empty()) return 0;
    const BYTE min_class = std::min_element(
        cores.begin(), cores.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; })
        ->first;
    const BYTE max_class = std::max_element(
        cores.begin(), cores.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; })
        ->first;
    if (min_class == max_class) {
        return 0;  // Uniform cores: nothing to separate.
    }

    DWORD_PTR mask = 0;
    for (const auto& [efficiency_class, core_mask] : cores) {
        if (efficiency_class == min_class) {
            mask |= core_mask;
        }
    }
    // Keep only the lowest `count` E-cores; the rest stay available to the game.
    DWORD_PTR result = 0;
    unsigned int kept = 0;
    while (mask != 0 && kept < count) {
        const DWORD_PTR lowest = mask & (~mask + 1);
        result |= lowest;
        mask &= (mask - 1);
        ++kept;
    }
    return result;
}
#endif

} // namespace

AbsoluteDeadlineState::AbsoluteDeadlineState(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::duration interval)
    : next_deadline_(start + interval), interval_(interval) {}

std::chrono::steady_clock::time_point AbsoluteDeadlineState::next_deadline() const {
    return next_deadline_;
}

std::uint64_t AbsoluteDeadlineState::advance_after_tick(
    std::chrono::steady_clock::time_point now) {
    if (interval_ <= std::chrono::steady_clock::duration::zero()) return 0;
    if (now < next_deadline_) {
        next_deadline_ += interval_;
        return 0;
    }
    const auto intervals_late = static_cast<std::uint64_t>((now - next_deadline_) / interval_);
    next_deadline_ += interval_ * static_cast<std::int64_t>(intervals_late + 1);
    return intervals_late;
}

PrecisionTickScheduler::PrecisionTickScheduler(unsigned int spin_tail_us)
    : spin_tail_us_(spin_tail_us) {
#ifdef _WIN32
    timer_ = CreateWaitableTimerExW(
        nullptr, nullptr, kCreateWaitableTimerHighResolution,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (timer_ != nullptr) mode_ = PrecisionSchedulerMode::WaitableTimer;
#endif
}

PrecisionTickScheduler::~PrecisionTickScheduler() {
#ifdef _WIN32
    if (timer_ != nullptr) CloseHandle(static_cast<HANDLE>(timer_));
#endif
}

void PrecisionTickScheduler::wait_until(std::chrono::steady_clock::time_point deadline) {
    const auto spin_tail = std::chrono::microseconds(spin_tail_us_);
#ifdef _WIN32
    if (timer_ != nullptr) {
        const auto now = std::chrono::steady_clock::now();
        const auto coarse_due = deadline - spin_tail;
        if (coarse_due > now) {
            const auto delay_100ns = std::chrono::duration_cast<
                std::chrono::duration<long long, std::ratio<1, 10'000'000>>>(
                    coarse_due - now).count();
            LARGE_INTEGER due{};
            due.QuadPart = -std::max<long long>(1, delay_100ns);
            if (SetWaitableTimerEx(static_cast<HANDLE>(timer_), &due, 0, nullptr, nullptr, nullptr, 0)) {
                WaitForSingleObject(static_cast<HANDLE>(timer_), INFINITE);
            } else {
                mode_ = PrecisionSchedulerMode::LegacyFallback;
            }
        }
    }
#endif
    if (mode_ == PrecisionSchedulerMode::LegacyFallback) {
        sleep_until_precise(deadline, std::chrono::milliseconds(1));
        return;
    }
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

PrecisionSchedulerMode PrecisionTickScheduler::mode() const { return mode_; }

const char* PrecisionTickScheduler::mode_name() const {
    return mode_ == PrecisionSchedulerMode::WaitableTimer ? "waitable_timer" : "legacy_fallback";
}

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
    case RuntimeThreadPriority::Highest:
        return "highest";
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

bool pin_process_to_efficiency_cores(unsigned int count) noexcept {
#ifdef _WIN32
    const DWORD_PTR mask = efficiency_core_affinity_mask(count);
    if (mask == 0) {
        return false;
    }
    return SetProcessAffinityMask(GetCurrentProcess(), mask) != 0;
#else
    (void)count;
    return false;
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
