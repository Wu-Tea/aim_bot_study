#include "frame_rate_counter.h"
#include "frame_rate_protocol.h"
#include "runtime_control_bridge.h"
#include "test_support/native_test_registry.h"

#include <chrono>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using runtime_app::FrameRateCounter;
constexpr std::uint64_t second = 1'000'000'000;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void time_weighted_average_and_zero_frame_waits() {
    FrameRateCounter counter;
    require(counter.snapshot().controller_ticks == 0, "unstarted counters are empty");
    counter.record_tick(0, true, false);
    for (std::uint64_t i = 1; i <= 180; ++i) counter.record_tick(i * second / 181, true, true);
    counter.record_tick(second, false, false);
    counter.record_tick(3 * second, true, false);
    for (std::uint64_t i = 1; i <= 640; ++i) counter.record_tick(3 * second + i * 4 * second / 641, true, true);
    counter.record_tick(7 * second, false, false);
    auto value = counter.snapshot();
    require(value.vision_frames == 820 && value.aim_frames == 820 && value.aim_ns == 5 * second && value.elapsed_ns == 7 * second,
        "180 frames/1s + 640 frames/4s give 164 Aim fps; idle time is excluded from Aim only");
    require(value.recent_elapsed_ns == 5 * second && value.recent_aim_ns == 4 * second && value.recent_aim_frames == 640,
        "the last completed 5s window retains exact Aim time and frame ownership");
    counter.record_tick(8 * second, true, false);
    counter.record_tick(14 * second, true, false);
    value = counter.snapshot();
    require(value.recent_aim_ns == 5 * second && value.recent_vision_frames == 0,
        "Aim stalls count toward the denominator and report zero, not missing data");
    const auto before = value;
    counter.record_tick(13 * second, false, true);
    require(counter.snapshot().controller_ticks == before.controller_ticks && counter.snapshot().vision_frames == before.vision_frames,
        "invalid clock order must not corrupt counters");
    counter.finish(15 * second);
    require(counter.snapshot().elapsed_ns == 15 * second && counter.snapshot().controller_ticks == before.controller_ticks,
        "shutdown accounts for the final interval without inventing a tick or frame");
    FrameRateCounter new_session;
    require(new_session.snapshot().aim_frames == 0 && new_session.snapshot().aim_ns == 0, "a new runtime resets the session");
}

void long_stall_has_bounded_memory_and_correct_duration() {
    static_assert(sizeof(FrameRateCounter) < 4096, "statistics must use a fixed small footprint");
    FrameRateCounter counter;
    counter.record_tick(second, true, true);
    counter.record_tick(24 * 3600 * second, true, false);
    const auto value = counter.snapshot();
    require(value.elapsed_ns == (24 * 3600 - 1) * second && value.aim_ns == value.elapsed_ns,
        "a long stall cannot be dropped from session duration");
    require(value.recent_aim_ns == 5 * second && value.recent_aim_frames == 0,
        "bounded recent buckets expire old frames while retaining the stalled Aim interval");
}

struct Interval { std::uint64_t start, end; bool aiming; };
struct Frame { std::uint64_t at; bool aiming; };
void compare_randomized(std::uint32_t seed) {
    std::mt19937 random(seed);
    for (int scenario = 0; scenario < 40; ++scenario) {
        const int length = scenario < 32 ? 4096 : 32768;
        FrameRateCounter counter;
        std::vector<Interval> intervals;
        std::vector<Frame> frames;
        std::uint64_t now = 17 * second + 37'000'001, start = now, aim_total = 0;
        bool aiming = false;
        counter.record_tick(now, aiming, false);
        for (int tick = 1; tick <= length; ++tick) {
            const auto previous = now;
            // Include many ordinary ticks, phase transitions and arbitrary
            // long gaps; the oracle below never uses the bucket algorithm.
            now += 1000 + random() % 20'000'000;
            if (tick % 997 == 0) now += 60 * second;
            intervals.push_back({previous, now, aiming});
            if (aiming) aim_total += now - previous;
            aiming = random() % 3 != 0;
            const bool fresh = random() % 7 == 0;
            if (fresh) frames.push_back({now, aiming});
            counter.record_tick(now, aiming, fresh);
            if (tick % 257 && tick != length) continue;
            const auto value = counter.snapshot();
            const auto end = now / FrameRateCounter::bucket_ns * FrameRateCounter::bucket_ns;
            const auto begin = std::max(start, end > FrameRateCounter::window_ns ? end - FrameRateCounter::window_ns : 0);
            std::uint64_t aim_frames = 0, recent_frames = 0, recent_aim_frames = 0, recent_aim_ns = 0;
            for (const auto& frame : frames) {
                if (frame.aiming) ++aim_frames;
                if (frame.at >= begin && frame.at < end) { ++recent_frames; if (frame.aiming) ++recent_aim_frames; }
            }
            for (const auto& interval : intervals) {
                const auto a = std::max(begin, interval.start), b = std::min(end, interval.end);
                if (b > a && interval.aiming) recent_aim_ns += b - a;
            }
            require(value.elapsed_ns == now - start && value.aim_ns == aim_total && value.vision_frames == frames.size() &&
                value.aim_frames == aim_frames && value.controller_ticks == std::uint64_t(tick + 1),
                "session counters must match an independent timestamp/frame oracle");
            require(value.recent_elapsed_ns == (end > begin ? end - begin : 0) && value.recent_aim_ns == recent_aim_ns &&
                value.recent_vision_frames == recent_frames && value.recent_aim_frames == recent_aim_frames,
                "recent counters must match exact independent interval intersections");
        }
    }
}

void memory_channel_preserves_control_abi_and_releases_resources() {
    require(sizeof(runtime_app::RuntimeControlSnapshot) == 640, "FPS cannot break existing learning/reload ABI");
    const auto name = L"Local\\cod_native_fps_" + std::to_wstring(GetCurrentProcessId());
    HANDLE handle = nullptr;
    runtime_app::RuntimeFrameRateMemory* memory = nullptr;
    {
        controller_native::RuntimeConfig config;
        runtime_app::RuntimeControlBridge bridge(config, [config] { return config; });
        handle = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
        require(handle != nullptr, "a pagefile-backed statistics channel exists with all log options disabled");
        memory = static_cast<runtime_app::RuntimeFrameRateMemory*>(MapViewOfFile(handle, FILE_MAP_READ, 0, 0, sizeof(runtime_app::RuntimeFrameRateMemory)));
        require(memory != nullptr, "read-only memory mapping is available to GUI");
        FrameRateCounter counter;
        counter.record_tick(0, true, true); counter.record_tick(second, true, true);
        const auto expected = counter.snapshot();
        bridge.offer_frame_rates(expected);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool published = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const LONG sequence = memory->sequence;
            MemoryBarrier();
            if (sequence & 1) continue;
            const auto copy = memory->snapshot;
            MemoryBarrier();
            if (sequence != memory->sequence) continue;
            if (copy.state == runtime_app::Sampling && copy.counts.aim_frames == expected.aim_frames) { published = true; break; }
            std::this_thread::yield();
        }
        require(published && memory->protocol == 1 && memory->snapshot.pid == GetCurrentProcessId(),
            "memory feedback publishes counters without any performance logger");
        counter.finish(2 * second); bridge.finish_frame_rates(counter.snapshot());
        require(memory->snapshot.state == runtime_app::Stopped && memory->snapshot.counts.aim_ns == 2 * second,
            "shutdown publishes final in-memory counts");
        UnmapViewOfFile(memory); memory = nullptr; CloseHandle(handle); handle = nullptr;
    }
    handle = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (handle) CloseHandle(handle);
    require(handle == nullptr, "statistics channel must not outlive its runtime owner");
}
} // namespace

void register_frame_rate_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "frame_rates_time_weighted_aim_and_stalls", time_weighted_average_and_zero_frame_waits);
    registry.add_case("BaseContracts", "frame_rates_bounded_long_stall", long_stall_has_bounded_memory_and_correct_duration);
    registry.add_case("BaseContracts", "frame_rates_randomized_development", [] { compare_randomized(20261004); });
    registry.add_case("BaseContracts", "frame_rates_randomized_validation", [] { compare_randomized(0x6eed9421); });
    registry.add_case("BaseContracts", "frame_rates_memory_only_channel_lifetime", memory_channel_preserves_control_abi_and_releases_resources);
}
