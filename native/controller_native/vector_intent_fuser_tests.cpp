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

}  // namespace

int main() {
    try {
        test_candidate_outputs_match_version_one_scales();
        test_aligned_input_keeps_existing_mix();
        test_opposing_small_manual_can_choose_ai_supported();
        test_orthogonal_manual_is_not_reduced_by_axis_projection();
        std::cout << "cod_native_vector_intent_fuser_tests PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_vector_intent_fuser_tests FAIL "
                  << error.what() << '\n';
        return 1;
    }
}
