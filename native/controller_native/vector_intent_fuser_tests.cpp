#include "vector_intent_fuser.h"

#include <cmath>
#include <iostream>
#include <limits>
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

void test_candidate_count_covers_polar_set() {
    require_true(controller_native::kFusionCandidateCount ==
                     static_cast<std::size_t>(FusionCandidate::TangentialReplaced) + 1,
                 "candidate statistics must cover every version-three polar candidate");
}

void test_aligned_input_keeps_existing_mix() {
    VectorIntentFuser fuser;
    const auto decision = fuser.update(
        input_for({0.20f, 0.0f}, {0.20f, 0.0f}), 0.001f);
    require_true(decision.candidate == FusionCandidate::ExistingMix,
                 "aligned user and AI input must keep the existing mix");
}

void test_opposing_small_manual_uses_partial_radial_brake() {
    VectorIntentFuser fuser;
    const auto decision = fuser.update(
        input_for({-0.20f, 0.0f}, {0.30f, 0.0f}), 0.001f);
    require_near(decision.target_manual_weight, 0.5f, 0.0001f,
                 "current-only conflict must preserve half of deliberate radial input");
    require_near(decision.target_ai_weight, 1.0f, 0.0001f,
                 "current-only conflict must retain the shaped AI proposal");
}

void test_wrong_way_manual_only_is_ineligible_below_escape() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.30f, 0.0f}, {0.30f, 0.0f});
    input.manual_confidence = 0.9f;
    input.plan.error_px = {40.0f, 0.0f};
    const auto decision = fuser.update(input, 0.001f);
    for (const auto candidate : {
             FusionCandidate::ExistingMix,
             FusionCandidate::ManualSupported,
             FusionCandidate::ManualOnly,
             FusionCandidate::ReducedMix}) {
        require_true(std::isinf(decision.candidate_costs[
                         static_cast<std::size_t>(candidate)]),
                     "wrong-way sub-escape input must not retain or symmetrically weaken radial conflict");
    }
}

void test_predicted_ads_reversal_releases_excess_radial_manual_ownership() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.15f}, {-0.30f, 0.0f});
    input.manual_confidence = 0.9f;
    input.plan.error_px = {10.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {-4.0f, 0.0f}},
        {0.080f, {-18.0f, 0.0f}},
        {0.160f, {-42.0f, 0.0f}},
    });
    const auto decision = fuser.update(input, 0.001f);
    require_true(std::isinf(decision.candidate_costs[
                     static_cast<std::size_t>(FusionCandidate::ManualOnly)]),
                 "ADS plan reversal must release sticky full radial manual ownership");
    require_true(std::isfinite(decision.candidate_costs[
                     static_cast<std::size_t>(FusionCandidate::RadialCorrected)]),
                 "ADS plan reversal must offer a partial radial brake that preserves tangent");
    require_true(std::isinf(decision.candidate_costs[
                     static_cast<std::size_t>(FusionCandidate::AiOnly)]),
                 "radial brake must not silently become whole-vector AI ownership");
    require_near(decision.target_manual_weight, 0.5f, 0.0001f,
                 "predicted ADS reversal must reduce rather than swallow radial manual");
}

void test_deliberate_opposing_manual_is_not_fully_swallowed() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.35f, 0.0f}, {0.30f, 0.0f});
    input.manual_confidence = 0.9f;
    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.target_manual_weight >= 0.5f,
                 "deliberate manual conflict must retain at least half ownership");
    require_true(decision.candidate != FusionCandidate::AiOnly,
                 "AI-only is reserved for low-confidence manual noise");
}

void test_high_confidence_manual_never_selects_ai_only() {
    for (const float manual : {-0.05f, -0.15f, -0.25f, -0.35f, -0.44f}) {
        for (const float ai : {0.10f, 0.30f, 0.50f}) {
            for (const float error : {5.0f, 20.0f, 40.0f}) {
                VectorIntentFuser fuser;
                auto input = input_for({manual, 0.0f}, {ai, 0.0f});
                input.manual_confidence = 0.9f;
                input.plan.error_px = {error, 0.0f};
                const auto decision = fuser.update(input, 0.024f);
                require_true(decision.candidate != FusionCandidate::AiOnly,
                             "high-confidence manual grid must never select AI-only");
            }
        }
    }
}

void test_orthogonal_manual_is_not_reduced_by_axis_projection() {
    VectorIntentFuser fuser;
    const auto decision = fuser.update(
        input_for({0.0f, 0.30f}, {0.30f, 0.0f}), 0.001f);
    require_near(decision.target_manual_weight, 1.0f, 0.0001f,
                 "orthogonal manual tracking must remain fully represented");
}

void test_radial_correction_preserves_helpful_tangential_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.20f, 0.30f}, {0.30f, 0.0f});
    input.manual_confidence = 0.9f;
    input.plan.error_px = {20.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {20.0f, -6.0f}},
        {0.080f, {25.0f, -12.0f}},
        {0.160f, {35.0f, -24.0f}},
    });

    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.candidate == FusionCandidate::RadialCorrected,
                 "wrong radial manual must be reduced without losing helpful tangent");
    require_near(decision.target_manual_weight, 0.5f, 0.0001f,
                 "radial correction must halve only radial manual ownership");
    require_near(decision.target_tangential_manual_weight, 1.0f, 0.0001f,
                 "radial correction must preserve tangential manual ownership");
    require_near(decision.fused_stick.y, 0.30f, 0.0001f,
                 "helpful tangential manual output must survive radial correction");
}

void test_radial_replacement_can_remove_only_wrong_radial_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.18f, 0.25f}, {0.22f, 0.0f});
    input.manual_confidence = 0.30f;
    input.plan.error_px = {8.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {12.0f, -5.0f}},
        {0.080f, {18.0f, -10.0f}},
        {0.160f, {24.0f, -20.0f}},
    });

    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.candidate == FusionCandidate::RadialReplaced,
                 "low-confidence wrong radial manual may be replaced independently");
    require_near(decision.target_manual_weight, 0.0f, 0.0001f,
                 "radial replacement must remove the wrong radial component");
    require_near(decision.target_tangential_manual_weight, 1.0f, 0.0001f,
                 "radial replacement must retain the tangential component");
    require_near(decision.fused_stick.y, 0.25f, 0.0001f,
                 "radial replacement must not swallow tangential correction");
}

void test_tangential_correction_preserves_helpful_radial_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({0.20f, -0.35f}, {0.20f, 0.0f});
    input.manual_confidence = 0.7f;
    input.plan.error_px = {20.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {25.0f, 0.0f}},
        {0.080f, {30.0f, 0.0f}},
        {0.160f, {40.0f, 0.0f}},
    });

    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.candidate == FusionCandidate::TangentialCorrected,
                 "wrong tangent must be reduced without weakening helpful radial input");
    require_near(decision.target_manual_weight, 1.0f, 0.0001f,
                 "tangential correction must preserve radial manual ownership");
    require_near(decision.target_tangential_manual_weight, 0.5f, 0.0001f,
                 "tangential correction must halve only tangential ownership");
    require_near(decision.fused_stick.x, 0.40f, 0.0001f,
                 "helpful radial manual input must survive tangential correction");
}

void test_tangential_replacement_can_remove_only_wrong_tangent() {
    VectorIntentFuser fuser;
    auto input = input_for({0.18f, -0.25f}, {0.22f, 0.0f});
    input.manual_confidence = 0.30f;
    input.plan.error_px = {12.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {16.0f, 0.0f}},
        {0.080f, {22.0f, 0.0f}},
        {0.160f, {34.0f, 0.0f}},
    });

    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.candidate == FusionCandidate::TangentialReplaced,
                 "low-confidence wrong tangent may be replaced independently");
    require_near(decision.target_manual_weight, 1.0f, 0.0001f,
                 "tangential replacement must retain helpful radial manual input");
    require_near(decision.target_tangential_manual_weight, 0.0f, 0.0001f,
                 "tangential replacement must remove only the tangent");
}

void test_short_local_gain_loses_to_lower_160ms_burden() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.0f}, {-0.10f, 0.0f});
    set_horizon(input.plan, {
        {0.040f, {6.0f, 0.0f}},
        {0.080f, {12.0f, 0.0f}},
        {0.160f, {20.0f, 0.0f}},
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

void test_aligned_center_cross_is_delegated_to_existing_brake() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.0f}, {0.30f, 0.0f});
    input.plan.error_px = {3.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {3.0f, 0.0f}},
        {0.080f, {2.0f, 0.0f}},
        {0.160f, {1.0f, 0.0f}},
    });

    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.candidate == FusionCandidate::ExistingMix,
                 "aligned crossing must remain owned by ADS Brake or controller dynamics");
}

void test_bodylock_allows_crossing_while_target_keeps_inertial_direction() {
    auto continuing = input_for({-0.10f, 0.0f}, {0.30f, 0.0f});
    continuing.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    continuing.plan.error_px = {3.0f, 0.0f};
    continuing.plan.velocity_px_per_sec = {100.0f, 0.0f};
    continuing.plan.acceleration_px_per_sec2 = {-300.0f, 0.0f};
    set_horizon(continuing.plan, {
        {0.040f, {-1.0f, 0.0f}},
        {0.080f, {-3.0f, 0.0f}},
        {0.160f, {-6.0f, 0.0f}},
    });

    auto reversed = continuing;
    reversed.plan.acceleration_px_per_sec2 = {-1000.0f, 0.0f};

    VectorIntentFuser inertial_fuser;
    VectorIntentFuser reversed_fuser;
    const auto inertial = inertial_fuser.update(continuing, 0.001f);
    const auto after_reversal = reversed_fuser.update(reversed, 0.001f);
    require_true(inertial.candidate_costs[0] < after_reversal.candidate_costs[0],
                 "BodyLock must allow finite crossing until target velocity reverses");
}

void test_ads_crossing_penalty_does_not_inherit_bodylock_inertia_allowance() {
    auto continuing = input_for({-0.10f, 0.0f}, {0.30f, 0.0f});
    continuing.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    continuing.plan.error_px = {3.0f, 0.0f};
    continuing.plan.velocity_px_per_sec = {100.0f, 0.0f};
    continuing.plan.acceleration_px_per_sec2 = {-300.0f, 0.0f};
    set_horizon(continuing.plan, {
        {0.040f, {-1.0f, 0.0f}},
        {0.080f, {-3.0f, 0.0f}},
        {0.160f, {-6.0f, 0.0f}},
    });
    auto reversed = continuing;
    reversed.plan.acceleration_px_per_sec2 = {-1000.0f, 0.0f};

    VectorIntentFuser continuing_fuser;
    VectorIntentFuser reversed_fuser;
    const auto before_reversal = continuing_fuser.update(continuing, 0.001f);
    const auto after_reversal = reversed_fuser.update(reversed, 0.001f);
    require_near(before_reversal.candidate_costs[0],
                 after_reversal.candidate_costs[0], 0.0001f,
                 "ADS settle must keep its crossing brake independent of target inertia");
}

void test_near_equal_cost_keeps_previous_candidate() {
    VectorIntentFuser fuser;
    const auto first = fuser.update(
        input_for({-0.20f, 0.0f}, {0.30f, 0.0f}), 0.001f);
    require_true(first.candidate == FusionCandidate::AiSupported,
                 "fixture must establish an AI-supported previous choice");

    auto near_equal = input_for({-0.19f, 0.0f}, {0.30f, 0.0f});
    near_equal.plan.error_px = {40.0f, 0.0f};
    const auto second = fuser.update(near_equal, 0.001f);
    require_true(second.candidate == FusionCandidate::AiSupported,
                 "near-equal cost must keep the previous candidate");
}

void test_diagonal_manual_escape_is_preserved_exactly() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.50f, 0.40f}, {0.40f, -0.30f});
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.manual_escape,
                 "deliberate diagonal input must be classified as escape");
    require_true(decision.candidate == FusionCandidate::ManualOnly,
                 "manual escape must disallow AI-owned candidates");
    require_near(decision.fused_stick.x, input.manual_stick.x, 0.0001f,
                 "escape X must remain physical input");
    require_near(decision.fused_stick.y, input.manual_stick.y, 0.0001f,
                 "escape Y must remain physical input");
}

void test_missing_target_falls_back_to_physical_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({0.20f, -0.10f}, {0.30f, 0.20f});
    input.plan = {};
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.fallback, "missing target must use fallback");
    require_near(decision.fused_stick.x, 0.20f, 0.0001f,
                 "missing target must preserve manual X");
    require_near(decision.fused_stick.y, -0.10f, 0.0001f,
                 "missing target must preserve manual Y");
}

void test_target_change_releases_without_new_attenuation_step() {
    VectorIntentFuser fuser;
    const auto first = fuser.update(
        input_for({-0.20f, 0.0f}, {0.30f, 0.0f}), 0.024f);
    require_true(first.applied_manual_weight < 1.0f,
                 "fixture must begin with attenuated manual ownership");

    auto changed = input_for({-0.20f, 0.0f}, {0.30f, 0.0f});
    changed.plan.target_id = 8;
    const auto second = fuser.update(changed, 0.001f);
    require_true(second.fallback, "target change must suspend active selection");
    require_near(second.target_manual_weight, 1.0f, 0.0001f,
                 "target change must target full manual ownership");
    require_true(second.applied_manual_weight > first.applied_manual_weight,
                 "target change must immediately begin releasing manual attenuation");
}

void test_reacquiring_low_reliability_and_low_response_release_to_manual() {
    for (int kind = 0; kind < 3; ++kind) {
        VectorIntentFuser fuser;
        auto input = input_for({0.20f, 0.0f}, {0.30f, 0.0f});
        if (kind == 0) {
            input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
        } else if (kind == 1) {
            input.plan.reliability = 0.40f;
        } else {
            input.plan.response_confidence = 0.10f;
        }
        const auto decision = fuser.update(input, 0.001f);
        require_true(decision.fallback,
                     "unreliable causal evidence must use manual fallback");
        require_near(decision.target_manual_weight, 1.0f, 0.0001f,
                     "fallback must target full manual ownership");
        require_near(decision.target_ai_weight, 1.0f, 0.0001f,
                     "ambiguous evidence must preserve safe shaped AI fallback");
    }
}

void test_unreliable_opposing_proposal_yields_exactly_to_manual() {
    for (int kind = 0; kind < 3; ++kind) {
        VectorIntentFuser fuser;
        auto input = input_for({-0.20f, 0.0f}, {0.30f, 0.0f});
        // Magnitude is deliberate even if the intent-confidence estimator has
        // not caught up yet; this is the exact stale-confidence runtime case.
        input.manual_confidence = 0.1f;
        if (kind == 0) {
            input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
        } else if (kind == 1) {
            input.plan.reliability = 0.40f;
        } else {
            input.plan.response_confidence = 0.10f;
        }
        const auto decision = fuser.update(input, 0.001f);
        require_true(decision.fallback,
                     "unreliable opposing evidence must use fallback");
        require_true(decision.candidate == FusionCandidate::ManualOnly,
                     "unreliable opposing AI must yield controller ownership");
        require_near(decision.fused_stick.x, input.manual_stick.x, 0.0001f,
                     "unreliable opposing fallback must deliver exact manual X");
        require_near(decision.applied_ai_weight, 0.0f, 0.0001f,
                     "unreliable opposing fallback must not retain hidden AI force");
    }
}

void test_low_response_confidence_does_not_deadlock_safe_ai_fallback() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.30f, 0.0f});
    input.plan.response_confidence = 0.0f;
    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.fallback,
                 "low response confidence must abstain from candidate ownership");
    require_near(decision.applied_manual_weight, 1.0f, 0.0001f,
                 "low confidence must never attenuate manual input");
    require_near(decision.applied_ai_weight, 1.0f, 0.0001f,
                 "safe shaped AI must remain active for response learning");
    require_near(decision.fused_stick.x, 0.30f, 0.0001f,
                 "safe shaped AI fallback must reach delivered output");
}

void test_neutral_manual_input_cannot_create_a_second_ai_brake() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.30f, -0.10f});
    input.manual_confidence = 0.0f;
    input.plan.error_px = {3.0f, 1.0f};
    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.candidate == FusionCandidate::ExistingMix,
                 "without credible manual input there is no fusion conflict");
    require_near(decision.applied_ai_weight, 1.0f, 0.0001f,
                 "neutral manual input must not duplicate ADS or BodyLock braking");
    require_near(decision.fused_stick.x, 0.30f, 0.0001f,
                 "neutral manual input must preserve shaped AI X");
    require_near(decision.fused_stick.y, -0.10f, 0.0001f,
                 "neutral manual input must preserve shaped AI Y");
}

void test_plan_horizon_does_not_double_apply_previous_camera_output() {
    VectorIntentFuser fuser;
    auto input = input_for({0.10f, 0.0f}, {0.20f, 0.0f});
    const auto first = fuser.update(input, 0.024f);
    require_true(first.candidate == FusionCandidate::ExistingMix,
                 "fixture must establish the existing mixed output");

    set_horizon(input.plan, {
        {0.040f, {10.0f, 0.0f}},
        {0.080f, {5.0f, 0.0f}},
        {0.160f, {1.0f, 0.0f}},
    });
    const auto second = fuser.update(input, 0.001f);
    require_true(second.candidate == FusionCandidate::ExistingMix,
                 "causal horizon already containing camera motion must score only output delta");
}

void test_aligned_input_near_center_is_not_split_into_another_brake() {
    VectorIntentFuser fuser;
    auto input = input_for({0.20f, 0.0f}, {0.20f, 0.0f});
    input.plan.error_px = {3.0f, 0.0f};
    set_horizon(input.plan, {
        {0.040f, {2.0f, 0.0f}},
        {0.080f, {1.0f, 0.0f}},
        {0.160f, {0.5f, 0.0f}},
    });
    const auto decision = fuser.update(input, 0.024f);
    require_true(decision.candidate == FusionCandidate::ExistingMix,
                 "aligned user and AI input has no fusion conflict to arbitrate");
}

void test_nonfinite_input_returns_exact_physical_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({0.20f, -0.10f}, {0.30f, 0.20f});
    input.plan.error_px.x = std::numeric_limits<float>::quiet_NaN();
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.fallback, "non-finite plan must use fallback");
    require_near(decision.fused_stick.x, 0.20f, 0.0001f,
                 "non-finite fallback must preserve exact physical X");
    require_near(decision.fused_stick.y, -0.10f, 0.0001f,
                 "non-finite fallback must preserve exact physical Y");
}

void test_same_target_ads_to_bodylock_preserves_weight_state() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.20f, 0.0f}, {0.30f, 0.0f});
    const auto ads = fuser.update(input, 0.012f);
    input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    const auto bodylock = fuser.update(input, 0.0001f);
    require_true(!bodylock.fallback,
                 "same-target mode handoff must remain eligible");
    require_true(std::fabs(bodylock.applied_manual_weight -
                           ads.applied_manual_weight) < 0.01f,
                 "same-target mode handoff must not reset fusion weights");
}

void test_initial_existing_mix_does_not_ramp_safe_ai() {
    VectorIntentFuser fuser;
    const auto input = input_for({0.20f, 0.0f}, {0.20f, 0.0f});
    const auto first = fuser.update(input, 0.001f);
    require_near(first.target_ai_weight, 1.0f, 0.0001f,
                 "aligned fixture must target full AI weight");
    require_near(first.applied_ai_weight, 1.0f, 0.0001f,
                 "baseline existing mix must not pay an AI cold-start ramp");
}

void test_conflict_weights_approach_target_over_24ms() {
    VectorIntentFuser fuser;
    const auto conflict = input_for({-0.20f, 0.0f}, {0.30f, 0.0f});
    const auto first = fuser.update(conflict, 0.001f);
    require_near(first.target_manual_weight, 0.5f, 0.0001f,
                 "conflict fixture must target AI-supported manual weight");
    require_true(first.applied_manual_weight < 1.0f &&
                 first.applied_manual_weight > 0.90f,
                 "first conflict millisecond must begin rather than finish transition");
    auto decision = first;
    for (int tick = 1; tick < 12; ++tick) {
        decision = fuser.update(conflict, 0.001f);
    }
    require_near(decision.applied_manual_weight, 0.5f, 0.001f,
                 "half-weight ownership change must complete in 12ms");
}

void test_reliable_wrong_way_ads_attenuates_radial_weight_within_6ms() {
    VectorIntentFuser fuser;
    const auto conflict = input_for({-0.20f, 0.0f}, {0.30f, 0.0f});
    auto decision = fuser.update(conflict, 0.001f);
    for (int tick = 1; tick < 6; ++tick) {
        decision = fuser.update(conflict, 0.001f);
    }
    require_near(decision.applied_manual_weight, 0.5f, 0.001f,
                 "reliable wrong-way ADS radial attenuation must finish within 6ms");
}

}  // namespace

int main() {
    try {
        test_candidate_outputs_match_version_one_scales();
        test_candidate_count_covers_polar_set();
        test_aligned_input_keeps_existing_mix();
        test_opposing_small_manual_uses_partial_radial_brake();
        test_wrong_way_manual_only_is_ineligible_below_escape();
        test_predicted_ads_reversal_releases_excess_radial_manual_ownership();
        test_deliberate_opposing_manual_is_not_fully_swallowed();
        test_high_confidence_manual_never_selects_ai_only();
        test_orthogonal_manual_is_not_reduced_by_axis_projection();
        test_short_local_gain_loses_to_lower_160ms_burden();
        test_left_motion_adjusted_error_rate_changes_winner();
        test_aligned_center_cross_is_delegated_to_existing_brake();
        test_bodylock_allows_crossing_while_target_keeps_inertial_direction();
        test_ads_crossing_penalty_does_not_inherit_bodylock_inertia_allowance();
        test_radial_correction_preserves_helpful_tangential_manual();
        test_radial_replacement_can_remove_only_wrong_radial_manual();
        test_tangential_correction_preserves_helpful_radial_manual();
        test_tangential_replacement_can_remove_only_wrong_tangent();
        test_near_equal_cost_keeps_previous_candidate();
        test_diagonal_manual_escape_is_preserved_exactly();
        test_missing_target_falls_back_to_physical_manual();
        test_target_change_releases_without_new_attenuation_step();
        test_reacquiring_low_reliability_and_low_response_release_to_manual();
        test_unreliable_opposing_proposal_yields_exactly_to_manual();
        test_low_response_confidence_does_not_deadlock_safe_ai_fallback();
        test_neutral_manual_input_cannot_create_a_second_ai_brake();
        test_plan_horizon_does_not_double_apply_previous_camera_output();
        test_aligned_input_near_center_is_not_split_into_another_brake();
        test_nonfinite_input_returns_exact_physical_manual();
        test_same_target_ads_to_bodylock_preserves_weight_state();
        test_initial_existing_mix_does_not_ramp_safe_ai();
        test_conflict_weights_approach_target_over_24ms();
        test_reliable_wrong_way_ads_attenuates_radial_weight_within_6ms();
        std::cout << "cod_native_vector_intent_fuser_tests PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_vector_intent_fuser_tests FAIL "
                  << error.what() << '\n';
        return 1;
    }
}
