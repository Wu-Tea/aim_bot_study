#include "vector_intent_fuser.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using controller_native::FusionCandidate;
using controller_native::FusionFallbackReason;
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

VectorIntentFusionInput input_for(
    pipeline_contract::Vec2f manual,
    pipeline_contract::Vec2f ai) {
    VectorIntentFusionInput input{};
    input.manual_stick = manual;
    input.shaped_ai_stick = ai;
    input.manual_confidence = 1.0f;
    input.plan.target_id = 7;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.plan.reliability = 1.0f;
    input.plan.confidence = 1.0f;
    input.plan.aim_authority = 1.0f;
    input.plan.error_px = {24.0f, 0.0f};
    return input;
}

void test_far_observed_aligned_input_keeps_full_mix() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    config.assisted_ai_priority_enabled = false;
    VectorIntentFuser fuser(config);
    auto input = input_for({0.20f, 0.0f}, {0.30f, 0.0f});
    input.plan.normalized_size = 0.12f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.50f, 0.0001f,
                 "fresh observed tracking must keep normal cooperative authority");
    require_near(decision.fused_stick.y, 0.0f, 0.0001f,
                 "cooperative limiting must not manufacture an orthogonal axis");
}

void test_cooperative_limit_preserves_orthogonal_ai() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    config.assisted_ai_priority_enabled = false;
    VectorIntentFuser fuser(config);
    auto input = input_for({0.30f, 0.0f}, {0.25f, 0.20f});
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    input.plan.normalized_size = 0.24f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.3625f, 0.0001f,
                 "cooperative AI parallel to strong manual input exceeded its headroom");
    require_near(decision.fused_stick.y, 0.20f, 0.0001f,
                 "cooperative limiting must preserve orthogonal target-follow AI");
}

void test_cooperative_limit_is_rotation_invariant() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    config.assisted_ai_priority_enabled = false;
    VectorIntentFuser fuser(config);
    auto input = input_for({0.30f, 0.40f}, {0.18f, 0.24f});
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    input.plan.normalized_size = 0.24f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(std::hypot(decision.fused_stick.x,
                            decision.fused_stick.y),
                 0.575f, 0.0001f,
                 "diagonal cooperative input must use the same scalar headroom");
    require_near(decision.fused_stick.x * 0.80f -
                     decision.fused_stick.y * 0.60f,
                 0.0f, 0.0001f,
                 "cooperative limiting rotated a diagonal control vector");
}

void test_coasting_cooperative_push_cannot_recreate_spring_force() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    config.assisted_ai_priority_enabled = false;
    VectorIntentFuser fuser(config);
    auto input = input_for({0.15f, 0.0f}, {0.324f, 0.0f});
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    input.plan.normalized_size = 0.24f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.39317f, 0.0001f,
                 "coasting manual and AI proposals recreated additive spring force");
}

void test_coasting_micro_input_retains_normal_cooperation() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    config.assisted_ai_priority_enabled = false;
    VectorIntentFuser fuser(config);
    auto input = input_for({0.05f, 0.0f}, {0.30f, 0.0f});
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    input.plan.normalized_size = 0.24f;
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.fused_stick.x >= 0.347f,
                 "micro manual input must not materially weaken target coast");
}

void test_far_coast_keeps_full_cooperative_authority() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    config.assisted_ai_priority_enabled = false;
    VectorIntentFuser fuser(config);
    auto input = input_for({0.15f, 0.0f}, {0.324f, 0.0f});
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    input.plan.normalized_size = 0.12f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.474f, 0.0001f,
                 "far target coast must preserve normal cooperative tracking");
}

void test_live_close_stack_fingerprint_is_reduced_without_axis_gate() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    config.assisted_ai_priority_enabled = false;
    VectorIntentFuser fuser(config);
    auto input = input_for(
        {0.145085f, -0.0352794f}, {0.323827f, 0.26265f});
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    input.plan.normalized_size = 0.237f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.395985f, 0.0001f,
                 "live close-target stack retained its spring-like X launch");
    require_near(decision.fused_stick.y, 0.245104f, 0.0001f,
                 "live close-target stack was bounded with an axis-specific gate");
}

controller_native::VectorIntentFusionConfig production_priority_config() {
    controller_native::VectorIntentFusionConfig config{};
    config.manual_escape_threshold = 0.22f;
    config.manual_preservation_floor = 0.75f;
    return config;
}

void test_ads_strong_cooperative_manual_uses_ai_as_primary_proposal() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({0.80f, 0.0f}, {0.30f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.normalized_size = 0.12f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.426667f, 0.0001f,
                 "ADS strong manual and AI proposals were still added as forces");
}

void test_near_bodylock_strong_cooperative_manual_uses_ai_as_primary_proposal() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({0.80f, 0.0f}, {0.30f, 0.0f});
    input.plan.normalized_size = 0.24f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.433333f, 0.0001f,
                 "near BodyLock retained high-sensitivity cooperative stacking");
}

void test_near_bodylock_subfull_countersteer_retains_ai_authority() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({-0.80f, 0.0f}, {0.30f, 0.0f});
    input.plan.normalized_size = 0.24f;
    const auto decision = fuser.update(input, 0.001f);
    require_true(!decision.manual_escape,
                 "sub-full high-sensitivity countersteer hard-dropped AI");
    require_true(decision.fused_stick.x > -0.70f,
                 "sub-full countersteer did not retain material AI authority");
}

void test_near_ai_priority_preserves_orthogonal_manual_intent() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({0.80f, 0.20f}, {0.30f, 0.0f});
    input.plan.normalized_size = 0.24f;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.433333f, 0.0001f,
                 "AI-priority radial control did not use bounded manual headroom");
    require_near(decision.fused_stick.y, 0.20f, 0.0001f,
                 "AI-priority radial control swallowed orthogonal manual intent");
}

void test_far_bodylock_full_cooperative_input_remains_manual_escape() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({1.0f, 0.0f}, {0.30f, 0.0f});
    input.plan.normalized_size = 0.12f;
    const auto pending = fuser.update(input, 0.001f);
    require_true(!pending.manual_escape,
                 "near-full input must not transfer ownership on one tick");
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.manual_escape,
                 "far BodyLock must keep the accepted full-manual escape boundary");
    require_near(decision.fused_stick.x, 1.0f, 0.0001f,
                 "far BodyLock full input must remain exact physical input");
}

void test_ads_full_cooperative_input_remains_on_ai_priority_path() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({1.0f, 0.0f}, {0.30f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.normalized_size = 0.12f;
    const auto decision = fuser.update(input, 0.001f);
    require_true(!decision.manual_escape,
                 "cooperative ADS input was misclassified as an escape request");
    require_near(decision.fused_stick.x, 0.458333f, 0.0001f,
                 "full cooperative ADS input recreated additive launch force");
}

void test_near_bodylock_full_opposing_input_remains_exact_escape() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({-1.0f, 0.0f}, {0.30f, 0.0f});
    input.plan.normalized_size = 0.24f;
    const auto pending = fuser.update(input, 0.001f);
    require_true(!pending.manual_escape,
                 "near-full opposing input must require a consecutive confirmation");
    require_true(pending.fused_stick.x > -0.95f,
                 "one near-full opposing tick must stay inside the predictive envelope");
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.manual_escape,
                 "near BodyLock swallowed a full opposing escape request");
    require_near(decision.fused_stick.x, -1.0f, 0.0001f,
                 "near BodyLock full escape must remain exact physical input");
}

void test_ads_moderate_manual_input_keeps_ordinary_cooperation() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({0.30f, 0.0f}, {0.30f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.60f, 0.0001f,
                 "AI priority activated below the strong-manual boundary");
}

void test_same_direction_subfull_manual_is_not_escape_feedback() {
    for (const float manual_x : {0.10f, 0.50f}) {
        VectorIntentFuser fuser(production_priority_config());
        auto input = input_for({manual_x, 0.0f}, {0.30f, 0.0f});
        input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
        const auto decision = fuser.update(input, 0.001f);
        require_true(!decision.manual_escape,
                     "same-direction sub-full manual input became an escape");
    }
}

void test_ads_log_normalization_uses_effective_1p9_parallel_proposal() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({0.70f, 0.0f}, {0.30f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.normalized_size = 0.12f;
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.candidate == FusionCandidate::RadialCorrected,
                 "ADS normalization left assisted proposal ownership");
    require_near(decision.fused_stick.x, 0.410833f, 0.0001f,
                 "ADS cooperative parallel proposal did not use 1.9/2.4 scaling");
}

void test_ads_normalized_manual_participates_when_ai_proposal_is_larger() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({0.70f, 0.0f}, {0.80f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.910833f, 0.0001f,
                 "cooperative manual proposal was ignored below the AI proposal");
    require_true(decision.applied_manual_weight > 0.15f,
                 "fusion telemetry did not retain bounded manual participation");
}

void test_near_bodylock_log_normalization_uses_effective_2p0_only_near() {
    auto near_input = input_for({0.70f, 0.0f}, {0.30f, 0.0f});
    near_input.plan.normalized_size = 0.24f;
    VectorIntentFuser near_fuser(production_priority_config());
    const auto near = near_fuser.update(near_input, 0.001f);
    require_near(near.fused_stick.x, 0.416667f, 0.0001f,
                 "near BodyLock cooperative proposal did not use 2.0/2.4 scaling");

    auto far_input = near_input;
    far_input.plan.normalized_size = 0.12f;
    VectorIntentFuser far_fuser(production_priority_config());
    const auto far = far_fuser.update(far_input, 0.001f);
    require_near(far.fused_stick.x, 1.0f, 0.0001f,
                 "far BodyLock was changed by near-only manual normalization");
    require_true(far.candidate != FusionCandidate::RadialCorrected,
                 "far BodyLock entered near assisted proposal ownership");
}

void test_log_normalization_preserves_tangent_and_opposing_manual() {
    VectorIntentFuser tangent_fuser(production_priority_config());
    auto tangent_input = input_for({0.70f, 0.20f}, {0.30f, 0.0f});
    tangent_input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    const auto tangent = tangent_fuser.update(tangent_input, 0.001f);
    require_near(tangent.fused_stick.y, 0.20f, 0.0001f,
                 "ADS normalization scaled orthogonal manual intent");

    auto enabled_config = production_priority_config();
    auto disabled_config = enabled_config;
    disabled_config.contextual_manual_normalization_enabled = false;
    auto opposing_input = input_for({-0.80f, 0.0f}, {0.30f, 0.0f});
    opposing_input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    VectorIntentFuser enabled(enabled_config);
    VectorIntentFuser disabled(disabled_config);
    const auto normalized = enabled.update(opposing_input, 0.001f);
    const auto stage_one = disabled.update(opposing_input, 0.001f);
    require_near(normalized.fused_stick.x, stage_one.fused_stick.x, 0.0001f,
                 "same-direction normalization weakened opposing counter-steer");
    require_near(normalized.applied_manual_weight,
                 stage_one.applied_manual_weight, 0.0001f,
                 "opposing counter-steer telemetry reported false normalization");
}

void test_log_normalization_classifies_strength_from_raw_physical_input() {
    auto config = production_priority_config();
    config.ads_same_direction_manual_scale = 0.10f;
    VectorIntentFuser fuser(config);
    auto input = input_for({0.55f, 0.0f}, {0.30f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.candidate == FusionCandidate::RadialCorrected,
                 "scaled proposal incorrectly disabled raw strong-input admission");
}

void test_opposing_input_preserves_manual_and_continuously_retires_ai() {
    VectorIntentFuser fuser;
    float previous_ai = 2.0f;
    for (float manual : {-0.10f, -0.20f, -0.30f, -0.40f, -0.46f}) {
        controller_native::VectorIntentFusionDecision decision{};
        for (int tick = 0; tick < 8; ++tick) {
            decision = fuser.update(
                input_for({manual, 0.0f}, {0.50f, 0.0f}), 0.001f);
        }
        const float effective_ai = decision.fused_stick.x - manual;
        require_true(effective_ai <= previous_ai + 0.025f,
                     "AI brake reasserted discontinuously as counter-steer increased");
        require_near(decision.applied_manual_weight, 1.0f, 0.0001f,
                     "fusion must never rewrite physical manual input");
        previous_ai = effective_ai;
    }
    require_true(previous_ai <= 0.208f,
                 "opposing AI must preserve the configured share of manual input");
}

void test_escape_threshold_is_not_a_control_switch() {
    VectorIntentFuser fuser;
    float previous = 0.0f;
    float maximum_threshold_jump = 0.0f;
    bool first = true;
    for (float manual : {-0.30f, -0.36f, -0.42f, -0.46f,
                         -0.42f, -0.36f, -0.30f}) {
        const auto decision = fuser.update(
            input_for({manual, 0.0f}, {0.50f, 0.0f}), 0.001f);
        if (!first && std::fabs(manual) >= 0.42f) {
            maximum_threshold_jump = std::max(
                maximum_threshold_jump,
                std::fabs(decision.fused_stick.x - previous));
        }
        previous = decision.fused_stick.x;
        first = false;
    }
    require_true(maximum_threshold_jump <= 0.12f,
                 "manual threshold crossing created an output impulse");
}

void test_fresh_vision_corrects_countersteer_without_touching_tangent() {
    VectorIntentFuser normal;
    VectorIntentFuser fresh;
    auto normal_input = input_for({-0.30f, 0.0f}, {0.50f, 0.20f});
    auto fresh_input = normal_input;
    fresh_input.fresh_single_target_observation = true;
    const auto a = normal.update(normal_input, 0.001f);
    const auto b = fresh.update(fresh_input, 0.001f);
    require_true(b.fused_stick.x > a.fused_stick.x + 0.20f,
                 "fresh Vision must be able to correct obsolete radial manual input");
    require_near(a.fused_stick.y, 0.20f, 0.0001f,
                 "AI motion orthogonal to the conflict must remain complete");
    require_near(b.fused_stick.y, 0.20f, 0.0001f,
                 "fresh radial correction must preserve tangent AI motion");
    require_true(b.fresh_vision_wrong_way_policy_applied,
                 "fresh radial policy activation must be visible at the fuser boundary");
}

void test_fresh_wrong_way_manual_floor_cancels_obsolete_radial_demand() {
    // Live ADS crossing fingerprint: fresh AI has already reversed, while
    // physical input still contributes the old direction.  This is written
    // against the one fuser boundary so the old 0.75 preservation path cannot
    // be hidden by a later output clamp.
    for (const auto& axis : {
             pipeline_contract::Vec2f{1.0f, 0.0f},
             pipeline_contract::Vec2f{0.0f, 1.0f},
             pipeline_contract::Vec2f{0.6f, 0.8f}}) {
        VectorIntentFuser fuser;
        auto input = input_for(
            {axis.x * 0.32f, axis.y * 0.32f},
            {-axis.x * 0.54f, -axis.y * 0.54f});
        input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
        input.plan.error_px = {-axis.x * 80.0f, axis.y * 80.0f};
        input.plan.reliability = 0.95f;
        input.plan.confidence = 0.95f;
        input.plan.response_confidence = 0.80f;
        input.fresh_single_target_observation = true;
        const auto decision = fuser.update(input, 0.001f);
        const float radial_output =
            decision.fused_stick.x * -axis.x +
            decision.fused_stick.y * -axis.y;
        require_true(
            radial_output >= 0.0f,
            "fresh radial AI correction must cancel obsolete manual demand");
    }
}

void test_fresh_ai_wrong_way_is_bounded_by_position_evidence() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.0f}, {-0.80f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.error_px = {80.0f, 0.0f};
    input.plan.reliability = 0.95f;
    input.fresh_single_target_observation = true;
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.fresh_vision_ai_radial_bound_applied,
                 "fresh position must expose the AI radial bound");
    require_true(decision.fused_stick.x >= -0.001f,
                 "fresh position must prevent an obsolete AI sign from deepening error");
}

void test_strong_manual_and_ai_are_one_radial_proposal_envelope() {
    for (const float manual : {0.70f, 1.00f}) {
        VectorIntentFuser cooperative;
        auto same = input_for({manual, 0.0f}, {0.80f, 0.0f});
        same.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
        same.plan.error_px = {80.0f, 0.0f};
        same.fresh_single_target_observation = true;
        const auto same_decision = cooperative.update(same, 0.001f);
        require_true(
            same_decision.fused_stick.x <=
                std::max(std::fabs(manual), 0.80f) + 0.001f,
            "strong same-direction manual and AI became an additive radial force");

        VectorIntentFuser opposing;
        auto opposite = input_for({-manual, 0.0f}, {0.80f, 0.0f});
        opposite.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
        opposite.plan.error_px = {80.0f, 0.0f};
        opposite.plan.reliability = 0.95f;
        opposite.fresh_single_target_observation = true;
        const auto opposite_decision = opposing.update(opposite, 0.001f);
        require_true(opposite_decision.fused_stick.x >= -0.001f,
                     "strong opposing manual must not deepen fresh radial error");
    }
}

void test_manual_escape_stays_latched_until_release_or_identity_change() {
    VectorIntentFuser fuser(production_priority_config());
    auto input = input_for({-1.0f, 0.0f}, {0.30f, 0.0f});
    input.plan.normalized_size = 0.24f;

    const auto first = fuser.update(input, 0.001f);
    require_true(!first.manual_escape,
                 "the first held escape candidate must remain inside the envelope");
    for (int tick = 0; tick < 4; ++tick) {
        const auto held = fuser.update(input, 0.001f);
        require_true(held.manual_escape,
                     "confirmed manual escape must stay latched while held");
        require_near(held.fused_stick.x, -1.0f, 0.0001f,
                     "latched manual escape must preserve the held X input");
    }

    // A shaped-AI direction change is not a physical release. The confirmed
    // ownership must remain exact-manual while the same stick stays held.
    auto flip_probe = input;
    flip_probe.shaped_ai_stick = {-0.35f, 0.0f};
    VectorIntentFuser classifier_probe(production_priority_config());
    const auto unlatched_flip = classifier_probe.update(flip_probe, 0.001f);
    require_true(!unlatched_flip.manual_escape,
                 "cooperative AI flip must not independently classify as escape");
    input.shaped_ai_stick = {-0.35f, 0.0f};
    for (int tick = 0; tick < 3; ++tick) {
        const auto ai_flip = fuser.update(input, 0.001f);
        require_true(ai_flip.manual_escape,
                     "AI direction change must not unlock a held escape latch");
        require_near(ai_flip.fused_stick.x, -1.0f, 0.0001f,
                     "AI direction change must not replace latched manual output");
    }

    input.manual_stick = {};
    input.shaped_ai_stick = {0.30f, 0.0f};
    const auto released = fuser.update(input, 0.001f);
    require_true(!released.manual_escape,
                 "manual release must clear the escape latch");

    input.manual_stick = {-1.0f, 0.0f};
    const auto same_target_first = fuser.update(input, 0.001f);
    require_true(!same_target_first.manual_escape,
                 "a re-held escape must require a fresh consecutive confirmation");
    const auto same_target_second = fuser.update(input, 0.001f);
    require_true(same_target_second.manual_escape,
                 "same-target re-held escape must confirm after the pending tick");

    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    const auto ads_transition = fuser.update(input, 0.001f);
    require_true(ads_transition.manual_escape,
                 "same-target ADS transition must keep escape ownership latched");
    input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto lifecycle_transition = fuser.update(input, 0.001f);
    require_true(lifecycle_transition.manual_escape,
                 "same-target lifecycle transition must keep escape ownership latched");

    input.plan.target_id = 8;
    const auto changed = fuser.update(input, 0.001f);
    require_true(changed.reason == FusionFallbackReason::TargetChanged,
                 "target change must keep its explicit manual admission boundary");
    require_true(!changed.manual_escape,
                 "target change must clear the previous target escape latch");
    const auto replacement_pending = fuser.update(input, 0.001f);
    require_true(!replacement_pending.manual_escape,
                 "replacement target must not inherit a latched escape");
}

void test_fresh_final_continuity_is_source_agnostic() {
    auto make_fuser = [] {
        VectorIntentFuser fuser;
        auto baseline = input_for({0.50f, 0.0f}, {});
        baseline.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
        (void)fuser.update(baseline, 0.001f);
        return fuser;
    };

    VectorIntentFuser manual_fuser = make_fuser();
    auto manual = input_for({0.80f, 0.0f}, {});
    manual.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    manual.plan.error_px = {80.0f, 0.0f};
    manual.plan.predicted_terminal_error_px = manual.plan.error_px;
    manual.plan.response_scale = 500.0f;
    manual.plan.response_confidence = 1.0f;
    manual.fresh_single_target_observation = true;
    manual.response_horizon_seconds = 0.20f;
    manual.response_horizon_y_seconds = 0.20f;
    manual.response_max_force = {1.0f, 1.0f};
    manual.response_envelope_valid = true;
    manual.response_envelope_source = "bodylock_response_model";

    VectorIntentFuser ai_fuser = make_fuser();
    auto ai = manual;
    ai.manual_stick = {};
    ai.shaped_ai_stick = {0.80f, 0.0f};

    const auto manual_result = manual_fuser.update(manual, 0.001f);
    const auto ai_result = ai_fuser.update(ai, 0.001f);
    require_near(manual_result.fresh_vision_pre_slew_radial,
                 ai_result.fresh_vision_pre_slew_radial,
                 0.0001f,
                 "matched fresh proposals did not produce one final demand");
    require_near(manual_result.fused_stick.x,
                 ai_result.fused_stick.x,
                 0.0001f,
                 "fresh output slew changed solely because proposal source changed");
    require_near(manual_result.fused_stick.x, 0.58f, 0.0002f,
                 "fresh source-agnostic continuity did not use the single output step");
}

void test_zero_authority_stays_manual_safe() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.0f}, {0.70f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.plan.aim_authority = 0.0f;
    input.plan.error_px = {80.0f, 0.0f};
    input.plan.reliability = 1.0f;
    input.fresh_single_target_observation = true;
    const auto decision = fuser.update(input, 0.001f);
    require_true(!decision.fresh_vision_predictive_envelope_applied,
                 "zero-authority BodyLock must not enter the fresh final-output owner");
    require_true(decision.reason == FusionFallbackReason::NoAuthority,
                 "zero authority must expose an explicit manual-safe reason");
    require_near(decision.fused_stick.x, 0.30f, 0.0001f,
                 "zero authority must reject stale AI and preserve exact manual input");
}

void test_fresh_shared_envelope_is_continuous_below_old_strength_gate() {
    for (const auto strengths : {
             std::pair<float, float>{0.10f, 0.30f},
             std::pair<float, float>{0.30f, 0.45f},
             std::pair<float, float>{0.45f, 0.70f},
             std::pair<float, float>{0.70f, 1.00f}}) {
        VectorIntentFuser fuser;
        auto input = input_for(
            {strengths.first, 0.0f}, {strengths.second, 0.0f});
        input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
        input.plan.error_px = {80.0f, 0.0f};
        input.fresh_single_target_observation = true;
        const auto decision = fuser.update(input, 0.001f);
        require_true(decision.fresh_vision_predictive_envelope_applied,
                     "fresh reliable proposals must report the shared envelope");
        require_true(
            decision.fused_stick.x < strengths.first + strengths.second - 0.001f,
            "fresh manual and AI proposals must not be raw additive forces");
        require_true(
            decision.fresh_vision_strongest_valid_radial <=
                std::max(strengths.first, strengths.second) + 0.0001f,
            "fresh radial demand exceeded the strongest valid proposal");
        require_true(
            decision.fresh_vision_final_radial <=
                decision.fresh_vision_permitted_radial + 0.0001f,
            "fresh radial demand exceeded stopping permission");
        require_true(decision.fused_stick.x >= -0.001f,
                     "same-direction fresh proposals must not reverse target error");
    }
}

void test_fresh_envelope_uses_configured_dynamic_target_curve() {
    auto input = input_for({}, {0.80f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.error_px = {20.0f, 0.0f};
    input.plan.predicted_terminal_error_px = input.plan.error_px;
    input.plan.response_scale = 500.0f;
    input.plan.response_confidence = 1.0f;
    input.fresh_single_target_observation = true;
    input.response_horizon_seconds = 0.20f;
    input.response_horizon_y_seconds = 0.20f;
    input.response_max_force = {1.0f, 1.0f};
    input.response_envelope_valid = true;

    VectorIntentFuser linear_fuser;
    const auto linear = linear_fuser.update(input, 0.001f);
    input.response_curve.algorithm =
        controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut;
    input.response_curve.calibration_reference_stick = 0.50f;
    VectorIntentFuser dynamic_fuser;
    const auto dynamic = dynamic_fuser.update(input, 0.001f);

    require_near(linear.fresh_vision_permitted_radial, 0.20f, 0.0001f,
                 "linear fresh envelope fixture changed its target demand");
    require_true(
        dynamic.fresh_vision_permitted_radial >
            linear.fresh_vision_permitted_radial + 0.05f,
        "fresh fuser envelope clipped away the Dynamic target correction");
}

void test_fresh_post_slew_radial_guard_survives_center_crossing() {
    VectorIntentFuser fuser;
    auto input = input_for({0.30f, 0.0f}, {0.30f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.error_px = {80.0f, 0.0f};
    input.fresh_single_target_observation = true;
    const auto before_cross = fuser.update(input, 0.001f);
    require_true(before_cross.fused_stick.x > 0.0f,
                 "center-cross fixture must initialize an old radial output");

    input.manual_stick = {};
    input.shaped_ai_stick = {-0.30f, 0.0f};
    input.plan.error_px = {-80.0f, 0.0f};
    const auto after_cross = fuser.update(input, 0.001f);
    require_true(after_cross.fresh_vision_final_radial >= -0.0001f,
                 "post-slew final radial must not deepen a fresh crossed error");
    require_near(after_cross.fresh_vision_final_radial,
                 after_cross.fused_stick.x * -1.0f,
                 0.0001f,
                 "final radial telemetry must match the returned post-slew vector");
}

void test_active_bodylock_envelope_limits_manual_and_ai() {
    for (const auto error : {
             pipeline_contract::Vec2f{80.0f, 0.0f},
             pipeline_contract::Vec2f{0.0f, 80.0f},
             pipeline_contract::Vec2f{80.0f, 80.0f},
             pipeline_contract::Vec2f{12.0f, 0.0f}}) {
        VectorIntentFuser fuser;
        const pipeline_contract::Vec2f control_error{
            error.x, -error.y};
        const float error_length = std::hypot(
            control_error.x, control_error.y);
        const pipeline_contract::Vec2f radial{
            control_error.x / error_length,
            control_error.y / error_length};
        auto input = input_for(radial,
                               {radial.x * 0.80f, radial.y * 0.80f});
        input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
        input.plan.error_px = error;
        input.plan.response_scale = 500.0f;
        input.plan.response_confidence = 1.0f;
        input.plan.predicted_terminal_error_px = error;
        input.fresh_single_target_observation = true;
        input.response_horizon_seconds = 0.18f;
        input.response_horizon_y_seconds = 0.128f;
        input.response_max_force = {0.30f, 0.42f};
        input.response_envelope_valid = true;
        input.response_envelope_source = "bodylock_response_model";
        const auto decision = fuser.update(input, 0.001f);
        const float ellipse = std::hypot(
            decision.fused_stick.x / 0.30f,
            decision.fused_stick.y / 0.42f);
        require_true(ellipse <= 1.0001f,
                     "fresh BodyLock manual+AI exceeded its configured force ellipse");
        const float actual_radial =
            decision.fused_stick.x * radial.x +
            decision.fused_stick.y * radial.y;
        require_true(actual_radial > 0.001f,
                     "fresh BodyLock envelope test unexpectedly produced zero output");
        if (error_length > 50.0f) {
            require_true(decision.fresh_vision_final_radial >=
                             decision.fresh_vision_permitted_radial - 0.0002f,
                         "far BodyLock case did not reach its active stopping/force bound");
        } else {
            require_true(decision.fresh_vision_final_radial < 0.20f,
                         "near BodyLock case did not remain stopping-demand limited");
        }
        require_true(decision.fresh_vision_envelope_reason != nullptr &&
                         std::string(decision.fresh_vision_envelope_source) ==
                             "bodylock_response_model",
                     "BodyLock envelope source was not observable");
        require_near(decision.fresh_vision_envelope_horizon_seconds,
                     0.18f, 0.0001f,
                     "BodyLock X horizon was not carried into fusion");
    }
}

void test_shaped_ai_authority_is_not_applied_twice() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.40f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.plan.aim_authority = 0.50f;
    input.plan.error_px = {80.0f, 0.0f};
    input.plan.predicted_terminal_error_px = {80.0f, 0.0f};
    input.plan.response_scale = 500.0f;
    input.plan.response_confidence = 1.0f;
    input.fresh_single_target_observation = true;
    input.response_horizon_seconds = 0.18f;
    input.response_horizon_y_seconds = 0.18f;
    input.response_max_force = {0.60f, 0.60f};
    input.response_envelope_valid = true;
    input.response_envelope_source = "bodylock_response_model";
    const auto decision = fuser.update(input, 0.001f);
    // 80/(0.18*500)*0.50 = 0.4444, so the 0.40 shaped proposal is below
    // the model demand and must pass through unchanged.
    require_near(decision.fresh_vision_stopping_radial,
                 80.0f / (0.18f * 500.0f) * 0.50f,
                 0.0002f,
                 "model stopping demand did not apply active authority once");
    require_near(decision.fused_stick.x, 0.40f, 0.0002f,
                 "shaped AI proposal was attenuated a second time by the fuser");
}

void test_rotated_force_ellipse_uses_both_tangent_signs() {
    const float diagonal = std::sqrt(0.5f);
    const pipeline_contract::Vec2f radial{diagonal, diagonal};
    const pipeline_contract::Vec2f tangent{-diagonal, diagonal};
    for (const float tangent_demand : {-0.30f, 0.30f}) {
        VectorIntentFuser fuser;
        auto input = input_for(
            {radial.x * 0.80f + tangent.x * tangent_demand,
             radial.y * 0.80f + tangent.y * tangent_demand},
            {});
        input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
        input.plan.error_px = {20.0f, -20.0f};
        input.plan.predicted_terminal_error_px = input.plan.error_px;
        input.plan.aim_authority = 0.70f;
        input.plan.response_scale = 500.0f;
        input.plan.response_confidence = 1.0f;
        input.fresh_single_target_observation = true;
        input.response_horizon_seconds = 0.20f;
        input.response_horizon_y_seconds = 0.20f;
        input.response_max_force = {0.30f, 0.42f};
        input.response_envelope_valid = true;
        input.response_envelope_source = "bodylock_response_model";
        const auto decision = fuser.update(input, 0.001f);
        const float ellipse = std::hypot(
            decision.fused_stick.x / 0.30f,
            decision.fused_stick.y / 0.42f);
        require_true(ellipse <= 1.0001f,
                     "rotated fresh output exceeded the anisotropic force ellipse");
        if (tangent_demand < 0.0f) {
            const float output_tangent =
                decision.fused_stick.x * tangent.x +
                decision.fused_stick.y * tangent.y;
            require_true(output_tangent > -0.23f,
                         "negative rotated tangent exceeded its asymmetric ellipse interval");
        }
    }
}

void test_ads_to_bodylock_post_slew_respects_new_ellipse() {
    VectorIntentFuser fuser;
    auto input = input_for({1.0f, 0.0f}, {0.80f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.error_px = {80.0f, 0.0f};
    input.plan.predicted_terminal_error_px = input.plan.error_px;
    input.plan.response_scale = 500.0f;
    input.plan.response_confidence = 1.0f;
    input.fresh_single_target_observation = true;
    input.response_horizon_seconds = 0.135f;
    input.response_horizon_y_seconds = 0.135f;
    input.response_max_force = {1.0f, 1.0f};
    input.response_envelope_valid = true;
    input.response_envelope_source = "ads_acquisition_response_model";
    const auto ads = fuser.update(input, 0.001f);
    require_true(ads.fused_stick.x > 0.70f,
                 "ADS setup failed to create a large previous output");

    input.plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    input.response_horizon_seconds = 0.18f;
    input.response_horizon_y_seconds = 0.128f;
    input.response_max_force = {0.30f, 0.42f};
    input.response_envelope_source = "bodylock_response_model";
    const auto bodylock = fuser.update(input, 0.001f);
    const float ellipse = std::hypot(
        bodylock.fused_stick.x / 0.30f,
        bodylock.fused_stick.y / 0.42f);
    require_true(ellipse <= 1.0001f,
                 "ADS to BodyLock post-slew output escaped the new force ellipse");
    require_near(bodylock.fresh_vision_final_radial,
                 bodylock.fused_stick.x,
                 0.0002f,
                 "post-slew final radial was not computed from actual output");
}

void test_active_ads_envelope_uses_acquisition_window_and_force() {
    VectorIntentFuser fuser;
    auto input = input_for({1.0f, 0.0f}, {0.80f, 0.0f});
    input.plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    input.plan.error_px = {80.0f, 0.0f};
    input.plan.response_scale = 500.0f;
    input.plan.response_confidence = 1.0f;
    input.plan.predicted_terminal_error_px = {80.0f, 0.0f};
    input.fresh_single_target_observation = true;
    input.response_horizon_seconds = 0.135f;
    input.response_horizon_y_seconds = 0.135f;
    input.response_max_force = {0.45f, 0.55f};
    input.response_envelope_valid = true;
    input.response_envelope_source = "ads_acquisition_response_model";
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.fused_stick.x <= 0.4501f,
                 "ADS fresh output exceeded its configured X force envelope");
    require_near(decision.fresh_vision_envelope_horizon_seconds,
                 0.135f, 0.0001f,
                 "ADS acquisition horizon was not carried into fusion");
    require_true(std::string(decision.fresh_vision_envelope_source) ==
                     "ads_acquisition_response_model",
                 "ADS envelope source was not observable");
}

void test_reacquire_and_target_change_fail_safe_to_manual() {
    VectorIntentFuser fuser;
    (void)fuser.update(input_for({}, {0.50f, 0.0f}), 0.001f);

    auto changed = input_for({-0.20f, 0.10f}, {0.50f, 0.0f});
    changed.plan.target_id = 8;
    const auto target_change = fuser.update(changed, 0.001f);
    require_true(target_change.reason == FusionFallbackReason::TargetChanged,
                 "target identity transition must be explicit");
    require_near(target_change.fused_stick.x, -0.20f, 0.0001f,
                 "new target must not inherit old AI ownership");

    changed.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto reacquire = fuser.update(changed, 0.001f);
    require_true(reacquire.reason == FusionFallbackReason::None,
                 "same-target reacquisition must remain on the normal fusion path");
    require_true(!reacquire.fallback,
                 "same-target reacquisition must not use manual fallback");
    require_true(reacquire.fused_stick.x > -0.20f,
                 "same-target reacquisition must retain the fresh AI proposal");
}

void test_reliability_boundary_does_not_drop_and_reassert_ai() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.55f, 0.0f});
    input.plan.reliability = 0.70f;
    auto previous = fuser.update(input, 0.001f).fused_stick;

    for (const float reliability : {0.64f, 0.68f, 0.63f, 0.72f}) {
        input.plan.reliability = reliability;
        const auto decision = fuser.update(input, 0.001f);
        require_true(
            std::fabs(decision.fused_stick.x - previous.x) <= 0.081f,
            "reliability boundary created a full AI off/on output impulse");
        previous = decision.fused_stick;
    }
}

void test_reacquire_to_observed_reenters_through_existing_slew() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    auto previous = fuser.update(input, 0.001f).fused_stick;

    input.manual_stick = {0.05f, -0.02f};
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto reacquiring = fuser.update(input, 0.001f);
    require_true(
        std::hypot(reacquiring.fused_stick.x - previous.x,
                   reacquiring.fused_stick.y - previous.y) <= 0.201f,
        "entering reacquiring hard-cut the existing AI output");
    previous = reacquiring.fused_stick;

    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    const auto observed = fuser.update(input, 0.001f);
    require_true(
        std::hypot(observed.fused_stick.x - previous.x,
                   observed.fused_stick.y - previous.y) <= 0.081f,
        "reacquire to observed cold-started full AI instead of reasserting by slew");
}

void test_reacquiring_release_edge_is_bounded() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    const auto active = fuser.update(input, 0.001f);
    input.manual_stick = {0.05f, 0.0f};
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto released = fuser.update(input, 0.001f);
    require_true(
        std::fabs(released.fused_stick.x - active.fused_stick.x) <= 0.201f,
        "reacquiring release exceeded the deliberate manual release envelope");
    require_true(released.applied_ai_weight > 0.0f,
                 "reacquiring release discarded all output continuity state");
}

void test_full_manual_escape_preempts_reacquiring_release_slew() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    (void)fuser.update(input, 0.001f);

    input.manual_stick = {-1.0f, 0.0f};
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto pending = fuser.update(input, 0.001f);
    require_true(!pending.manual_escape,
                 "full manual escape must not be unconditional on one tick");
    const auto escaped = fuser.update(input, 0.001f);
    require_true(escaped.manual_escape,
                 "full manual escape must preempt reacquiring release slew");
    require_true(escaped.reason == FusionFallbackReason::ManualEscape,
                 "reacquiring full escape must report manual ownership");
    require_true(escaped.candidate == FusionCandidate::ManualOnly,
                 "reacquiring full escape must disallow AI ownership");
    require_near(escaped.fused_stick.x, -1.0f, 0.0001f,
                 "reacquiring must not delay full physical counter-steer");
}

void test_full_manual_escape_cannot_be_blocked_by_saturated_ai() {
    VectorIntentFuser fuser;
    const auto input = input_for({-1.0f, 0.0f}, {1.34f, 0.0f});
    const auto pending = fuser.update(input, 0.001f);
    require_true(!pending.manual_escape,
                 "saturated AI must not be preempted by a one-tick full input");
    const auto escaped = fuser.update(input, 0.001f);
    require_true(escaped.manual_escape,
                 "saturated shaped AI must not block full manual escape");
    require_true(escaped.candidate == FusionCandidate::ManualOnly,
                 "saturated AI escape must transfer ownership to manual");
    require_near(escaped.fused_stick.x, -1.0f, 0.0001f,
                 "saturated AI escape must preserve exact physical input");
}

void test_diagonal_manual_escape_is_preserved_exactly() {
    VectorIntentFuser fuser;
    const auto input = input_for({-0.50f, 0.40f}, {0.40f, -0.30f});
    const auto pending = fuser.update(input, 0.001f);
    require_true(!pending.manual_escape,
                 "diagonal near-full input must require a consecutive confirmation");
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.manual_escape,
                 "deliberate diagonal input must be classified as escape");
    require_true(decision.candidate == FusionCandidate::ManualOnly,
                 "manual escape must disallow AI-owned candidates");
    require_near(decision.fused_stick.x, input.manual_stick.x, 0.0001f,
                 "manual escape X must remain exact physical input");
    require_near(decision.fused_stick.y, input.manual_stick.y, 0.0001f,
                 "manual escape Y must remain exact physical input");
}

void test_nonfinite_input_returns_exact_physical_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({0.20f, -0.10f}, {0.30f, 0.20f});
    input.plan.error_px.x = std::numeric_limits<float>::quiet_NaN();
    const auto decision = fuser.update(input, 0.001f);
    require_true(decision.fallback,
                 "non-finite plan must use the finite manual fallback");
    require_near(decision.fused_stick.x, 0.20f, 0.0001f,
                 "non-finite fallback X must preserve physical input");
    require_near(decision.fused_stick.y, -0.10f, 0.0001f,
                 "non-finite fallback Y must preserve physical input");
}

void test_remaining_work_rotation_does_not_reproject_stable_manual_input() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.38f, 0.02f}, {0.24f, 0.0f});
    input.plan.error_px = {30.0f, 2.0f};
    input.plan.remaining_work_px = input.plan.error_px;
    input.plan.remaining_work_confidence = 1.0f;
    input.plan.remaining_work_valid = true;
    // This is a Remaining/work-coordinate continuity guard, not fresh
    // positional authority. The P1 fresh policy is covered separately.
    input.fresh_single_target_observation = false;

    const auto before = fuser.update(input, 0.024f);
    input.plan.error_px = {2.0f, -30.0f};
    input.plan.remaining_work_px = input.plan.error_px;
    input.plan.delivered_camera_motion_since_capture_px = {28.0f, 32.0f};
    const auto after = fuser.update(input, 0.001f);
    require_true(
        std::hypot(after.fused_stick.x - before.fused_stick.x,
                   after.fused_stick.y - before.fused_stick.y) < 0.05f,
        "rotating Remaining error must not rotate the manual projection basis");
}

void test_visual_reference_rotation_is_not_a_one_tick_output_rotation() {
    VectorIntentFuser fuser;
    auto input = input_for({-0.38f, 0.02f}, {0.24f, 0.0f});
    input.plan.error_px = {30.0f, 2.0f};
    input.fresh_single_target_observation = false;

    const auto before = fuser.update(input, 0.024f);
    input.plan.error_px = {2.0f, -30.0f};
    const auto after = fuser.update(input, 0.001f);
    require_true(
        std::hypot(after.fused_stick.x - before.fused_stick.x,
                   after.fused_stick.y - before.fused_stick.y) < 0.10f,
        "a one-frame visual direction change must not rotate output instantly");
}

void test_target_change_reenters_from_manual_baseline() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    (void)fuser.update(input, 0.001f);

    input.plan.target_id = 8;
    input.manual_stick = {0.05f, 0.0f};
    const auto changed = fuser.update(input, 0.001f);
    require_true(changed.reason == FusionFallbackReason::TargetChanged,
                 "target change must still be explicit");

    const auto reentered = fuser.update(input, 0.001f);
    require_true(
        std::fabs(reentered.fused_stick.x - changed.fused_stick.x) <= 0.081f,
        "new target re-entry inherited a full old/new AI impulse");
}

void test_no_target_replacement_uses_manual_admission_tick() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    (void)fuser.update(input, 0.001f);

    input.plan.target_id = 0;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::None;
    input.manual_stick = {-0.04f, 0.03f};
    const auto no_target = fuser.update(input, 0.001f);
    require_near(no_target.fused_stick.x, -0.04f, 0.0001f,
                 "no-target re-entry baseline must preserve manual X");

    input.plan.target_id = 9;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    const auto admitted = fuser.update(input, 0.001f);
    require_true(admitted.reason == FusionFallbackReason::TargetChanged,
                 "replacement after a no-target gap must remain an explicit target change");
    require_near(admitted.fused_stick.x, input.manual_stick.x, 0.0001f,
                 "replacement admission tick must preserve exact manual X");
    require_near(admitted.fused_stick.y, input.manual_stick.y, 0.0001f,
                 "replacement admission tick must preserve exact manual Y");

    const auto reentered = fuser.update(input, 0.001f);
    require_true(
        std::hypot(reentered.fused_stick.x - admitted.fused_stick.x,
                   reentered.fused_stick.y - admitted.fused_stick.y) <= 0.081f,
        "replacement target re-entry bypassed the post-admission slew");
}

void test_no_target_is_exact_manual() {
    VectorIntentFuser fuser;
    auto input = input_for({0.17f, -0.23f}, {0.60f, 0.60f});
    input.plan.target_id = 0;
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::None;
    const auto decision = fuser.update(input, 0.001f);
    require_near(decision.fused_stick.x, 0.17f, 0.0001f,
                 "no-target X must be exact manual");
    require_near(decision.fused_stick.y, -0.23f, 0.0001f,
                 "no-target Y must be exact manual");
}

void test_same_target_reacquiring_preserves_shaped_ai_continuity() {
    VectorIntentFuser fuser;
    auto input = input_for({0.0f, 0.0f}, {0.60f, 0.0f});
    (void)fuser.update(input, 0.001f);

    input.manual_stick = {0.05f, -0.02f};
    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Reacquiring;
    const auto reacquiring = fuser.update(input, 0.001f);
    require_true(
        !reacquiring.fallback,
        "same-target Reacquiring must not take the manual-only fallback");
    require_true(
        std::fabs(reacquiring.fused_stick.x - input.manual_stick.x) > 0.02f,
        "same-target Reacquiring must not unload shaped AI to manual-only");

    input.plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto coasting = fuser.update(input, 0.001f);
    require_true(
        std::fabs(coasting.fused_stick.x - reacquiring.fused_stick.x) <= 0.081f,
        "same-target reacquire must not reassert a hidden AI backlog");
}

}  // namespace

int main() {
    try {
        test_far_observed_aligned_input_keeps_full_mix();
        test_cooperative_limit_preserves_orthogonal_ai();
        test_cooperative_limit_is_rotation_invariant();
        test_coasting_cooperative_push_cannot_recreate_spring_force();
        test_coasting_micro_input_retains_normal_cooperation();
        test_far_coast_keeps_full_cooperative_authority();
        test_live_close_stack_fingerprint_is_reduced_without_axis_gate();
        test_ads_strong_cooperative_manual_uses_ai_as_primary_proposal();
        test_near_bodylock_strong_cooperative_manual_uses_ai_as_primary_proposal();
        test_near_bodylock_subfull_countersteer_retains_ai_authority();
        test_near_ai_priority_preserves_orthogonal_manual_intent();
        test_far_bodylock_full_cooperative_input_remains_manual_escape();
        test_ads_full_cooperative_input_remains_on_ai_priority_path();
        test_near_bodylock_full_opposing_input_remains_exact_escape();
        test_ads_moderate_manual_input_keeps_ordinary_cooperation();
        test_same_direction_subfull_manual_is_not_escape_feedback();
        test_ads_log_normalization_uses_effective_1p9_parallel_proposal();
        test_ads_normalized_manual_participates_when_ai_proposal_is_larger();
        test_near_bodylock_log_normalization_uses_effective_2p0_only_near();
        test_log_normalization_preserves_tangent_and_opposing_manual();
        test_log_normalization_classifies_strength_from_raw_physical_input();
        test_opposing_input_preserves_manual_and_continuously_retires_ai();
        test_escape_threshold_is_not_a_control_switch();
        test_fresh_vision_corrects_countersteer_without_touching_tangent();
        test_fresh_wrong_way_manual_floor_cancels_obsolete_radial_demand();
        test_fresh_ai_wrong_way_is_bounded_by_position_evidence();
        test_strong_manual_and_ai_are_one_radial_proposal_envelope();
        test_fresh_shared_envelope_is_continuous_below_old_strength_gate();
        test_fresh_envelope_uses_configured_dynamic_target_curve();
        test_fresh_final_continuity_is_source_agnostic();
        test_zero_authority_stays_manual_safe();
        test_fresh_post_slew_radial_guard_survives_center_crossing();
        test_active_bodylock_envelope_limits_manual_and_ai();
        test_shaped_ai_authority_is_not_applied_twice();
        test_rotated_force_ellipse_uses_both_tangent_signs();
        test_ads_to_bodylock_post_slew_respects_new_ellipse();
        test_active_ads_envelope_uses_acquisition_window_and_force();
        test_manual_escape_stays_latched_until_release_or_identity_change();
        test_reacquire_and_target_change_fail_safe_to_manual();
        test_reliability_boundary_does_not_drop_and_reassert_ai();
        test_reacquire_to_observed_reenters_through_existing_slew();
        test_reacquiring_release_edge_is_bounded();
        test_full_manual_escape_preempts_reacquiring_release_slew();
        test_full_manual_escape_cannot_be_blocked_by_saturated_ai();
        test_diagonal_manual_escape_is_preserved_exactly();
        test_nonfinite_input_returns_exact_physical_manual();
        test_remaining_work_rotation_does_not_reproject_stable_manual_input();
        test_visual_reference_rotation_is_not_a_one_tick_output_rotation();
        test_target_change_reenters_from_manual_baseline();
        test_no_target_replacement_uses_manual_admission_tick();
        test_no_target_is_exact_manual();
        test_same_target_reacquiring_preserves_shaped_ai_continuity();
        std::cout << "[VectorIntentFuserTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[VectorIntentFuserTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
