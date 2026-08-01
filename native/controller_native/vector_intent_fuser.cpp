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
    config_.ai_priority_parallel_headroom = std::clamp(
        config_.ai_priority_parallel_headroom, 0.0f, 1.0f);
    config_.ai_priority_opposing_manual_retention = std::clamp(
        config_.ai_priority_opposing_manual_retention, 0.0f, 1.0f);
    config_.ads_same_direction_manual_scale = std::clamp(
        config_.ads_same_direction_manual_scale, 0.0f, 1.0f);
    config_.near_bodylock_same_direction_manual_scale = std::clamp(
        config_.near_bodylock_same_direction_manual_scale, 0.0f, 1.0f);
    config_.ai_priority_manual_start = std::clamp(
        config_.ai_priority_manual_start, 0.02f, 0.94f);
    config_.ai_priority_manual_full = std::clamp(
        config_.ai_priority_manual_full,
        config_.ai_priority_manual_start + 0.001f, 0.95f);
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
    const float authority = std::clamp(
        std::min(input.plan.aim_authority, input.plan.reliability),
        0.0f, 1.0f);
    const float bodylock_priority_scope =
        input.plan.mode == pipeline_contract::ControlMode::BodyLockFollow
        ? smoothstep((input.plan.normalized_size - 0.18f) / 0.06f)
        : 0.0f;
    const float assisted_priority_scope =
        config_.assisted_ai_priority_enabled ? authority *
        (input.plan.mode == pipeline_contract::ControlMode::AdsAcquire
            ? 1.0f : bodylock_priority_scope) : 0.0f;
    const float strong_manual_commitment =
        ai_magnitude > kIntentDeadzone &&
            manual_magnitude > kIntentDeadzone
        ? smoothstep(
            (manual_magnitude - config_.ai_priority_manual_start) /
            (config_.ai_priority_manual_full -
             config_.ai_priority_manual_start))
        : 0.0f;
    pipeline_contract::Vec2f ai_direction{};
    if (ai_magnitude > kIntentDeadzone) {
        ai_direction = {
            input.shaped_ai_stick.x / ai_magnitude,
            input.shaped_ai_stick.y / ai_magnitude};
    }
    const float manual_ai_alignment =
        dot(input.manual_stick, input.shaped_ai_stick);
    const bool cooperative_manual =
        ai_magnitude > kIntentDeadzone && manual_ai_alignment > 0.0f;
    // A near-full physical deflection is an unconditional ownership request.
    // Same-direction input is cooperative rather than an escape request: it
    // stays on the AI-priority proposal path so high camera sensitivity cannot
    // turn manual + AI into two additive forces.
    const bool full_manual_escape =
        input.manual_confidence > 0.0f &&
        manual_magnitude >= std::max(
            0.95f, config_.manual_escape_threshold) &&
        !(cooperative_manual && assisted_priority_scope > 0.001f);
    const bool directional_manual_escape =
        manual_magnitude >= config_.manual_escape_threshold &&
        opposing_projection > 0.001f &&
        manual_magnitude >= ai_magnitude &&
        opposing_projection >= ai_magnitude * 0.995f &&
        assisted_priority_scope <= 0.001f;
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
    // Classify intent and escape from the raw physical stick above, then
    // normalize only the proposal that is actually allowed to cooperate with
    // AI. This keeps the 0.45..0.70 ownership transition stable instead of
    // accidentally disabling it by scaling the evidence first.
    float desired_same_direction_scale = 1.0f;
    if (input.plan.mode == pipeline_contract::ControlMode::AdsAcquire) {
        desired_same_direction_scale =
            config_.ads_same_direction_manual_scale;
    } else if (input.plan.mode ==
               pipeline_contract::ControlMode::BodyLockFollow) {
        desired_same_direction_scale =
            config_.near_bodylock_same_direction_manual_scale;
    }
    const float manual_normalization_commitment =
        config_.contextual_manual_normalization_enabled
        ? assisted_priority_scope * strong_manual_commitment : 0.0f;
    const float same_direction_manual_scale = manual_ai_alignment > 0.0f
        ? 1.0f - manual_normalization_commitment *
            (1.0f - desired_same_direction_scale)
        : 1.0f;
    pipeline_contract::Vec2f fusion_manual_stick = input.manual_stick;
    if (manual_ai_alignment > 0.0f && ai_magnitude > kIntentDeadzone) {
        const float raw_manual_parallel =
            dot(input.manual_stick, ai_direction);
        const float removed_parallel = std::max(
            0.0f,
            raw_manual_parallel * (1.0f - same_direction_manual_scale));
        fusion_manual_stick.x -= ai_direction.x * removed_parallel;
        fusion_manual_stick.y -= ai_direction.y * removed_parallel;
    }
    // Same-target Reacquiring means that the coordinator has fresh evidence
    // for the existing owner. The shaper may continue its normal request
    // slew; only TargetChanged/None/Manual owns a hard admission boundary.
    // Outside assisted AI-priority scope the accepted legacy ownership curve
    // remains intact. Inside it, physical input is an alternative proposal;
    // full escape and hard identity boundaries still preserve it exactly.
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
    if (manual_magnitude > kIntentDeadzone) {
        const float cooperative_projection = std::max(
            0.0f, dot(fused_ai, manual_direction));
        const float combined_parallel =
            manual_magnitude + cooperative_projection;
        // Live close-target failures appeared in the largest-size quartile,
        // starting around normalized size 0.22, and only after same-direction
        // parallel demand exceeded ~0.32.
        // Smooth both boundaries so ordinary/far cooperation remains exactly
        // unchanged and no new threshold impulse is introduced.
        const float proximity_commitment = smoothstep(
            (input.plan.normalized_size - 0.22f) / 0.02f);
        const float stack_commitment = smoothstep(
            (combined_parallel - 0.32f) / 0.08f);
        const float cooperative_commitment =
            manual_commitment * proximity_commitment * stack_commitment;
        if (cooperative_projection > 0.001f &&
            cooperative_commitment > 0.0f) {
            // Manual and AI are control proposals, not additive forces. Keep
            // the stronger parallel proposal and bounded headroom from the
            // weaker one; preserve the complete orthogonal follow vector.
            const float cooperative_overlap =
                1.0f - config_.manual_preservation_floor;
            const float dominant_parallel = std::max(
                manual_magnitude, cooperative_projection);
            const float weaker_parallel = std::min(
                manual_magnitude, cooperative_projection);
            const float bounded_combined_parallel =
                dominant_parallel +
                weaker_parallel * cooperative_overlap;
            const float retained_cooperative_ai = std::max(
                0.0f,
                bounded_combined_parallel - manual_magnitude);
            const float cooperative_reduction =
                (retained_cooperative_ai - cooperative_projection) *
                cooperative_commitment;
            fused_ai.x += manual_direction.x * cooperative_reduction;
            fused_ai.y += manual_direction.y * cooperative_reduction;
        }
    }
    const float fused_ai_magnitude = length(fused_ai);
    const float legacy_ai_weight = ai_magnitude > 0.001f
        ? std::clamp(fused_ai_magnitude / ai_magnitude, 0.0f, 1.0f)
        : 0.0f;
    pipeline_contract::Vec2f fused_target{
        fusion_manual_stick.x + fused_ai.x,
        fusion_manual_stick.y + fused_ai.y};
    float applied_manual_weight = same_direction_manual_scale;
    float applied_ai_weight = legacy_ai_weight;
    float ai_priority_commitment = 0.0f;
    if (ai_magnitude > kIntentDeadzone &&
        manual_magnitude > kIntentDeadzone &&
        assisted_priority_scope > 0.0f) {
        ai_priority_commitment =
            assisted_priority_scope * strong_manual_commitment;
        if (ai_priority_commitment > 0.0f) {
            const float manual_parallel =
                dot(fusion_manual_stick, ai_direction);
            const pipeline_contract::Vec2f manual_tangent{
                fusion_manual_stick.x - ai_direction.x * manual_parallel,
                fusion_manual_stick.y - ai_direction.y * manual_parallel};
            float priority_parallel = ai_magnitude;
            float priority_manual_weight =
                config_.ai_priority_opposing_manual_retention;
            if (manual_parallel >= 0.0f) {
                // AI remains the primary absolute proposal. A bounded share
                // of the normalized manual proposal always participates, so
                // the old asymmetry (manual kept, AI decayed) is not replaced
                // by the opposite asymmetry (manual ignored below AI).
                priority_parallel += manual_parallel *
                    config_.ai_priority_parallel_headroom;
                priority_manual_weight =
                    config_.ai_priority_parallel_headroom *
                    same_direction_manual_scale;
            } else {
                priority_parallel += manual_parallel *
                    config_.ai_priority_opposing_manual_retention;
            }
            pipeline_contract::Vec2f priority_target{
                ai_direction.x * priority_parallel + manual_tangent.x,
                ai_direction.y * priority_parallel + manual_tangent.y};
            // Approach exact manual continuously before a non-cooperative
            // near-full escape. Same-direction full input remains cooperative.
            const float escape_commitment = manual_parallel <= 0.0f
                ? smoothstep((manual_magnitude - 0.85f) / 0.10f)
                : 0.0f;
            priority_target = {
                priority_target.x * (1.0f - escape_commitment) +
                    input.manual_stick.x * escape_commitment,
                priority_target.y * (1.0f - escape_commitment) +
                    input.manual_stick.y * escape_commitment};
            priority_manual_weight +=
                (1.0f - priority_manual_weight) * escape_commitment;
            fused_target = {
                fused_target.x * (1.0f - ai_priority_commitment) +
                    priority_target.x * ai_priority_commitment,
                fused_target.y * (1.0f - ai_priority_commitment) +
                    priority_target.y * ai_priority_commitment};
            applied_manual_weight =
                applied_manual_weight * (1.0f - ai_priority_commitment) +
                priority_manual_weight * ai_priority_commitment;
            applied_ai_weight = legacy_ai_weight +
                ai_priority_commitment * (1.0f - legacy_ai_weight);
        }
    }
    decision.fused_stick = {
        std::clamp(fused_target.x, -1.0f, 1.0f),
        std::clamp(fused_target.y, -1.0f, 1.0f),
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
    decision.target_manual_weight = applied_manual_weight;
    decision.target_ai_weight = applied_ai_weight;
    decision.target_tangential_manual_weight = 1.0f;
    decision.applied_manual_weight = applied_manual_weight;
    decision.applied_ai_weight = applied_ai_weight;
    decision.applied_tangential_manual_weight = 1.0f;
    decision.manual_escape = deliberate_manual_escape;
    decision.fallback = false;
    decision.reason = FusionFallbackReason::None;
    if (applied_ai_weight <= 0.001f) {
        decision.candidate = FusionCandidate::ManualOnly;
    } else if (ai_priority_commitment > 0.001f) {
        decision.candidate = FusionCandidate::RadialCorrected;
    } else if (applied_ai_weight >= 0.999f) {
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
