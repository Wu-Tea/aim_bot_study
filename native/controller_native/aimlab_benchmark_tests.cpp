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

void test_near_side_vs_far_front_penalizes_far_wrong_target() {
    const auto report = controller_native::aimlab::run_scenario("near_side_vs_far_front", 12345);

    expect_true(report.frames > 30, "scenario should run multiple frames");
    expect_true(report.wrong_target_ads_snap_count > 0, "baseline scenario should expose wrong strong snap");
    expect_true(report.selection_score < 80.0, "wrong far target should reduce selection score");
}

void test_real_selector_intent_improves_near_side_vs_far_front() {
    const auto baseline = controller_native::aimlab::run_scenario(
        "near_side_vs_far_front_no_intent",
        12345);
    const auto intent = controller_native::aimlab::run_scenario(
        "near_side_vs_far_front_intent",
        12345);

    expect_true(baseline.frames > 30, "selector baseline scenario should run multiple frames");
    expect_true(intent.frames == baseline.frames, "paired selector scenarios should use same frame count");
    expect_true(
        baseline.wrong_target_ads_snap_count > 0,
        "selector baseline should expose wrong high-confidence target lock");
    expect_true(
        intent.wrong_target_ads_snap_count == 0,
        "intent-aware selector should avoid wrong high-confidence target lock");
    expect_true(
        intent.time_on_intended_target_ratio > 0.90,
        "intent-aware selector should stay on intended target");
    expect_true(
        intent.final_score > baseline.final_score + 50.0,
        "intent-aware selector scenario should score materially better than baseline");
}

void test_unknown_scenario_fails_closed() {
    const auto report = controller_native::aimlab::run_scenario(
        "not_a_real_scenario",
        12345);

    expect_true(report.frames == 0, "unknown scenario should not synthesize passing frames");
    expect_near(report.final_score, 0.0, 0.001, "unknown scenario should fail closed");
}

void test_manual_input_model_slow_profile_has_delay_and_ramp() {
    controller_native::aimlab::ManualInputModel model(
        controller_native::aimlab::ManualInputProfile::Slow,
        12345);

    controller_native::aimlab::ManualInputFrame frame;
    frame.frame_index = 1;
    frame.timestamp_seconds = 0.05;
    frame.reticle_px = {320.0f, 256.0f};
    frame.target_px = {250.0f, 310.0f};
    const auto early = model.update(frame);

    expect_true(!early.reaction_ready, "slow profile should wait before reacting");
    expect_true(!early.intent.valid, "slow profile should not expose intent before reaction delay");
    expect_near(controller_native::aimlab::vector_length(early.manual_stick), 0.0, 0.001, "early stick should be idle");

    frame.frame_index = 24;
    frame.timestamp_seconds = 0.20;
    const auto ramping = model.update(frame);

    expect_true(ramping.reaction_ready, "slow profile should eventually react");
    expect_true(ramping.intent.valid, "slow profile should expose intent after reaction delay");
    expect_true(
        controller_native::aimlab::vector_length(ramping.manual_stick) > 0.05,
        "slow profile should start moving after delay");
    expect_true(
        controller_native::aimlab::vector_length(ramping.manual_stick) < 0.40,
        "slow profile should ramp in rather than jump to full input");
}

void test_manual_input_model_noisy_profile_detects_reverse_correction() {
    controller_native::aimlab::ManualInputModel model(
        controller_native::aimlab::ManualInputProfile::NoisyRecover,
        7);

    controller_native::aimlab::ManualInputFrame frame;
    frame.frame_index = 12;
    frame.timestamp_seconds = 0.12;
    frame.reticle_px = {280.0f, 310.0f};
    frame.target_px = {250.0f, 310.0f};
    const auto first = model.update(frame);
    expect_true(first.manual_stick.x < -0.05f, "first correction should pull left toward target");

    frame.frame_index = 24;
    frame.timestamp_seconds = 0.24;
    frame.reticle_px = {245.0f, 310.0f};
    const auto recovered = model.update(frame);

    expect_true(recovered.reverse_correction, "noisy profile should flag overshoot reverse correction");
    expect_true(recovered.manual_stick.x > 0.05f, "reverse correction should pull back right");
}

void test_manual_input_model_is_deterministic_for_seed() {
    controller_native::aimlab::ManualInputModel lhs(
        controller_native::aimlab::ManualInputProfile::Clean,
        99);
    controller_native::aimlab::ManualInputModel rhs(
        controller_native::aimlab::ManualInputProfile::Clean,
        99);

    for (int index = 0; index < 8; ++index) {
        controller_native::aimlab::ManualInputFrame frame;
        frame.frame_index = static_cast<std::uint64_t>(index + 1);
        frame.timestamp_seconds = 0.02 * static_cast<double>(index + 1);
        frame.reticle_px = {320.0f + static_cast<float>(index), 256.0f};
        frame.target_px = {250.0f, 310.0f};

        const auto left = lhs.update(frame);
        const auto right = rhs.update(frame);
        expect_near(left.manual_stick.x, right.manual_stick.x, 0.0001, "manual x should be deterministic");
        expect_near(left.manual_stick.y, right.manual_stick.y, 0.0001, "manual y should be deterministic");
        expect_true(left.intent.valid == right.intent.valid, "intent validity should be deterministic");
        expect_near(left.intent.strength, right.intent.strength, 0.0001, "intent strength should be deterministic");
    }
}

}  // namespace

int main() {
    test_wrong_strong_lock_reduces_selection_score();
    test_helpful_output_increases_cooperation_score();
    test_near_side_vs_far_front_penalizes_far_wrong_target();
    test_real_selector_intent_improves_near_side_vs_far_front();
    test_unknown_scenario_fails_closed();
    test_manual_input_model_slow_profile_has_delay_and_ramp();
    test_manual_input_model_noisy_profile_detects_reverse_correction();
    test_manual_input_model_is_deterministic_for_seed();
    std::cout << "cod_native_aimlab_benchmark_tests PASS\n";
    return 0;
}
