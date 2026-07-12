#include "ads_visual_transition.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, int line) {
    if (!value) { std::cerr << "require failed at line " << line << '\n'; std::abort(); }
}
void require_near(float actual, float expected, float tolerance, int line) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "near failed at line " << line << " actual=" << actual << '\n';
        std::abort();
    }
}
#define REQUIRE(value) require((value), __LINE__)
#define REQUIRE_NEAR(a,b,t) require_near((a),(b),(t),__LINE__)

runtime_app::AdsVisualFrame box(std::uint64_t id, float scale, float residual = 0.0f) {
    runtime_app::AdsVisualFrame value;
    value.frame_id = id;
    value.captured_at_ns = id * 10'000'000;
    value.target_track_id = 7;
    value.identity_quality = runtime_app::TargetIdentityQuality::StrongGeometricMatch;
    value.live = true;
    value.x1 = 320.0f - 50.0f * scale;
    value.x2 = 320.0f + 50.0f * scale;
    value.y1 = 256.0f - 100.0f * scale;
    value.y2 = 256.0f + 100.0f * scale;
    value.target_x = 340.0f;
    value.target_y = 230.0f;
    value.motion_residual_px = residual;
    return value;
}

void test_clean_zoom_settles_from_visual_frames() {
    runtime_app::AdsVisualTransitionEstimator estimator({3, 0.011f, 0.25f, 0.5f});
    estimator.start(box(1, 1.0f));
    runtime_app::AdsVisualEvidence last;
    for (float scale : {1.10f, 1.25f, 1.39f, 1.40f, 1.40f, 1.40f})
        last = estimator.observe(box(last.frame_id + 2, scale));
    REQUIRE(last.settled);
    REQUIRE_NEAR(last.scale_x, 1.40f, 0.01f);
    REQUIRE_NEAR(last.scale_y, 1.40f, 0.01f);
    REQUIRE(last.settle_confidence >= 1.0f);
}

void test_timer_ticks_without_new_frames_never_settle() {
    runtime_app::AdsVisualTransitionEstimator estimator({3, 0.011f, 0.25f, 0.5f});
    estimator.start(box(1, 1.0f));
    for (int i = 0; i < 20; ++i) estimator.observe_timer_only(10'000'000 + i * 10'000'000);
    REQUIRE(!estimator.latest().settled);
}

void test_motion_residual_blocks_clean_settle() {
    runtime_app::AdsVisualTransitionEstimator estimator({3, 0.011f, 0.25f, 0.5f});
    estimator.start(box(1, 1.0f));
    runtime_app::AdsVisualEvidence last;
    for (std::uint64_t id = 2; id <= 6; ++id) last = estimator.observe(box(id, 1.4f, 3.0f));
    REQUIRE(!last.settled);
    REQUIRE(!last.clean_calibration);
}

void test_repeated_frame_is_ignored() {
    runtime_app::AdsVisualTransitionEstimator estimator({2, 0.011f, 0.25f, 0.5f});
    estimator.start(box(1, 1.0f));
    const auto first = estimator.observe(box(2, 1.4f));
    const auto repeated = estimator.observe(box(2, 1.4f));
    REQUIRE(first.accepted_new_frame);
    REQUIRE(!repeated.accepted_new_frame);
    REQUIRE(!repeated.settled);
}
}

int main() {
    test_clean_zoom_settles_from_visual_frames();
    test_timer_ticks_without_new_frames_never_settle();
    test_motion_residual_blocks_clean_settle();
    test_repeated_frame_is_ignored();
    return 0;
}
