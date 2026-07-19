#include "vector_intent_fuser.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using controller_native::FusionCandidate;
using controller_native::VectorIntentFuser;
using controller_native::VectorIntentFusionInput;

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

pipeline_contract::TargetPlan observed_plan() {
    pipeline_contract::TargetPlan plan;
    plan.target_id = 7;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    plan.error_px = {40.0f, 0.0f};
    plan.error_rate_px_per_sec = {0.0f, 0.0f};
    plan.reliability = 1.0f;
    plan.response_scale = 500.0f;
    plan.response_confidence = 1.0f;
    return plan;
}

VectorIntentFusionInput input_for(
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f ai) {
    VectorIntentFusionInput input;
    input.manual_stick = manual;
    input.shaped_ai_stick = ai;
    input.plan = observed_plan();
    return input;
}

void set_horizon(
    pipeline_contract::TargetPlan& plan,
    std::initializer_list<std::pair<float, pipeline_contract::Vec2f>> samples) {
    plan.horizon_count = static_cast<std::uint32_t>(samples.size());
    std::size_t index = 0;
    for (const auto& sample : samples) {
        plan.horizon[index].time_seconds = sample.first;
        plan.horizon[index].error_px = sample.second;
        ++index;
    }
}

void test_candidate_outputs_match_version_one_scales() {
    using controller_native::candidate_weights;
    const auto existing = candidate_weights(FusionCandidate::ExistingMix);
    const auto manual_supported = candidate_weights(
        FusionCandidate::ManualSupported);
    const auto ai_supported = candidate_weights(FusionCandidate::AiSupported);
    const auto manual = candidate_weights(FusionCandidate::ManualOnly);
    const auto ai = candidate_weights(FusionCandidate::AiOnly);
    const auto reduced = candidate_weights(FusionCandidate::ReducedMix);

    require_near(existing.manual, 1.0f, 0.0001f,
                 "existing mix must retain full manual input");
    require_near(existing.ai, 1.0f, 0.0001f,
                 "existing mix must retain full AI input");
    require_near(manual_supported.manual, 1.0f, 0.0001f,
                 "manual-supported candidate must retain manual input");
    require_near(manual_supported.ai, 0.5f, 0.0001f,
                 "manual-supported candidate must halve AI input");
    require_near(ai_supported.manual, 0.5f, 0.0001f,
                 "AI-supported candidate must halve manual input");
    require_near(ai_supported.ai, 1.0f, 0.0001f,
                 "AI-supported candidate must retain AI input");
    require_near(manual.manual, 1.0f, 0.0001f,
                 "manual-only candidate must retain manual input");
    require_near(manual.ai, 0.0f, 0.0001f,
                 "manual-only candidate must remove AI input");
    require_near(ai.manual, 0.0f, 0.0001f,
                 "AI-only candidate must remove manual input");
    require_near(ai.ai, 1.0f, 0.0001f,
                 "AI-only candidate must retain AI input");
    require_near(reduced.manual, 0.5f, 0.0001f,
                 "reduced mix must halve manual input");
    require_near(reduced.ai, 0.5f, 0.0001f,
                 "reduced mix must halve AI input");
}

void test_aligned_input_keeps_existing_mix() {
    VectorIntentFuser fuser;
    const auto decision = fuser.update(
        input_for({0.20f, 0.0f}, {0.20f, 0.0f}), 0.001f);
    require_true(decision.candidate == FusionCandidate::ExistingMix,
                 "aligned user and AI input must keep the existing mix");
}

void test_opposing_small_manual_can_choose_ai_supported() {
    VectorIntentFuser fuser;
    const auto decision = fuser.update(
        input_for({-0.20f, 0.0f}, {0.30f, 0.0f}), 0.001f);
    require_true(decision.candidate == FusionCandidate::AiSupported,
                 "small opposing manual input must allow AI-supported fusion");
}

void test_orthogonal_manual_is_not_reduced_by_axis_projection() {
    VectorIntentFuser fuser;
    const auto decision = fuser.update(
        input_for({0.0f, 0.30f}, {0.30f, 0.0f}), 0.001f);
    require_near(decision.target_manual_weight, 1.0f, 0.0001f,
                 "orthogonal manual tracking must remain fully represented");
}

void test_short_local_gain_loses_to_lower_160ms_burden() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.0f}, {0.10f, 0.0f});
    set_horizon(input.plan, {
        {0.040f, {10.0f, 0.0f}},
        {0.080f, {15.0f, 0.0f}},
        {0.160f, {30.0f, 0.0f}},
    });

    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.candidate == FusionCandidate::ManualSupported,
                 "40ms winner must yield to the lower multi-horizon burden");
}

void test_left_motion_adjusted_error_rate_changes_winner() {
    VectorIntentFuser moving_away;
    auto away = input_for({0.25f, 0.0f}, {-0.25f, 0.0f});
    away.plan.error_px = {10.0f, 0.0f};
    away.plan.error_rate_px_per_sec = {200.0f, 0.0f};

    VectorIntentFuser closing;
    auto toward = away;
    toward.plan.error_rate_px_per_sec = {-200.0f, 0.0f};

    const auto away_decision = moving_away.update(away, 0.001f);
    const auto toward_decision = closing.update(toward, 0.001f);
    require_true(away_decision.candidate != toward_decision.candidate,
                 "the plan's left-motion-adjusted error rate must affect selection");
}

void test_center_cross_continued_push_is_penalized() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.0f}, {0.30f, 0.0f});
    input.plan.error_px = {3.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {3.0f, 0.0f}},
        {0.080f, {2.0f, 0.0f}},
        {0.160f, {1.0f, 0.0f}},
    });

    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.candidate != FusionCandidate::ExistingMix,
                 "a mix that keeps pushing after a crossing must be penalized");
}

void test_near_equal_cost_keeps_previous_candidate() {
    VectorIntentFuser fuser;
    const auto first = fuser.update(
        input_for({-0.20f, 0.0f}, {0.30f, 0.0f}), 0.001f);
    require_true(first.candidate == FusionCandidate::AiSupported,
                 "fixture must establish an AI-supported previous choice");

    auto near_equal = input_for({0.01f, 0.0f}, {0.30f, 0.0f});
    near_equal.plan.error_px = {40.0f, 0.0f};
    const auto second = fuser.update(near_equal, 0.001f);
    require_true(second.candidate == FusionCandidate::AiSupported,
                 "near-equal cost must keep the previous candidate");
}

}  // namespace

int main() {
    try {
        test_candidate_outputs_match_version_one_scales();
        test_aligned_input_keeps_existing_mix();
        test_opposing_small_manual_can_choose_ai_supported();
        test_orthogonal_manual_is_not_reduced_by_axis_projection();
        test_short_local_gain_loses_to_lower_160ms_burden();
        test_left_motion_adjusted_error_rate_changes_winner();
        test_center_cross_continued_push_is_penalized();
        test_near_equal_cost_keeps_previous_candidate();
        std::cout << "cod_native_vector_intent_fuser_tests PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_vector_intent_fuser_tests FAIL "
                  << error.what() << '\n';
        return 1;
    }
}
