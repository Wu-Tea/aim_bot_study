#include "vision_native/ego_motion_observer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace vision_native;

std::uint8_t pattern(int x, int y) {
    const int value = (x * 17) + (y * 29) + ((x * y) % 37) * 5;
    return static_cast<std::uint8_t>(value & 0xff);
}

std::array<std::uint8_t, kEgoMotionPixelCount> make_frame(int dx, int dy) {
    std::array<std::uint8_t, kEgoMotionPixelCount> pixels{};
    for (int y = 0; y < kEgoMotionFrameHeight; ++y) {
        for (int x = 0; x < kEgoMotionFrameWidth; ++x) {
            const int source_x = std::clamp(x - dx, 0, kEgoMotionFrameWidth - 1);
            const int source_y = std::clamp(y - dy, 0, kEgoMotionFrameHeight - 1);
            pixels[static_cast<std::size_t>(y) * kEgoMotionFrameWidth + x] =
                pattern(source_x, source_y);
        }
    }
    return pixels;
}

EgoMotionFrameView view(
    std::uint64_t id,
    const std::array<std::uint8_t, kEgoMotionPixelCount>& pixels) {
    EgoMotionFrameView value;
    value.frame_id = id;
    value.source_present_qpc = id * 100;
    value.source_present_qpc_frequency = 1'000'000;
    value.source_present_steady_ns = id * 1'000'000;
    value.source_present_calibration_id = id;
    value.source_present_steady_available = true;
    value.captured_at_ns = id * 1'000;
    value.result_at_ns = id * 1'100;
    value.width = kEgoMotionFrameWidth;
    value.height = kEgoMotionFrameHeight;
    value.row_pitch = kEgoMotionFrameWidth;
    value.gray = pixels.data();
    return value;
}

struct Case {
    int dx;
    int dy;
};

void run_radius(int radius) {
    EgoMotionObserverConfig config;
    config.search_radius_px = radius;
    const std::array<Case, 18> cases{{
        {0, 0}, {5, 0}, {-5, 0}, {0, 5}, {0, -5}, {8, 0}, {-8, 0},
        {12, 0}, {-12, 0}, {0, 12}, {0, -12},
        {12, 12}, {-12, -12}, {12, -12}, {-12, 12}, {9, -11},
        {4, 3}, {-4, -3}}};
    std::vector<double> timings_ms;
    timings_ms.reserve(cases.size() * 12);
    std::size_t valid = 0;
    std::size_t accurate = 0;
    std::size_t boundary_limited = 0;
    std::uint32_t boundary_hits = 0;
    for (int repeat = 0; repeat < 12; ++repeat) {
        for (std::size_t index = 0; index < cases.size(); ++index) {
            const auto previous_pixels = make_frame(0, 0);
            const auto current_pixels = make_frame(cases[index].dx, cases[index].dy);
            const auto previous = view(
                static_cast<std::uint64_t>(index + 1), previous_pixels);
            const auto current = view(
                static_cast<std::uint64_t>(index + 2), current_pixels);
            const auto start = std::chrono::steady_clock::now();
            const auto estimate = EgoMotionObserver::estimate_pair(
                previous, current, config);
            const auto end = std::chrono::steady_clock::now();
            timings_ms.push_back(std::chrono::duration<double, std::milli>(
                end - start).count());
            if (estimate.valid) ++valid;
            if (estimate.valid &&
                std::abs(estimate.background_dx - cases[index].dx) <= 1.0f &&
                std::abs(estimate.background_dy - cases[index].dy) <= 1.0f) {
                ++accurate;
            }
            if (estimate.invalid_reason ==
                EgoMotionInvalidReason::SearchBoundaryLimited) {
                ++boundary_limited;
            }
            boundary_hits += estimate.boundary_hit_count;
        }
    }
    std::sort(timings_ms.begin(), timings_ms.end());
    const auto percentile = [&timings_ms](double fraction) {
        const std::size_t index = std::min(
            timings_ms.size() - 1,
            static_cast<std::size_t>(std::ceil(
                fraction * static_cast<double>(timings_ms.size()))) - 1);
        return timings_ms[index];
    };
    std::cout << "{\"radius\":" << radius
              << ",\"samples\":" << timings_ms.size()
              << ",\"valid\":" << valid
              << ",\"accurate\":" << accurate
              << ",\"boundary_limited\":" << boundary_limited
              << ",\"boundary_hits\":" << boundary_hits
              << ",\"p50_ms\":" << percentile(0.50)
              << ",\"p95_ms\":" << percentile(0.95)
              << ",\"p99_ms\":" << percentile(0.99)
              << ",\"max_ms\":" << timings_ms.back() << "}\n";
}

}  // namespace

int main() {
    run_radius(8);
    run_radius(12);
    run_radius(14);
    run_radius(16);
    return 0;
}
