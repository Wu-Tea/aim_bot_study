#include "vision_native/ego_motion_observer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

using namespace vision_native;

std::uint8_t pattern(int x, int y) {
    const int value = (x * 17) + (y * 29) + ((x * y) % 37) * 5;
    return static_cast<std::uint8_t>(value & 0xff);
}

std::array<std::uint8_t, kEgoMotionPixelCount> make_frame(
    int dx = 0,
    int dy = 0,
    bool moving_foreground = false,
    bool noisy = false) {
    std::array<std::uint8_t, kEgoMotionPixelCount> pixels{};
    for (int y = 0; y < kEgoMotionFrameHeight; ++y) {
        for (int x = 0; x < kEgoMotionFrameWidth; ++x) {
            const int source_x = std::clamp(x - dx, 0, kEgoMotionFrameWidth - 1);
            const int source_y = std::clamp(y - dy, 0, kEgoMotionFrameHeight - 1);
            std::uint8_t value = pattern(source_x, source_y);
            if (moving_foreground && x >= 36 && x < 88 && y >= 24 && y < 68) {
                value = static_cast<std::uint8_t>((x * 11 + y * 3 + 91) & 0xff);
            }
            if (noisy) {
                value = static_cast<std::uint8_t>((x * 73 + y * 41 + 19) & 0xff);
            }
            pixels[static_cast<std::size_t>(y) * kEgoMotionFrameWidth + x] = value;
        }
    }
    return pixels;
}

EgoMotionFrameView view(
    std::uint64_t id,
    const std::array<std::uint8_t, kEgoMotionPixelCount>& pixels,
    const EgoMotionMaskRect* masks = nullptr,
    std::size_t mask_count = 0) {
    return EgoMotionFrameView{
        id, id * 100, 1'000'000, id * 1'000, id * 1'100,
        kEgoMotionFrameWidth, kEgoMotionFrameHeight,
        kEgoMotionFrameWidth, pixels.data(), masks, mask_count};
}

void assert_translation(int dx, int dy) {
    const auto previous_pixels = make_frame();
    const auto current_pixels = make_frame(dx, dy);
    const auto estimate = EgoMotionObserver::estimate_pair(
        view(1, previous_pixels), view(2, current_pixels));
    assert(estimate.available);
    assert(estimate.valid);
    assert(std::abs(estimate.background_dx - static_cast<float>(dx)) <= 1.0f);
    assert(std::abs(estimate.background_dy - static_cast<float>(dy)) <= 1.0f);
    assert(std::abs(estimate.camera_dx + static_cast<float>(dx)) <= 1.0f);
    assert(std::abs(estimate.camera_dy + static_cast<float>(dy)) <= 1.0f);
    assert(estimate.current_frame_id == 2);
    assert(estimate.previous_result_ns < estimate.current_result_ns);
}

void test_deterministic_pairs() {
    assert_translation(0, 0);
    assert_translation(5, 0);
    assert_translation(-5, 0);
    assert_translation(0, 4);
    assert_translation(0, -4);
    assert_translation(4, -3);

    const auto previous_pixels = make_frame();
    const auto current_pixels = make_frame(2, 1);
    const auto foreground_pixels = make_frame(3, 2, true);
    const EgoMotionMaskRect foreground{30, 18, 94, 74};
    const auto estimate = EgoMotionObserver::estimate_pair(
        view(10, previous_pixels), view(11, foreground_pixels, &foreground, 1));
    assert(estimate.available && estimate.valid);
    assert(std::abs(estimate.background_dx - 3.0f) <= 1.0f);
    assert(std::abs(estimate.background_dy - 2.0f) <= 1.0f);

    const auto noisy_pixels = make_frame(0, 0, false, true);
    const auto noisy = EgoMotionObserver::estimate_pair(
        view(20, previous_pixels), view(21, noisy_pixels));
    assert(noisy.available);
    assert(!noisy.valid || std::abs(noisy.background_dx) <= 2.0f);

    const EgoMotionMaskRect all_pixels{0, 0, kEgoMotionFrameWidth, kEgoMotionFrameHeight};
    const auto low_background = EgoMotionObserver::estimate_pair(
        view(30, previous_pixels, &all_pixels, 1),
        view(31, current_pixels, &all_pixels, 1));
    assert(low_background.available);
    assert(!low_background.valid);
    assert(low_background.invalid_reason == EgoMotionInvalidReason::LowBackgroundCoverage);

    const auto duplicate = EgoMotionObserver::estimate_pair(
        view(40, previous_pixels), view(40, current_pixels));
    assert(!duplicate.available);
    assert(duplicate.invalid_reason == EgoMotionInvalidReason::DuplicateOrOutOfOrder);
}

void test_latest_only_worker() {
    EgoMotionObserver observer(EgoMotionObserverConfig{8, 1, 8, 6, 10, 0.20f, 0.18f, 3.5f, 20});
    const auto frame_pixels = make_frame();
    const auto shifted_pixels = make_frame(2, 1);
    assert(observer.submit_frame(view(100, frame_pixels)));
    assert(observer.submit_frame(view(101, shifted_pixels)));
    assert(observer.submit_frame(view(102, frame_pixels)));
    assert(observer.submit_frame(view(103, shifted_pixels)));
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    EgoMotionShadowResult result;
    bool got_result = false;
    while (observer.take_latest_result(&result)) got_result = true;
    assert(got_result);
    assert(result.result_sequence != 0);
    assert(result.current_frame_id >= 101 && result.current_frame_id <= 103);
    assert(result.compute_ms >= 0.0f);
    assert(observer.submit_frame(view(103, shifted_pixels)));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EgoMotionShadowResult duplicate_result;
    assert(!observer.take_latest_result(&duplicate_result));

    // A late frame must not become the next pair's predecessor. The next
    // accepted source frame should still pair with the last valid frame.
    assert(observer.submit_frame(view(102, frame_pixels)));
    assert(observer.submit_frame(view(104, frame_pixels)));
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EgoMotionShadowResult recovered;
    assert(observer.take_latest_result(&recovered));
    assert(recovered.previous_frame_id == 103);
    assert(recovered.current_frame_id == 104);
}

}  // namespace

int main() {
    test_deterministic_pairs();
    test_latest_only_worker();
    std::cout << "EgoMotionObserverTests passed\n";
    return 0;
}
