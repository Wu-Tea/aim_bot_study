#include "vision_native/ego_motion_observer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <thread>
#include <utility>

namespace {

using namespace vision_native;

void require_red(bool value, const char* expression) {
    if (!value) {
        std::cerr << "RED require failed: " << expression << '\n';
        std::exit(17);
    }
}

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
    EgoMotionFrameView value;
    value.frame_id = id;
    value.source_present_qpc = id * 100;
    value.source_present_qpc_frequency = 1'000'000;
    value.source_present_steady_ns = id * 1'000'000;
    value.source_present_calibration_id = 1;
    value.source_present_calibration_uncertainty_ns = 25;
    value.source_present_steady_available = true;
    value.captured_at_ns = id * 1'000;
    value.result_at_ns = id * 1'100;
    value.width = kEgoMotionFrameWidth;
    value.height = kEgoMotionFrameHeight;
    value.row_pitch = kEgoMotionFrameWidth;
    value.gray = pixels.data();
    value.masks = masks;
    value.mask_count = mask_count;
    return value;
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

void test_displacement_beyond_current_search_radius_is_identified() {
    const auto previous_pixels = make_frame();
    const auto current_pixels = make_frame(12, -11);
    EgoMotionObserverConfig reference_config;
    reference_config.search_radius_px = 8;
    const auto reference = EgoMotionObserver::estimate_pair(
        view(50, previous_pixels), view(51, current_pixels), reference_config);

    std::cerr << "radius=8 reference valid=" << reference.valid
              << " dx=" << reference.background_dx
              << " dy=" << reference.background_dy
              << " boundary_hits=" << reference.boundary_hit_count
              << " boundary_rate=" << reference.boundary_hit_rate
              << " consistent_rate=" << reference.boundary_consistent_hit_rate
              << '\n';
    require_red(!reference.valid, "radius=8 reports boundary-limited input");
    require_red(reference.invalid_reason ==
        EgoMotionInvalidReason::SearchBoundaryLimited,
        "radius=8 reason is SearchBoundaryLimited");
    require_red(reference.boundary_hit_count > 0,
        "radius=8 records boundary diagnostics");
    require_red(reference.boundary_consistent_hit_rate >= 0.50f,
        "radius=8 has same-direction boundary evidence");

    const auto estimate = EgoMotionObserver::estimate_pair(
        view(60, previous_pixels), view(61, current_pixels));

    std::cerr << "radius=14 beyond-radius estimate valid=" << estimate.valid
              << " dx=" << estimate.background_dx
              << " dy=" << estimate.background_dy << '\n';
    require_red(estimate.valid, "radius=14 estimate.valid");
    require_red(std::abs(estimate.background_dx - 12.0f) <= 1.0f,
                "background_dx identifies +12 px");
    require_red(std::abs(estimate.background_dy + 11.0f) <= 1.0f,
                "background_dy identifies -11 px");

    // Release-effective coverage for the full +/-12 boundary in both axes,
    // including the diagonal signs that can otherwise hide a mixed-axis
    // clipping error.
    const std::array<std::pair<int, int>, 8> edge_cases{{
        {12, 0}, {-12, 0}, {0, 12}, {0, -12},
        {12, 12}, {-12, -12}, {12, -12}, {-12, 12}}};
    std::uint64_t frame_id = 100;
    for (const auto [dx, dy] : edge_cases) {
        const auto edge_current_pixels = make_frame(dx, dy);
        const auto edge_previous = view(frame_id, previous_pixels);
        const auto edge_current = view(frame_id + 1, edge_current_pixels);
        const auto edge_estimate = EgoMotionObserver::estimate_pair(
            edge_previous, edge_current);
        frame_id += 2;
        require_red(edge_estimate.valid,
                    "radius=14 +/-12 edge case remains valid");
        require_red(std::abs(edge_estimate.background_dx -
                                 static_cast<float>(dx)) <= 1.0f,
                    "radius=14 edge X displacement is accurate");
        require_red(std::abs(edge_estimate.background_dy -
                                 static_cast<float>(dy)) <= 1.0f,
                    "radius=14 edge Y displacement is accurate");
    }
}

void test_local_boundary_hits_do_not_invalidate_global_estimate() {
    const auto previous_pixels = make_frame();
    auto current_pixels = make_frame(2, 1);
    for (int y = 36; y < 60; ++y) {
        for (int x = 34; x < 58; ++x) {
            current_pixels[static_cast<std::size_t>(y) * kEgoMotionFrameWidth + x] =
                static_cast<std::uint8_t>((x * 73 + y * 41 + 19) & 0xff);
        }
    }
    const auto estimate = EgoMotionObserver::estimate_pair(
        view(70, previous_pixels), view(71, current_pixels));
    std::cerr << "local-boundary estimate valid=" << estimate.valid
              << " dx=" << estimate.background_dx
              << " dy=" << estimate.background_dy
              << " boundary_hits=" << estimate.boundary_hit_count
              << " boundary_rate=" << estimate.boundary_hit_rate
              << " consistent_rate=" << estimate.boundary_consistent_hit_rate
              << '\n';
    require_red(estimate.valid, "local boundary hits preserve global validity");
    require_red(std::abs(estimate.background_dx - 2.0f) <= 1.0f,
                "local boundary hits preserve global dx");
    require_red(std::abs(estimate.background_dy - 1.0f) <= 1.0f,
                "local boundary hits preserve global dy");
    require_red(estimate.boundary_hit_count > 0,
                "local boundary hits are diagnosed");
    require_red(estimate.boundary_consistent_hit_rate < 0.50f,
                "local boundary hits are not global same-direction evidence");
}

void test_latest_only_worker() {
    EgoMotionObserver observer(EgoMotionObserverConfig{8, 1, 8, 6, 10, 0.20f, 0.18f, 3.5f, 20});
    const auto frame_pixels = make_frame();
    const auto shifted_pixels = make_frame(2, 1);
    require_red(observer.submit_frame(view(100, frame_pixels)),
                "submit frame 100");
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    require_red(observer.submit_frame(view(101, shifted_pixels)),
                "submit frame 101");
    require_red(observer.submit_frame(view(102, frame_pixels)),
                "submit frame 102");
    require_red(observer.submit_frame(view(103, shifted_pixels)),
                "submit frame 103");
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    EgoMotionShadowResult result;
    bool got_result = false;
    while (observer.take_latest_result(&result)) got_result = true;
    require_red(got_result, "take first latest result");
    assert(result.result_sequence != 0);
    assert(result.current_frame_id >= 101 && result.current_frame_id <= 103);
    assert(result.compute_ms >= 0.0f);
    require_red(observer.submit_frame(view(103, shifted_pixels)),
                "submit duplicate frame 103");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EgoMotionShadowResult duplicate_result;
    require_red(!observer.take_latest_result(&duplicate_result),
                "duplicate frame has no result");

    // A late frame must not become the next pair's predecessor. The next
    // accepted source frame should still pair with the last valid frame.
    require_red(observer.submit_frame(view(102, frame_pixels)),
                "submit late frame 102");
    require_red(observer.submit_frame(view(104, frame_pixels)),
                "submit recovery frame 104");
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EgoMotionShadowResult recovered;
    require_red(observer.take_latest_result(&recovered),
                "take recovered result");
    require_red(recovered.previous_frame_id == 103,
                "late frame does not replace previous baseline");
    require_red(recovered.current_frame_id == 104,
                "recovery frame forms next pair");

    // Leave one result unread, then produce a newer pair. The mailbox must
    // report replacement rather than growing a hidden result queue.
    require_red(observer.submit_frame(view(105, shifted_pixels)),
                "submit unread-result predecessor");
    std::this_thread::sleep_for(std::chrono::milliseconds(55));
    require_red(observer.submit_frame(view(106, frame_pixels)),
                "submit unread-result replacement");
    std::this_thread::sleep_for(std::chrono::milliseconds(55));
    EgoMotionShadowResult newest;
    require_red(observer.take_latest_result(&newest),
                "take newest result after unread replacement");
    require_red(newest.current_frame_id == 106,
                "newer result replaces unread result");
    require_red(newest.observer_completed_at_ns != 0,
                "observer completion timestamp is distinct and present");
    require_red(newest.result_age_at_take_ns <= 1'000'000'000ull,
                "result age is bounded from observer completion");

    const auto stats = observer.stats();
    require_red(stats.frames_submitted >= 7, "frames_submitted is monotonic");
    require_red(stats.pending_frame_replaced >= 1,
                "pending_frame_replaced is observable");
    require_red(stats.pairs_processed >= 1,
                "pairs_processed is observable");
    require_red(stats.duplicate_or_out_of_order_rejected >= 1,
                "duplicate_or_out_of_order_rejected is observable");
    require_red(stats.results_taken >= 2, "results_taken is observable");
    require_red(stats.unread_result_replaced >= 1,
                "unread_result_replaced is observable");
}

}  // namespace

int main() {
    test_deterministic_pairs();
    test_displacement_beyond_current_search_radius_is_identified();
    test_local_boundary_hits_do_not_invalidate_global_estimate();
    test_latest_only_worker();
    std::cout << "EgoMotionObserverTests passed\n";
    return 0;
}
