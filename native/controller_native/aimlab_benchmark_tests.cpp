#include "aimlab_benchmark.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_true(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << "\n";
        std::exit(1);
    }
}

void test_wrong_strong_lock_reduces_selection_score() {
    controller_native::aimlab::ScoreAggregator scorer;
    controller_native::aimlab::FrameScoreInput frame;
    frame.intended_target_id = 1;
    frame.selected_target_id = 2;
    frame.has_selected_target = true;
    frame.strong_snap_active = true;
    frame.aim_error_before_px = {80.0f, 0.0f};
    frame.aim_error_after_px = {95.0f, 0.0f};
    frame.controller_output = {1.0f, 0.0f};
    frame.user_input = {-1.0f, 0.0f};
    frame.dt_seconds = 1.0f / 120.0f;

    scorer.add_frame(frame);
    const auto report = scorer.report();

    expect_true(report.frames == 1, "frame count should be recorded");
    expect_true(report.wrong_target_ads_snap_count == 1, "wrong strong ADS snap should be counted");
    expect_true(report.user_fight_frames == 1, "controller/user fighting should be counted");
    expect_true(report.helpful_output_ratio < 0.01, "wrong output should not be helpful");
    expect_true(report.selection_score < 50.0, "wrong strong lock should reduce selection score");
    expect_true(report.final_score < 70.0, "hard penalties should lower final score");
}

void test_helpful_output_increases_cooperation_score() {
    controller_native::aimlab::ScoreAggregator scorer;
    controller_native::aimlab::FrameScoreInput frame;
    frame.intended_target_id = 3;
    frame.selected_target_id = 3;
    frame.has_selected_target = true;
    frame.strong_snap_active = true;
    frame.aim_error_before_px = {80.0f, 0.0f};
    frame.aim_error_after_px = {35.0f, 0.0f};
    frame.controller_output = {-0.6f, 0.0f};
    frame.user_input = {-0.5f, 0.0f};
    frame.dt_seconds = 1.0f / 120.0f;

    scorer.add_frame(frame);
    const auto report = scorer.report();

    expect_true(report.wrong_target_ads_snap_count == 0, "correct target should not count wrong snap");
    expect_near(report.helpful_output_ratio, 1.0, 0.001, "helpful ratio should be one");
    expect_true(report.cooperation_score > 90.0, "helpful output should score high");
}

}  // namespace

int main() {
    test_wrong_strong_lock_reduces_selection_score();
    test_helpful_output_increases_cooperation_score();
    std::cout << "cod_native_aimlab_benchmark_tests PASS\n";
    return 0;
}
