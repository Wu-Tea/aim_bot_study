#include "runtime_timing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <Windows.h>

namespace {
double process_cpu_seconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return static_cast<double>(k.QuadPart + u.QuadPart) / 10'000'000.0;
}
double percentile(std::vector<double> values, double q) {
    std::sort(values.begin(), values.end());
    if (values.empty()) return 0.0;
    const std::size_t rank = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(q * values.size())));
    return values[rank - 1];
}
}

int main(int argc, char** argv) {
    (void)runtime_app::set_current_thread_priority(runtime_app::RuntimeThreadPriority::AboveNormal);
    int seconds = 60;
    int tick_hz = 1000;
    std::string mode = "precision";
    std::string output = "scheduler_benchmark.json";
    unsigned int spin_tail_us = 50;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seconds" && i + 1 < argc) seconds = std::stoi(argv[++i]);
        else if (arg == "--tick-hz" && i + 1 < argc) tick_hz = std::stoi(argv[++i]);
        else if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output = argv[++i];
        else if (arg == "--spin-tail-us" && i + 1 < argc) spin_tail_us = static_cast<unsigned int>(std::stoul(argv[++i]));
    }
    if (seconds <= 0 || tick_hz <= 0 || tick_hz > 100'000) {
        std::cerr << "seconds must be positive and tick-hz must be in [1, 100000]\n";
        return 2;
    }
    using clock = std::chrono::steady_clock;
    const auto interval = std::chrono::nanoseconds(1'000'000'000ll / tick_hz);
    const auto started = clock::now();
    const double cpu_started = process_cpu_seconds();
    runtime_app::AbsoluteDeadlineState deadlines(started, interval);
    runtime_app::PrecisionTickScheduler scheduler(spin_tail_us);
    std::vector<double> intervals_us;
    std::vector<double> lateness_us;
    intervals_us.reserve(static_cast<std::size_t>(seconds) * tick_hz);
    lateness_us.reserve(intervals_us.capacity());
    auto previous = started;
    std::uint64_t missed = 0;
    std::uint64_t consecutive = 0;
    std::uint64_t max_consecutive = 0;
    while (clock::now() - started < std::chrono::seconds(seconds)) {
        const auto due = deadlines.next_deadline();
        if (mode == "legacy") runtime_app::sleep_until_precise(due);
        else scheduler.wait_until(due);
        const auto now = clock::now();
        intervals_us.push_back(std::chrono::duration<double, std::micro>(now - previous).count());
        lateness_us.push_back(std::max(0.0, std::chrono::duration<double, std::micro>(now - due).count()));
        previous = now;
        const auto skipped = deadlines.advance_after_tick(now);
        missed += skipped;
        consecutive = skipped > 0 ? consecutive + skipped : 0;
        max_consecutive = std::max(max_consecutive, consecutive);
    }
    const double elapsed = std::chrono::duration<double>(clock::now() - started).count();
    const double cpu_seconds = process_cpu_seconds() - cpu_started;
    std::ofstream stream(output, std::ios::trunc);
    stream << "{\n"
        << "  \"mode\": \"" << (mode == "legacy" ? "legacy" : scheduler.mode_name()) << "\",\n"
        << "  \"duration_seconds\": " << elapsed << ",\n"
        << "  \"requested_hz\": " << tick_hz << ",\n"
        << "  \"interval_ns\": " << interval.count() << ",\n"
        << "  \"spin_tail_us\": " << spin_tail_us << ",\n"
        << "  \"samples\": " << intervals_us.size() << ",\n"
        << "  \"achieved_hz\": " << intervals_us.size() / elapsed << ",\n"
        << "  \"process_cpu_seconds\": " << cpu_seconds << ",\n"
        << "  \"interval_us_p50\": " << percentile(intervals_us, .50) << ",\n"
        << "  \"interval_us_p95\": " << percentile(intervals_us, .95) << ",\n"
        << "  \"interval_us_p99\": " << percentile(intervals_us, .99) << ",\n"
        << "  \"lateness_us_p50\": " << percentile(lateness_us, .50) << ",\n"
        << "  \"lateness_us_p95\": " << percentile(lateness_us, .95) << ",\n"
        << "  \"lateness_us_p99\": " << percentile(lateness_us, .99) << ",\n"
        << "  \"missed_deadlines\": " << missed << ",\n"
        << "  \"max_consecutive_missed\": " << max_consecutive << "\n}\n";
    return 0;
}
