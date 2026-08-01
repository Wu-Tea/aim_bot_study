#include "vector_intent_fuser.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace controller_native {
namespace {

float dot(pipeline_contract::Vec2f left,
          pipeline_contract::Vec2f right) noexcept {
    return left.x * right.x + left.y * right.y;
}

float length(pipeline_contract::Vec2f value) noexcept {
    return std::hypot(value.x, value.y);
}

bool finite_vec(pipeline_contract::Vec2f value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float smoothstep(float value) noexcept {
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

}  // namespace

FusionWeights candidate_weights(FusionCandidate candidate) noexcept {
    switch (candidate) {
        case FusionCandidate::ExistingMix: return {1.0f, 1.0f, 1.0f};
        case FusionCandidate::ManualSupported: return {1.0f, 0.5f, 1.0f};
        case FusionCandidate::AiSupported: return {0.5f, 1.0f, 0.5f};
        case FusionCandidate::ManualOnly: return {1.0f, 0.0f, 1.0f};
        case FusionCandidate::AiOnly: return {0.0f, 1.0f, 0.0f};
        case FusionCandidate::ReducedMix: return {0.5f, 0.5f, 0.5f};
        case FusionCandidate::RadialCorrected: return {0.5f, 1.0f, 1.0f};
        case FusionCandidate::RadialReplaced: return {0.0f, 1.0f, 1.0f};
        case FusionCandidate::TangentialCorrected: return {1.0f, 1.0f, 0.5f};
        case FusionCandidate::TangentialReplaced: return {1.0f, 1.0f, 0.0f};
        case FusionCandidate::FreshVisionCounterCorrected:
            return {0.0f, 1.0f, 1.0f};
    }
    return {1.0f, 0.0f, 1.0f};
}

VectorIntentFuser::VectorIntentFuser(VectorIntentFusionConfig config)
    : config_(config) {
    config_.manual_escape_threshold = std::clamp(
        config_.manual_escape_threshold, 0.05f, 1.0f);
    config_.manual_preservation_floor = std::clamp(
        config_.manual_preservation_floor, 0.0f, 1.0f);
}

VectorIntentFusionDecision VectorIntentFuser::update(
    const VectorIntentFusionInput& input, float dt_seconds) noexcept {
    VectorIntentFusionDecision decision{};
    decision.candidate_costs.fill(std::numeric_limits<float>::infinity());

    auto manual_only = [&](FusionFallbackReason reason,
                           bool preserve_output_continuity) {
        const pipeline_contract::Vec2f manual_stick = finite_vec(
            input.manual_stick) ? input.manual_stick
                                 : pipeline_contract::Vec2f{};
        decision.fused_stick = manual_stick;
        decision.candidate = FusionCandidate::ManualOnly;
        decision.target_manual_weight = 1.0f;
        decision.target_ai_weight = 0.0f;
        decision.target_tangential_manual_weight = 1.0f;
        decision.applied_manual_weight = 1.0f;
        decision.applied_ai_weight = 0.0f;
        decision.applied_tangential_manual_weight = 1.0f;
        decision.reason = reason;
        decision.fallback = reason != FusionFallbackReason::None;
        decision.candidate_costs[
            static_cast<std::size_t>(FusionCandidate::ManualOnly)] = 0.0f;
        if (preserve_output_continuity && output_initialized_) {
            const pipeline_contract::Vec2f output_delta{
                manual_stick.x - previous_output_.x,
                manual_stick.y - previous_output_.y};
            const float output_delta_length = length(output_delta);
            const float maximum_release_step = std::min(
                200.0f * std::clamp(dt_seconds, 0.0001f, 0.05f),
                0.20f);
            if (output_delta_length > maximum_release_step &&
                output_delta_length > 1.0e-6f) {
                const float scale = maximum_release_step / output_delta_length;
                decision.fused_stick = {
                    previous_output_.x + output_delta.x * scale,
                    previous_output_.y + output_delta.y * scale};
            }
        }
        // Manual fallback is an output state, not a request to cold-start the
        // next AI tick. Keep the actual released output as the re-entry
        // baseline so lifecycle boundaries remain continuous.
        previous_output_ = decision.fused_stick;
        output_initialized_ = true;
        reentry_pending_ = true;
        const pipeline_contract::Vec2f residual_ai{
            decision.fused_stick.x - manual_stick.x,
            decision.fused_stick.y - manual_stick.y};
        const float shaped_ai_magnitude = length(input.shaped_ai_stick);
        decision.applied_ai_weight = shaped_ai_magnitude > 0.001f
            ? std::clamp(length(residual_ai) / shaped_ai_magnitude,
                         0.0f, 1.0f)
            : 0.0f;
        return decision;
    };

    if (!finite_vec(input.manual_stick) ||
        !finite_vec(input.shaped_ai_stick) ||
        !std::isfinite(input.manual_confidence) ||
        !std::isfinite(dt_seconds) ||
        !pipeline_contract::valid(input.plan)) {
        return manual_only(FusionFallbackReason::NonFinite, false);
    }
    if (input.plan.target_id == 0 ||
        input.plan.lifecycle == pipeline_contract::TargetLifecycle::None ||
        input.plan.mode == pipeline_contract::ControlMode::Manual) {
        // Retain the last nonzero identity for the rest of this LT epoch. If
        // ownership expires and a different person appears while LT is still
        // held, the first replacement tick must pass through TargetChanged
        // instead of being mistaken for an initial acquisition. reset() still
        // clears this memory on a deliberate new ADS epoch.
        return manual_only(FusionFallbackReason::NoTarget, false);
    }
    if (target_id_ != 0 && target_id_ != input.plan.target_id) {
        target_id_ = input.plan.target_id;
        return manual_only(FusionFallbackReason::TargetChanged, false);
    }
    target_id_ = input.plan.target_id;
    const float manual_magnitude = length(input.manual_stick);
    const float ai_magnitude = length(input.shaped_ai_stick);
    constexpr float kIntentDeadzone = 0.02f;
    const float commitment_span = std::max(
        0.001f, config_.manual_escape_threshold - kIntentDeadzone);
    // IntentFilter confidence already derives from stick magnitude. Using it
    // as another multiplier squares the same evidence and makes deliberate
    // 10-30% corrections look like drift. Confidence gates the path; the
    // continuous magnitude curve owns commitment strength.
    const float manual_commitment = input.manual_confidence > 0.0f
        ? smoothstep(
            (manual_magnitude - kIntentDeadzone) / commitment_span)
        : 0.0f;

    float opposing_projection = 0.0f;
    pipeline_contract::Vec2f fused_ai = input.shaped_ai_stick;
    pipeline_contract::Vec2f manual_direction{};
    if (manual_magnitude > kIntentDeadzone) {
        manual_direction = {
            input.manual_stick.x / manual_magnitude,
            input.manual_stick.y / manual_magnitude};
        opposing_projection = std::max(
            0.0f, -dot(input.shaped_ai_stick, manual_direction));
    }
    // A near-full physical deflection is an unconditional ownership request.
    // In particular, neither lifecycle release smoothing nor an over-range
    // shaped AI vector may keep the camera moving against that request.
    const bool full_manual_escape =
        input.manual_confidence > 0.0f &&
        manual_magnitude >= std::max(
            0.95f, config_.manual_escape_threshold);
    const bool directional_manual_escape =
        manual_magnitude >= config_.manual_escape_threshold &&
        opposing_projection > 0.001f &&
        manual_magnitude >= ai_magnitude &&
        opposing_projection >= ai_magnitude * 0.995f;
    const bool deliberate_manual_escape =
        full_manual_escape || directional_manual_escape;
    if (deliberate_manual_escape) {
        decision.fused_stick = input.manual_stick;
        decision.candidate = FusionCandidate::ManualOnly;
        decision.target_manual_weight = 1.0f;
        decision.target_ai_weight = 0.0f;
        decision.target_tangential_manual_weight = 1.0f;
        decision.applied_manual_weight = 1.0f;
        decision.applied_ai_weight = 0.0f;
        decision.applied_tangential_manual_weight = 1.0f;
        decision.reason = FusionFallbackReason::ManualEscape;
        decision.fallback = false;
        decision.manual_escape = true;
        decision.candidate_costs[
            static_cast<std::size_t>(FusionCandidate::ManualOnly)] = 0.0f;
        previous_output_ = input.manual_stick;
        output_initialized_ = true;
        reentry_pending_ = true;
        return decision;
    }
    if (input.plan.lifecycle ==
        pipeline_contract::TargetLifecycle::Reacquiring) {
        return manual_only(FusionFallbackReason::Reacquiring, true);
    }
    // Manual input is never rewritten.  Only AI authority retreats, and it
    // does so continuously as deliberate counter-steer grows.  Fresh Vision,
    // target prediction, and response horizons are intentionally absent here:
    // TargetCoordinator is their sole owner.
    if (opposing_projection > 0.001f) {
        // AI may brake a wrong manual push, but its opposing component may
        // never consume more than the configured share of physical intent.
        // Interpolate into that bound over the whole commitment range so the
        // brake cannot disappear and then reassert at the old threshold.
        const float bounded_brake = manual_magnitude *
            (1.0f - config_.manual_preservation_floor);
        const float retained_opposing_ai =
            opposing_projection * (1.0f - manual_commitment) +
            bounded_brake * manual_commitment;
        const float opposing_retention = std::clamp(
            retained_opposing_ai / opposing_projection, 0.0f, 1.0f);
        // Decompose in the user's actual 2-D direction. Only the conflicting
        // projection is bounded; useful orthogonal target-follow motion is
        // untouched. This is rotationally invariant and avoids X/Y gates.
        const pipeline_contract::Vec2f opposing_component{
            -manual_direction.x * opposing_projection,
            -manual_direction.y * opposing_projection};
        fused_ai.x += opposing_component.x * (opposing_retention - 1.0f);
        fused_ai.y += opposing_component.y * (opposing_retention - 1.0f);
    }
    const float fused_ai_magnitude = length(fused_ai);
    const float ai_weight = ai_magnitude > 0.001f
        ? std::clamp(fused_ai_magnitude / ai_magnitude, 0.0f, 1.0f)
        : 0.0f;
    decision.fused_stick = {
        std::clamp(
            input.manual_stick.x + fused_ai.x,
            -1.0f, 1.0f),
        std::clamp(
            input.manual_stick.y + fused_ai.y,
            -1.0f, 1.0f),
    };
    // This is the only final-output memory in the aim chain. It limits the
    // vector result itself (manual + bounded AI), so neither manual mistakes
    // nor fresh AI observations can create a one-tick camera impulse. At
    // Yielding toward deliberate manual input is fast; AI reassertion is
    // slower. This avoids making escape feel sticky without allowing a fresh
    // observation to snap ownership back in one tick.
    if (!output_initialized_) {
        previous_output_ = decision.fused_stick;
        output_initialized_ = true;
    }
    const pipeline_contract::Vec2f output_delta{
        decision.fused_stick.x - previous_output_.x,
        decision.fused_stick.y - previous_output_.y};
    const float output_delta_length = length(output_delta);
    const bool yielding_to_manual =
        !reentry_pending_ &&
        manual_magnitude > kIntentDeadzone &&
        dot(output_delta, manual_direction) > 0.0f;
    const float output_slew_rate = yielding_to_manual ? 200.0f : 80.0f;
    const float maximum_output_step = std::min(
        output_slew_rate * std::clamp(dt_seconds, 0.0001f, 0.05f),
        yielding_to_manual ? 0.20f : 0.08f);
    if (output_delta_length > maximum_output_step &&
        output_delta_length > 1.0e-6f) {
        const float scale = maximum_output_step / output_delta_length;
        decision.fused_stick = {
            previous_output_.x + output_delta.x * scale,
            previous_output_.y + output_delta.y * scale};
    }
    previous_output_ = decision.fused_stick;
    reentry_pending_ = false;
    decision.target_manual_weight = 1.0f;
    decision.target_ai_weight = ai_weight;
    decision.target_tangential_manual_weight = 1.0f;
    decision.applied_manual_weight = 1.0f;
    decision.applied_ai_weight = ai_weight;
    decision.applied_tangential_manual_weight = 1.0f;
    decision.manual_escape = deliberate_manual_escape;
    decision.fallback = false;
    decision.reason = FusionFallbackReason::None;
    if (ai_weight <= 0.001f) {
        decision.candidate = FusionCandidate::ManualOnly;
    } else if (ai_weight >= 0.999f) {
        decision.candidate = FusionCandidate::ExistingMix;
    } else {
        decision.candidate = FusionCandidate::ManualSupported;
    }
    decision.candidate_costs[
        static_cast<std::size_t>(decision.candidate)] = 0.0f;
    return decision;
}

void VectorIntentFuser::reset() noexcept {
    target_id_ = 0;
    previous_output_ = {};
    output_initialized_ = false;
    reentry_pending_ = false;
}

}  // namespace controller_native
